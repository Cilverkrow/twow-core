# twow-repo#601 R01 v4 (OB-00 19.10.2026: R01 as bot cohorts on two realms): AiPlayerbot.QuestFirstProgression.S1Cohort /
# S3Cohort say for which bots the S1 and S3 key groups act (0/1 all, 2 only odd guids, 3 only even guids, other none).
# Every per-bot read of a group key goes through S1For / S3For; global log lines stay on the keys. Reads named files only.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 R01 cohort: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 R01 cohort: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" len)
  set(n 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#601 R01 cohort: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("R01CohortPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
read_source("strategy/actions/DropQuestAction.cpp" drop_cpp)
read_source("strategy/values/TravelValues.cpp" values_cpp)
read_source("TravelMgr.cpp" travel_cpp)

# 1. Pure policy on the CohortPolicy.h parity rule; unknown values act for no bot.
count_text("${policy}" "#include \"" 1 "one include (CohortPolicy.h, std otherwise)")
require_text("${policy}" "#include \"CohortPolicy.h\"" "the shared parity rule")
require_text("${policy}" "    return mode == ModeAll || mode == ModeAllToo || (mode == ModeOnlyB && ai::cohort::InB(guidLow)) ||\n        (mode == ModeOnlyA && !ai::cohort::InB(guidLow));" "0/1 all, 2 odd, 3 even, else none")
require_text("${policy}" "constexpr std::uint32_t ModeAll = 0;" "0 = all")
require_text("${policy}" "constexpr std::uint32_t ModeOnlyB = 2;" "2 = cohort B")
require_text("${policy}" "constexpr std::uint32_t ModeOnlyA = 3;" "3 = cohort A")

# 2. Keys: default 0 (every bot, as before), restart-only, documented; accessors on the parsed members.
string(FIND "${config_h}" "CohortPolicy.h" cohort_in_h)
if(NOT cohort_in_h EQUAL -1)
  message(FATAL_ERROR "#601 R01 cohort: forbidden cohort header in PlayerbotAIConfig.h (botpch chain)")
endif()
require_text("${config_cpp}" "#include \"playerbot/R01CohortPolicy.h\"" "policy include in the config source")
require_text("${config_h}" "    uint32 questFirstProgressionS1Cohort = 0;" "S1Cohort default 0")
require_text("${config_h}" "    uint32 questFirstProgressionS3Cohort = 0;" "S3Cohort default 0")
require_text("${config_h}" "    bool S1For(uint32 guidLow) const;" "S1For declared")
require_text("${config_h}" "    bool S3For(uint32 guidLow) const;" "S3For declared")
require_text("${config_cpp}" "bool PlayerbotAIConfig::S1For(uint32 guidLow) const\n{\n    return ai::r01_cohort::Applies(questFirstProgressionS1Cohort, guidLow);\n}" "S1For on the parsed member")
require_text("${config_cpp}" "bool PlayerbotAIConfig::S3For(uint32 guidLow) const\n{\n    return ai::r01_cohort::Applies(questFirstProgressionS3Cohort, guidLow);\n}" "S3For on the parsed member")
require_text("${config_cpp}" "\n    questFirstProgressionS1Cohort = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.S1Cohort\", 0);" "S1Cohort read")
require_text("${config_cpp}" "\n    questFirstProgressionS3Cohort = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.S3Cohort\", 0);" "S3Cohort read")
count_text("${config_cpp}" "\"AiPlayerbot.QuestFirstProgression.S1Cohort\"" 1 "S1Cohort read once")
count_text("${config_cpp}" "\"AiPlayerbot.QuestFirstProgression.S3Cohort\"" 1 "S3Cohort read once")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.S1Cohort = 0\n" "S1Cohort documented, 0")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.S3Cohort = 0\n" "S3Cohort documented, 0")

