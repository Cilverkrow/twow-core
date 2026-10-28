# twow-repo#602 ROT-01 tank count (OB-00 28.10.2026): AiPlayerbot.Rotation.TankKitFromLevel (code default 0, behaviour change, A/B with 2)
# gives the tank kit only from the level of the tank spells: below level 10 every warrior had the default tab Protection (live540: 47 of
# 90 warriors pulled as tanks), and a protection paladin below Righteous Fury (16) tanked without it. With the switch: warrior below 10 =
# arms kit, protection paladin below 16 = retribution kit, in the combat and the non-combat engine. This contract pins the dist key, the
# config member and read (unknown modes = off), the parsed mode in [ConfigEcho] (owner rule #615), the pure policy, the four AiFactory
# gates (the old branches unchanged behind them) and the [TankKit] minute line. The rules are tested in t/tank_kit_policy_tests.cpp.

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

# 3. AiFactory: the include, the two combat gates (with the counter) and the two non-combat gates; the tank branches stay behind them.
require_text("${factory}" "#include \"playerbot/TankKitPolicy.h\"" "the policy include")
require_text("${factory}" "            bool const tankEarly = tab == 2 && ai::tank_kit::TooEarly(\n                ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, player->GetGUIDLow()), CLASS_WARRIOR, player->GetLevel());\n            if (tankEarly)\n                ai::tank_kit::demoted.fetch_add(1, std::memory_order_relaxed);\n            if (tab == 2 && !tankEarly)\n            {\n                combatEngine->addStrategies(\"protection\", \"tank assist\", \"pull\", \"pull back\", \"close\", NULL);\n            }\n            else if (player->GetLevel() < 30 || tab == 0)\n            {\n                combatEngine->addStrategies(\"arms\", \"dps assist\", \"behind\", NULL);" "the warrior combat gate (arms below 10)")
require_text("${factory}" "            bool const tankEarly = tab == 1 && ai::tank_kit::TooEarly(\n                ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, player->GetGUIDLow()), CLASS_PALADIN, player->GetLevel());\n            if (tankEarly)\n                ai::tank_kit::demoted.fetch_add(1, std::memory_order_relaxed);\n            if (tab == 1 && !tankEarly)\n            {\n                combatEngine->addStrategies(\"protection\", \"tank assist\", \"pull\", \"pull back\", \"close\", NULL);" "the paladin combat gate (retribution below 16)")
require_text("${factory}" "            if (tab == 1 && !ai::tank_kit::TooEarly(ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, player->GetGUIDLow()),\n                    CLASS_PALADIN, player->GetLevel()))\n            {\n                nonCombatEngine->addStrategies(\"protection\", \"tank assist\", NULL);" "the paladin non-combat gate")
require_text("${factory}" "            if (tab == 2 && !ai::tank_kit::TooEarly(ai::tank_kit::Applies(sPlayerbotAIConfig.rotationTankKitFromLevel, player->GetGUIDLow()),\n                    CLASS_WARRIOR, player->GetLevel()))\n            {\n                nonCombatEngine->addStrategies(\"protection\", \"tank assist\", NULL);\n            }\n            else if(player->GetLevel() < 30 || tab == 0)\n            {\n                nonCombatEngine->addStrategies(\"arms\", \"dps assist\", NULL);" "the warrior non-combat gate")
count_text("${factory}" "ai::tank_kit::TooEarly(" gates)
if(NOT gates EQUAL 4)
  message(FATAL_ERROR "#602 TankKitFromLevel: expected 4 TooEarly gates in AiFactory.cpp, found ${gates}")
endif()
# the low-level default tab (the reason for the switch) and the druid leveling kit (why druids are not gated) are unchanged
require_text("${factory}" "        case CLASS_WARRIOR:\n            tab = 2;\n            break;" "the warrior default tab below 10 stays (the switch gates the kit, not the tab)")
require_text("${factory}" "            if (player->GetLevel() < 10)\n            {\n                combatEngine->addStrategies(\"leveling\", \"dps assist\", \"cure\", NULL);\n                break;\n            }" "druids below 10 keep the leveling kit")

# 4. The [TankKit] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/TankKitPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationTankKitFromLevel || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[TankKit] mode=%u demoted=%llu\", sPlayerbotAIConfig.rotationTankKitFromLevel,\n        (unsigned long long)ai::tank_kit::demoted.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportTankKit();" "the minute report is called")

message(STATUS "#602 TankKitFromLevel source contract passed")
