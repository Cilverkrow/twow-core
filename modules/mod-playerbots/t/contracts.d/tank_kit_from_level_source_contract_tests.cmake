# twow-repo#602 ROT-01 tank count (OB-00 28.10.2026): AiPlayerbot.Rotation.TankKitFromLevel (code default 0, behaviour change, A/B with 2)
# gives the tank kit only from the level of the tank spells: below level 10 every warrior had the default tab Protection (live540: 47 of
# 90 warriors pulled as tanks), and a protection paladin below Righteous Fury (16) tanked without it. With the switch: warrior below 10 =
# arms kit, protection paladin below 16 = retribution kit, through one kit tab (AiFactory::GetPlayerKitTab) in all four engines and the role
# offer, random bots only, a forced tank role kept; a level crossing rebuilds the strategies. This contract pins the dist key, the
# config member and read (unknown modes = off), the parsed mode in [ConfigEcho] (owner rule #615), the pure policy, the kit tab and its
# users, the level crossing rebuild and the [TankKit] minute line. The rules are tested in t/tank_kit_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#602 TankKitFromLevel: missing ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("TankKitPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("AiFactory.cpp" factory)
read_source("AiFactory.h" factory_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)

# 1. The switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.TankKitFromLevel = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 rotationTankKitFromLevel = 0;" "config member, default 0")
require_text("${config_cpp}" "rotationTankKitFromLevel = config.GetIntDefault(\"AiPlayerbot.Rotation.TankKitFromLevel\", 0);   // twow-repo#602 tank count\n    if (rotationTankKitFromLevel > 2)\n        rotationTankKitFromLevel = 0;" "config read, > 2 = off")
require_text("${config_cpp}" "            {\"Rotation.SpecFromPath\", Value(rotationSpecFromPath)},\n            {\"Rotation.TankKitFromLevel\", Value(rotationTankKitFromLevel)},\n" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only) and its levels.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 TankKitFromLevel: TankKitPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "constexpr std::uint32_t kWarriorTankLevel = 10;" "the warrior key level")
require_text("${policy}" "constexpr std::uint32_t kPaladinTankLevel = 16;" "the paladin key level")
require_text("${policy}" "    if (!applies)\n        return false;" "off never demotes")

# 3. AiFactory: one kit tab (GetPlayerKitTab) for all four engines and the role offer; the spec tab (talent choice, chat) is unchanged.
require_text("${factory}" "#include \"playerbot/TankKitPolicy.h\"" "the policy include")
require_text("${factory_h}" "static int GetPlayerKitTab(const Player* player);" "the kit tab declaration")
string(FIND "${factory}" "int AiFactory::GetPlayerKitTab(const Player* player)" kat)
if(kat EQUAL -1)
  message(FATAL_ERROR "#602 TankKitFromLevel: AiFactory::GetPlayerKitTab not found")
endif()
string(SUBSTRING "${factory}" ${kat} 1200 kit)
require_text("${kit}" "    int const tab = GetPlayerSpecTab(player);\n    if (!sPlayerbotAIConfig.rotationTankKitFromLevel || !player)\n        return tab;" "off = the spec tab")
require_text("${kit}" "bool const tankTab = ai::tank_kit::IsTankTab(cls, tab);" "only the warrior / paladin tank tabs")
require_text("${policy}" "return (playerClass == kClassWarrior && tab == 2) || (playerClass == kClassPaladin && tab == 1);" "the tank tabs")
require_text("${kit}" "if (!tankTab || !botAi || !sRandomPlayerbotMgr.IsRandomBot(const_cast<Player*>(player)) || (botAi->GetForcedRole() & BOT_ROLE_TANK))\n        return tab;" "random bots only, a forced tank role keeps the tank kit")
require_text("${kit}" "if (!ai::tank_kit::TooEarly(ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, player->GetGUIDLow()), cls, player->GetLevel()))\n        return tab;\n\n    return ai::tank_kit::DpsTab(cls);" "below the key level the DPS tab")
require_text("${factory}" "    return GetPlayerRoles(player->getClass(), GetPlayerKitTab(player));" "the role offer follows the kit tab")
require_text("${factory}" "    int tab = GetPlayerKitTab(player);\n    if (tab != GetPlayerSpecTab(player))\n        ai::tank_kit::demoted.fetch_add(1, std::memory_order_relaxed);" "the combat engine follows the kit tab and counts")
count_text("${factory}" "    const int tab = GetPlayerKitTab(player);" engines)
if(NOT engines EQUAL 3)
  message(FATAL_ERROR "#602 TankKitFromLevel: expected the non-combat, dead and reaction engines on the kit tab (3), found ${engines}")
