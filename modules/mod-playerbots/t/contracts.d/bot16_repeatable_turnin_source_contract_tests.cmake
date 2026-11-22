cmake_policy(VERSION 3.16)
# twow-repo#716 BOT-16 (bug fix, CLI-Deep 16.11.2026): AiPlayerbot.QuestTurnIn.RepeatableAutoComplete (cohort mode 0 off = default,
# 1 all, 2 cohort B, 3 cohort A; other = 0 + error line; 1 after the acceptance unit, 0 = rollback). A complete quest that is
# auto-complete AND repeatable marks its taker only DIALOG_STATUS_REWARD_REP, and "can turn in quest npc" took only the turn-in mark,
# so the Blood Shard quests (889, 5042-5046) never got a taker route. This contract pins that:
#   - the switch: member default 0, key read with default 0 and clamped by repeatable_turnin::ValidMode with one error line, documented
#     once = 0, echoed in byte order between QuestRescue.NoTargetFreeSlot and Rescue.RepopDeadOnly;
#   - the value: CanTurnInQuestValue keeps the dialog status rule (both builds) and returns it unchanged where the switch does not
#     apply; otherwise it walks only the taker's INVOLVED relations (creature, game object = entry * -1), fills the held quest from
#     the bot's own quest map and the template, and needs HeldForTurnIn AND the core's CanRewardQuest;
#   - the policy: HeldForTurnIn needs active, complete, unrewarded, auto-complete and repeatable; Applies goes through ValidMode;
#   - the turn-in path the fix relies on: the rpg end quest trigger reads the value, "talk to quest giver" turns in a COMPLETE quest,
#     TurnInQuest stops only on GetQuestRewardStatus (false for a repeatable quest), RewardNoItem rewards after CanRewardQuest;
#   - RepeatableTurnInPolicy.h names no game header and reads no config / log (comments aside); the unit test is registered.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "bot16 repeatable turnin: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "bot16 repeatable turnin: PB_MODULE_DIR is required")
endif()

set(TAG "bot16 repeatable turnin")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("RepeatableTurnInPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/QuestValues.h" values_h)
read_source("strategy/values/QuestValues.cpp" values_cpp)
read_source("strategy/triggers/RpgTriggers.cpp" rpg_triggers_cpp)
read_source("strategy/actions/TalkToQuestGiverAction.cpp" talk_cpp)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

