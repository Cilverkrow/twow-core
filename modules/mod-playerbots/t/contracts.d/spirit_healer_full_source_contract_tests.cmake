# Owner order 27.10.2026 (via OB-00): AiPlayerbot.Revive.SpiritHealerFull, code default 1 (0 = rollback), revives a bot at a
# spirit healer at full health and mana without Resurrection Sickness. This contract pins the switch: dist key, config member
# and read (unknown modes = the default 1), the parsed mode in [ConfigEcho] (owner rule #615, sorted #710), the pure policy,
# the one call site in the bot's spirit healer revive (the only bot ResurrectPlayer(0.5f ...)), the full= field of the revive
# line, and that the core's spirit healer for real players is untouched. The rules are tested in
# t/spirit_healer_revive_policy_tests.cpp.

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
    message(FATAL_ERROR "SpiritHealerFull: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "SpiritHealerFull: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("SpiritHealerRevivePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("strategy/actions/ReviveFromCorpseAction.cpp" revive)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho].
require_text("${dist}" "\nAiPlayerbot.Revive.SpiritHealerFull = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 reviveSpiritHealerFull = 1;" "config member, default 1")
require_text("${config_cpp}" "reviveSpiritHealerFull = config.GetIntDefault(\"AiPlayerbot.Revive.SpiritHealerFull\", 1);   // owner order 27.10.2026\n    if (reviveSpiritHealerFull > 2)\n        reviveSpiritHealerFull = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"QuestRescue.KeepRequestWhenFull\", Value(questRescueKeepRequestWhenFull)},\n            {\"Revive.SpiritHealerFull\", Value(reviveSpiritHealerFull)},\n            {\"RosterFarMove.ArriveSafe\"" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only).
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "SpiritHealerFull: SpiritHealerRevivePolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "inline bool FullApplies(std::uint32_t mode, std::uint32_t guidLow)\n{\n    return ai::cohort::Applies(mode, guidLow);\n}" "the cohort rule")
require_text("${policy}" "return full ? 1.0f : 0.5f;" "100 % full, 50 % off")
require_text("${policy}" "return !full && !repairCheat;" "no sickness when full; off: none with the repair cheat")

# 3. The call site: the switch read per bot, the revive line with full=, the policy in ResurrectPlayer, the old literal gone.
require_text("${revive}" "#include \"playerbot/SpiritHealerRevivePolicy.h\"" "the policy include")
require_text("${revive}" "bool const fullRevive = ai::spirit_healer::FullApplies(sPlayerbotAIConfig.reviveSpiritHealerFull, bot->GetGUIDLow());" "the switch per bot")
require_text("${revive}" "corpse_dist=%.0f full=%u\"" "full= at the end of the revive line")
require_order("${revive}" "bool const fullRevive =" "sLog.outBasic(\"[BOT GRAVEYARD] event=revive" "the switch read before the revive line")
require_text("${revive}" "bot->ResurrectPlayer(ai::spirit_healer::RestorePercent(fullRevive),\n            ai::spirit_healer::ApplySickness(fullRevive, ai->HasCheat(BotCheatMask::repair)));" "the policy in ResurrectPlayer")
require_order("${revive}" "bot->ResurrectPlayer(ai::spirit_healer::RestorePercent(fullRevive)," "bot->DurabilityLossAll(0.25f, true);" "the durability loss unchanged after the revive")
string(FIND "${revive}" "ResurrectPlayer(0.5f" old_literal)
if(NOT old_literal EQUAL -1)
  message(FATAL_ERROR "SpiritHealerFull: ReviveFromCorpseAction.cpp still has a ResurrectPlayer(0.5f ...) outside the policy")
endif()

# 4. A real player's spirit healer is the core's and stays as it was (bots only).
if(DEFINED CORE_SOURCE_DIR AND EXISTS "${CORE_SOURCE_DIR}/src/game/Handlers/NPCHandler.cpp")
  file(READ "${CORE_SOURCE_DIR}/src/game/Handlers/NPCHandler.cpp" npc)
  string(REPLACE "\r" "" npc "${npc}")
  require_text("${npc}" "    _player->ResurrectPlayer(0.5f, true);\n" "the core's spirit healer for real players unchanged")
endif()

message(STATUS "SpiritHealerFull source contract passed")