endif()
count_text("${factory}" "const int tab = GetPlayerSpecTab(player);" stale)
if(NOT stale EQUAL 0)
  message(FATAL_ERROR "#602 TankKitFromLevel: an engine still reads the spec tab (${stale})")
endif()
# talent choice keeps the spec tab (a protection paladin 10-15 still spends points in Protection)
require_text("${ai_cpp}" "            talentsTab = AiFactory::GetPlayerSpecTab(bot);" "talent choice keeps the spec tab")
# the low-level default tab (the reason for the switch) and the druid leveling kit (why druids are not gated) are unchanged
require_text("${factory}" "        case CLASS_WARRIOR:\n            tab = 2;\n            break;" "the warrior default tab below 10 stays (the switch gates the kit, not the tab)")
require_text("${factory}" "            if (player->GetLevel() < 10)\n            {\n                combatEngine->addStrategies(\"leveling\", \"dps assist\", \"cure\", NULL);\n                break;\n            }" "druids below 10 keep the leveling kit")

# 3b. The level crossing rebuild in UpdateAIInternal (strategies are not rebuilt on a level-up).
require_text("${ai_cpp}" "#include \"playerbot/TankKitPolicy.h\"" "the policy include in PlayerbotAI.cpp")
require_text("${ai_h}" "uint32 tankKitLevel = 0;" "the level member")
require_text("${ai_cpp}" "    if (sPlayerbotAIConfig.rotationTankKitFromLevel && ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, bot->GetGUIDLow()) &&\n        sRandomPlayerbotMgr.IsRandomBot(bot))\n    {\n        uint32 const level = bot->GetLevel();\n        if (level != tankKitLevel)\n        {\n            // only a bot on its tank tab changes its kit at the crossing (an arms warrior reaching 10 stays arms: no rebuild)\n            bool const crossed = ai::tank_kit::CrossesKey(bot->getClass(), tankKitLevel, level) &&\n                ai::tank_kit::IsTankTab(bot->getClass(), AiFactory::GetPlayerSpecTab(bot));\n            tankKitLevel = level;\n            if (crossed)\n            {\n                ai::tank_kit::rebuilt.fetch_add(1, std::memory_order_relaxed);\n                ResetStrategies();\n            }\n        }\n    }" "one rebuild per crossing, only random bots on their tank tab where the switch applies (OB-10 28.10.)")
require_text("${policy}" "return key && fromLevel && (fromLevel < key) != (toLevel < key);" "the crossing rule (never on the first pass)")
# the low-level default tab (the reason for the switch) and the druid leveling kit (why druids are not gated) are unchanged
require_text("${factory}" "        case CLASS_WARRIOR:\n            tab = 2;\n            break;" "the warrior default tab below 10 stays (the switch gates the kit, not the tab)")
require_text("${factory}" "            if (player->GetLevel() < 10)\n            {\n                combatEngine->addStrategies(\"leveling\", \"dps assist\", \"cure\", NULL);\n                break;\n            }" "druids below 10 keep the leveling kit")

# 4. The [TankKit] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/TankKitPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationTankKitFromLevel || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[TankKit] mode=%u demoted=%llu rebuilt=%llu\", sPlayerbotAIConfig.rotationTankKitFromLevel,\n        (unsigned long long)ai::tank_kit::demoted.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::tank_kit::rebuilt.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportTankKit();" "the minute report is called")

message(STATUS "#602 TankKitFromLevel source contract passed")