# returns in `out` the reason for the first needle missing from `text` ("" when all are there)
#   The needles are passed by VARIABLE NAME: a needle passed by value would be split at every ";" (ARGN is a list).
function(require_all text where out)
  foreach(var IN LISTS ARGN)
    string(FIND "${text}" "${${var}}" at)
    if(at EQUAL -1)
      set(${out} "${where}: missing ${var}: ${${var}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# returns in `out` the reason for the first needle (by variable name) that IS in `text`
function(forbid_all text where out)
  foreach(var IN LISTS ARGN)
    string(FIND "${text}" "${${var}}" at)
    if(NOT at EQUAL -1)
      set(${out} "${where}: must not contain ${var}: ${${var}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ECHO "            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},
            {\"QuestRescue.StartValleyFix\", Value(questRescueStartValleyFix)},
            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)},
            {\"Replan\", Value(replan)},
            {\"Replan.Shadow\", Value(replanShadow)},
            {\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)},")   # R9-2 (twow-repo#575) added Replan / Replan.Shadow; R2 (twow-repo#575, 22.11.2026) StartValleyFix
set(S_MEMBER "    uint32 questTurnInRepeatableAutoComplete = 0;\n")
set(S_INCLUDE "#include \"playerbot/RepeatableTurnInPolicy.h\"")
set(S_LOAD "    questTurnInRepeatableAutoComplete = uint32(config.GetIntDefault(\"AiPlayerbot.QuestTurnIn.RepeatableAutoComplete\", 0));
    if (!ai::repeatable_turnin::ValidMode(questTurnInRepeatableAutoComplete))
    {
        sLog.outError(\"[QuestTurnIn] config key=RepeatableAutoComplete value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", questTurnInRepeatableAutoComplete);
        questTurnInRepeatableAutoComplete = 0;
    }")

function(check_switch config_h config_cpp conf_dist out)
  require_all("${config_h}" "PlayerbotAIConfig.h" r S_MEMBER)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${config_cpp}" "PlayerbotAIConfig.cpp" r S_INCLUDE S_LOAD ECHO)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "\nAiPlayerbot\\.QuestTurnIn\\.RepeatableAutoComplete *=[^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 0")
    set(${out} "conf.dist: AiPlayerbot.QuestTurnIn.RepeatableAutoComplete must be documented once, = 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the value -----------------------------------------------------------------------------------------------------------
set(V_DECL "        CanTurnInQuestValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, \"can turn in quest npc\"), Qualified() {}

        // twow-repo#716 BOT-16: QuestValues.cpp - the dialog status rule, plus (AiPlayerbot.QuestTurnIn.RepeatableAutoComplete,
        // RepeatableTurnInPolicy.h) a held repeatable auto-complete quest of this taker.
        virtual bool Calculate() override;
    };")
set(V_INCLUDE "#include \"playerbot/RepeatableTurnInPolicy.h\"")
set(V_HEAD "bool CanTurnInQuestValue::Calculate()
{
	uint32 dialogStatus = AI_VALUE2(uint32, \"dialog status\", getQualifier());
#ifdef MANGOSBOT_ZERO
	bool const byDialogStatus = dialogStatus == DIALOG_STATUS_REWARD2;
#else
	bool const byDialogStatus = dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD;
#endif")
set(V_GATE "	if (byDialogStatus || !ai::repeatable_turnin::Applies(sPlayerbotAIConfig.questTurnInRepeatableAutoComplete, bot->GetGUIDLow()))
		return byDialogStatus;")
set(V_BOUNDS "	int32 const questgiver = stoi(getQualifier());
	QuestRelationsMapBounds const irbounds = questgiver > 0 ? sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(questgiver) :
		sObjectMgr.GetGOQuestInvolvedRelationsMapBounds(questgiver * -1);
	for (QuestRelationsMap::const_iterator itr = irbounds.first; itr != irbounds.second; ++itr)")
set(V_FIELDS "		held.active = quest->IsActive();
		held.complete = bot->GetQuestStatus(itr->second) == QUEST_STATUS_COMPLETE;
		held.rewarded = bot->GetQuestRewardStatus(itr->second);
		held.autoComplete = quest->IsAutoComplete();
		held.repeatable = quest->IsRepeatable();")
set(V_HELD "		if (ai::repeatable_turnin::HeldForTurnIn(held) && bot->CanRewardQuest(quest, false))
			return true;
	}

	return false;
}")
# the giver relations (take-again marks) and the old inline rule must not come back into the value
set(V_NO_GIVER "GetCreatureQuestRelationsMapBounds")
set(V_NO_GO_GIVER "GetGOQuestRelationsMapBounds")
set(V_NO_INLINE "            return dialogStatus == DIALOG_STATUS_REWARD2;")

function(check_value values_h values_cpp out)
  require_all("${values_h}" "QuestValues.h" r V_DECL)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  forbid_all("${values_h}" "QuestValues.h" r V_NO_INLINE)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${values_cpp}" "${V_HEAD}" at)
  if(at EQUAL -1)
    set(${out} "QuestValues.cpp: missing V_HEAD: ${V_HEAD}" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${values_cpp}" ${at} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    set(${out} "QuestValues.cpp: CanTurnInQuestValue::Calculate has no end" PARENT_SCOPE)
    return()
  endif()
  math(EXPR end "${end} + 3")
  string(SUBSTRING "${rest}" 0 ${end} body)
  require_all("${values_cpp}" "QuestValues.cpp" r V_INCLUDE)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${body}" "CanTurnInQuestValue::Calculate" r V_GATE V_BOUNDS V_FIELDS V_HELD)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  forbid_all("${body}" "CanTurnInQuestValue::Calculate" r V_NO_GIVER V_NO_GO_GIVER)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # order: the dialog status rule, then the gate, then the relation walk
  string(FIND "${body}" "${V_GATE}" gate_at)
  string(FIND "${body}" "${V_BOUNDS}" bounds_at)
  if(NOT gate_at LESS bounds_at)
    set(${out} "CanTurnInQuestValue::Calculate: the relation walk runs before the switch gate" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the policy ----------------------------------------------------------------------------------------------------------
set(P_VALID "inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}")
set(P_APPLIES "inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ValidMode(mode) && cohort::Applies(mode, guidLow);
}")
set(P_HELD "inline bool HeldForTurnIn(EnderQuest const& quest)
{
    return quest.active && quest.complete && !quest.rewarded && quest.autoComplete && quest.repeatable;
}")

function(check_policy policy out)
  require_all("${policy}" "RepeatableTurnInPolicy.h" r P_VALID P_APPLIES P_HELD)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- the turn-in path the fix relies on ----------------------------------------------------------------------------------
set(T_TRIGGER "    int32 entry = guidP.IsCreature() ? guidP.GetEntry() : -1 * (int32)guidP.GetEntry();

    if (AI_VALUE2(bool, \"can turn in quest npc\", entry))
        return true;")
set(T_COMPLETE "        case QUEST_STATUS_COMPLETE:
        {
            isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);
            break;
        }")
set(T_REWARDED "    uint32 questID = quest->GetQuestId();
    if (bot->GetQuestRewardStatus(questID))
    {
        return false;
    }")
set(T_NOITEM "    if (bot->CanRewardQuest(quest, false))
    {
        bot->RewardQuest(quest, 0, questGiver, false);")

function(check_path rpg_triggers_cpp talk_cpp out)
  require_all("${rpg_triggers_cpp}" "RpgTriggers.cpp" r T_TRIGGER)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${talk_cpp}" "TalkToQuestGiverAction.cpp" r T_COMPLETE T_REWARDED T_NOITEM)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- purity + registration -----------------------------------------------------------------------------------------------
set(T_EXE "add_executable(repeatable_turnin_policy_tests
  \"\${PB_MODULE_DIR}/t/repeatable_turnin_policy_tests.cpp\")")
set(T_ADD "add_test(NAME repeatable_turnin_policy
  COMMAND repeatable_turnin_policy_tests")

function(check_pure policy tests_cmake out)
  # comments out, then no game header, no config, no log
  string(REGEX REPLACE "//[^\n]*" "" code "${policy}")
  string(REGEX MATCHALL "#include \"[^\"]*\"" includes "${code}")
  foreach(inc IN LISTS includes)
    if(NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      set(${out} "RepeatableTurnInPolicy.h: game include ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(bad "sPlayerbotAIConfig" "sLog" "sObjectMgr" "Player*" "Quest const*")
    string(FIND "${code}" "${bad}" at)
    if(NOT at EQUAL -1)
      set(${out} "RepeatableTurnInPolicy.h: names ${bad}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  require_all("${tests_cmake}" "tests.cmake" r T_EXE T_ADD)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- run -----------------------------------------------------------------------------------------------------------------
set(failures "")
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_value("${values_h}" "${values_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_policy("${policy}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_path("${rpg_triggers_cpp}" "${talk_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_pure("${policy}" "${tests_cmake}" r)
if(r)
  list(APPEND failures "${r}")
endif()
if(failures)
  string(REPLACE ";" "\n  " text "${failures}")
  message(FATAL_ERROR "${TAG}:\n  ${text}")
endif()

# ---- negative probes -----------------------------------------------------------------------------------------------------
function(expect_caught reason what)
  if(reason STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

string(REPLACE "uint32 questTurnInRepeatableAutoComplete = 0;" "uint32 questTurnInRepeatableAutoComplete = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestTurnIn.RepeatableAutoComplete\", 0)" "\"AiPlayerbot.QuestTurnIn.RepeatableAutoComplete\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::repeatable_turnin::ValidMode(questTurnInRepeatableAutoComplete))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)},\n" "" m "${config_cpp}")
string(REPLACE "            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},\n" "            {\"QuestRescue.NoTargetFreeSlot\", Value(questRescueNoTargetFreeSlot)},\n            {\"Rescue.RepopDeadOnly\", Value(rescueRepopDeadOnly)},\n            {\"QuestTurnIn.RepeatableAutoComplete\", Value(questTurnInRepeatableAutoComplete)},\n" m "${m}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "echo out of byte order")

string(REPLACE "\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 0\n" "\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "dist = 1")

string(REPLACE "\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 0\n" "\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 0\nAiPlayerbot.QuestTurnIn.RepeatableAutoComplete = 0\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "dist documented twice")

string(REPLACE "${V_GATE}" "	if (byDialogStatus)\n		return byDialogStatus;" m "${values_cpp}")
check_value("${values_h}" "${m}" r)
expect_caught("${r}" "the walk without the switch")

string(REPLACE "	bool const byDialogStatus = dialogStatus == DIALOG_STATUS_REWARD2;\n#else" "	bool const byDialogStatus = dialogStatus == DIALOG_STATUS_REWARD2 || dialogStatus == DIALOG_STATUS_REWARD_REP;\n#else" m "${values_cpp}")
check_value("${values_h}" "${m}" r)
expect_caught("${r}" "REWARD_REP alone counted as a turn-in (take-again marks)")

string(REPLACE "sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(questgiver) :" "sObjectMgr.GetCreatureQuestRelationsMapBounds(questgiver) :" m "${values_cpp}")
check_value("${values_h}" "${m}" r)
expect_caught("${r}" "giver relations instead of the involved ones")

string(REPLACE "		if (ai::repeatable_turnin::HeldForTurnIn(held) && bot->CanRewardQuest(quest, false))" "		if (ai::repeatable_turnin::HeldForTurnIn(held))" m "${values_cpp}")
check_value("${values_h}" "${m}" r)
expect_caught("${r}" "no CanRewardQuest")

string(REPLACE "		held.complete = bot->GetQuestStatus(itr->second) == QUEST_STATUS_COMPLETE;" "		held.complete = true;" m "${values_cpp}")
check_value("${values_h}" "${m}" r)
expect_caught("${r}" "complete not read from the bot's quest map")

string(REPLACE "${V_DECL}" "        CanTurnInQuestValue(PlayerbotAI* ai) : BoolCalculatedValue(ai, \"can turn in quest npc\"), Qualified() {}\n        virtual bool Calculate() override\n        {\n            uint32 dialogStatus = AI_VALUE2(uint32, \"dialog status\", getQualifier());\n            return dialogStatus == DIALOG_STATUS_REWARD2;\n        };\n    };" m "${values_h}")
check_value("${m}" "${values_cpp}" r)
expect_caught("${r}" "the old inline rule back in the header")

string(REPLACE "    return quest.active && quest.complete && !quest.rewarded && quest.autoComplete && quest.repeatable;" "    return quest.active && quest.complete && !quest.rewarded && quest.autoComplete;" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "policy without repeatable")

string(REPLACE "    return quest.active && quest.complete && !quest.rewarded && quest.autoComplete && quest.repeatable;" "    return quest.active && !quest.rewarded && quest.autoComplete && quest.repeatable;" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "policy without complete (take-again cases)")

string(REPLACE "    return ValidMode(mode) && cohort::Applies(mode, guidLow);" "    return cohort::Applies(mode, guidLow);" m "${policy}")
check_policy("${m}" r)
expect_caught("${r}" "Applies without ValidMode")

string(REPLACE "            isCompleted |= TurnInQuest(requester, quest, questGiver, outputMessage);\n            break;" "            break;" m "${talk_cpp}")
check_path("${rpg_triggers_cpp}" "${m}" r)
expect_caught("${r}" "COMPLETE quest no longer turned in")

string(REPLACE "    if (AI_VALUE2(bool, \"can turn in quest npc\", entry))\n        return true;" "    if (false)\n        return true;" m "${rpg_triggers_cpp}")
check_path("${m}" "${talk_cpp}" r)
expect_caught("${r}" "rpg end quest trigger without the value")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" "${tests_cmake}" r)
expect_caught("${r}" "game include in the policy")

check_pure("${policy}\nstd::uint32_t x = sPlayerbotAIConfig.questTurnInRepeatableAutoComplete;" "${tests_cmake}" r)
expect_caught("${r}" "config read in the policy")

string(REPLACE "add_test(NAME repeatable_turnin_policy\n" "add_test(NAME repeatable_turnin_policy_off\n" m "${tests_cmake}")
check_pure("${policy}" "${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