# 3. S1 group per bot.
#    ResetOnProgressEvents: every reset request is applied in ApplyQuestRouteReset (rescue, battleground return, level-up,
#    login, quest state) - gated there; the XP stage reset follows the cohort too.
require_text("${ai_cpp}" "    if (!events || !sPlayerbotAIConfig.questFirstProgressionResetOnProgressEvents || !sPlayerbotAIConfig.questFirstProgressionEnabled ||\n        !sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) ||" "reset applied for the S1 cohort only")
require_text("${ai_cpp}" "ProgressResetsStage(sPlayerbotAIConfig.questFirstProgressionResetOnProgressEvents &&\n                        sPlayerbotAIConfig.S1For(bot->GetGUIDLow()), levelUp)" "XP stage reset as before outside the S1 cohort")
count_text("${ai_cpp}" "S1For(" 2 "S1 reads in PlayerbotAI.cpp")
#    FailureClasses: every per-bot read (the [QuestRoute] decision log and the global [PreValidate] line stay on the key).
foreach(gated
    "            if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&\n                UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster())"
    "        if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()))\n            SET_AI_VALUE2(int, \"manual int\", ai::quest_failure_class::TransientStreakValue, 0);"
    "    bool const failureClasses = preferLocalQuest && sPlayerbotAIConfig.questFirstProgressionFailureClasses &&\n        sPlayerbotAIConfig.S1For(bot->GetGUIDLow());"
    "    bool const failureClasses = sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&\n        UsesQuestFirstProgression(bot) && !ai->HasRealPlayerMaster();")
  require_text("${choose_cpp}" "${gated}" "FailureClasses per bot")
endforeach()
count_text("${choose_cpp}" "sPlayerbotAIConfig.questFirstProgressionFailureClasses &&" 4 "every '&&' read of FailureClasses in ChooseTravelTargetAction is one of the gated four")
require_text("${values_cpp}" "if (sPlayerbotAIConfig.questFirstProgressionFailureClasses && sPlayerbotAIConfig.S1For(bot->GetGUIDLow()) &&\n                !ai->HasRealPlayerMaster())" "grind yield per bot")
count_text("${values_cpp}" "sPlayerbotAIConfig.questFirstProgressionFailureClasses" 1 "one FailureClasses read in TravelValues")
#    AbandonStructuralParked: the abandon mark is set in one place only, for the S1 cohort; the other sites read the mark.
require_text("${choose_cpp}" "AbandonParkedTurnIn(sPlayerbotAIConfig.questFirstProgressionAbandonStructuralParked &&\n            sPlayerbotAIConfig.S1For(ai->GetBot()->GetGUIDLow()), abandonDue," "abandon mark for the S1 cohort only")
count_text("${choose_cpp}" "NoteStructuralAbandon(" 1 "one place sets the abandon mark")
require_order("${choose_cpp}" "AbandonParkedTurnIn(sPlayerbotAIConfig.questFirstProgressionAbandonStructuralParked &&" "NoteStructuralAbandon(" "mark only behind the gated decision")

# 4. S3 group per bot.
#    Feasibility: one choke point (QuestFeasibilityReason) for accept, item quests, giver trips and the log drop.
require_text("${ai_cpp}" "    if (!sPlayerbotAIConfig.questFeasibilityEnable || !bot || !quest || !sPlayerbotAIConfig.S3For(bot->GetGUIDLow()))\n        return ai::quest_feasibility::Reason::None;" "feasibility for the S3 cohort only")
require_text("${ai_cpp}" "    ai::quest_feasibility::Reason const reason = QuestFeasibilityReason(quest);" "SkipInfeasibleQuest asks the choke point")
require_text("${drop_cpp}" "ai::quest_feasibility::Reason const why = ai->QuestFeasibilityReason(quest);" "the log drop asks the choke point")
count_text("${ai_cpp}" "S3For(" 1 "S3 reads in PlayerbotAI.cpp")
#    HubPreloopSameFilters
require_text("${choose_cpp}" "    bool const hubPreloopSameFilters = failureClasses && sPlayerbotAIConfig.questFirstProgressionHubPreloopSameFilters &&\n        sPlayerbotAIConfig.S3For(bot->GetGUIDLow());" "hub pre-loop per bot")
#    ZoneExit.TurnInTravelStall (with TurnInNearRadius): every behaviour read; the [QuestTurnInStall] field stays on the key.
#    3a (TurnInTravelStallCountsCombat) adds the fifth read, per bot like the others.
count_text("${travel_cpp}" "sPlayerbotAIConfig.zoneExitTurnInTravelStall &&" 5 "every '&&' read of TurnInTravelStall")
count_text("${travel_cpp}" "sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow())" 5 "each of them per bot")
count_text("${travel_cpp}" "if (sPlayerbotAIConfig.zoneExitTurnInTravelStall)" 0 "no bare TurnInTravelStall branch")

message(STATUS "r01_cohort source contract passed")
