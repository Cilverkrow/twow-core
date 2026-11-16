cmake_policy(VERSION 3.16)
# A3 step 2 H5 (A3-HARM-UPSTREAM H5; OB-00 09.11.2026 one lever, one pair; OB-00 10.11.2026 level gate option A):
# AiPlayerbot.QuestBook.NeedPerObjective (0 off = default, 1 all, 2 cohort B, 3 cohort A; other = 0 + error line),
# .NeedPerObjective.ItemUse (0/1, default 0, only where NeedPerObjective applies) and .NeedPerObjective.Log (0/1, default 0,
# measurement only). Spec ws-10/cli-quest-organic/a3/step2-questbook/h5/H5-SPEC-PREREG-DRAFT.md. This contract pins that:
#   - the switch: member defaults 0 / false / false, keys read with default 0, the mode clamped by quest_need::ValidMode with one
#     error line, documented once each = 0, echoed in byte order between QuestBook.KillSeconds and QuestBook.Shadow;
#   - the level gate: QuestNeedPolicy.h MinLevel = 10 and H5Applies gates on it; PlayerbotAI::QuestNeedApplies reads the switch
#     first, then H5Applies with the bot's level, then InQuestBookScope (the unit test pins L9 off / L10 on in every mode);
#   - (a): NeededQuestEntriesValue keeps the old walk (QuestAllObjective fetch + break) for every bot the part does not apply to,
#     and the new walk fetches once per quest with the open-objective mask; both quest credit actions call QuestNeedOnCredit;
#   - (b): both CanCastSpell returns of CanUseItemOn keep the dice for every other bot and use UseByNeed only where ItemUse
#     applies; the dice line stays where it was (mode 0 = the same calls); UseByNeed refuses without a link and checks the cap;
#   - the hour tick runs only with .Log or .ItemUse on; the [QuestNeed] pick count only with .Log;
#   - QuestNeedPolicy.h names no game header and reads no config / log (comments aside); the unit test is registered.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "questbook need per objective: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "questbook need per objective: PB_MODULE_DIR is required")
endif()

set(TAG "questbook need per objective")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("QuestNeedPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/values/QuestValues.cpp" values_cpp)
read_source("strategy/actions/QuestAction.cpp" quest_action_cpp)
read_source("strategy/actions/RpgSubActions.cpp" rpg_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_target_cpp)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

# returns in `out` the reason for the first needle missing from `text` ("" when all are there)
#   The needles are passed by VARIABLE NAME: a needle passed by value would be split at every ";" (ARGN is a list), so a mutation
#   behind a ";" would still find its pieces elsewhere (negative probe "use without a link").
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

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ECHO "            {\"QuestBook.KillSeconds\", Value(questBookKillSeconds)},
            {\"QuestBook.NeedPerObjective\", Value(questBookNeedPerObjective)},
            {\"QuestBook.NeedPerObjective.ItemUse\", Value(questBookNeedPerObjectiveItemUse)},
            {\"QuestBook.NeedPerObjective.Log\", Value(questBookNeedPerObjectiveLog)},
            {\"QuestBook.Shadow\", Value(questBookShadow)},")

set(S_MEMBER1 "    uint32 questBookNeedPerObjective = 0;\n")
set(S_MEMBER2 "    bool questBookNeedPerObjectiveItemUse = false;\n")
set(S_MEMBER3 "    bool questBookNeedPerObjectiveLog = false;\n")
set(S_LOAD1 "questBookNeedPerObjective = uint32(config.GetIntDefault(\"AiPlayerbot.QuestBook.NeedPerObjective\", 0));
    if (!ai::quest_need::ValidMode(questBookNeedPerObjective))
    {
        sLog.outError(\"[QuestNeed] config key=QuestBook.NeedPerObjective value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", questBookNeedPerObjective);
        questBookNeedPerObjective = 0;
    }")
set(S_LOAD2 "questBookNeedPerObjectiveItemUse = config.GetIntDefault(\"AiPlayerbot.QuestBook.NeedPerObjective.ItemUse\", 0) != 0;")
set(S_LOAD3 "questBookNeedPerObjectiveLog = config.GetIntDefault(\"AiPlayerbot.QuestBook.NeedPerObjective.Log\", 0) != 0;")

function(check_switch config_h config_cpp conf_dist out)
  require_all("${config_h}" "PlayerbotAIConfig.h" r S_MEMBER1 S_MEMBER2 S_MEMBER3)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${config_cpp}" "PlayerbotAIConfig.cpp" r S_LOAD1 S_LOAD2 S_LOAD3 ECHO)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  foreach(key "NeedPerObjective" "NeedPerObjective.ItemUse" "NeedPerObjective.Log")
    string(REPLACE "." "\\." rkey "${key}")
    string(REGEX MATCHALL "\nAiPlayerbot\\.QuestBook\\.${rkey} = [^\n]*" lines "${conf_dist}")
    list(LENGTH lines n)
    if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.QuestBook.${key} = 0")
      set(${out} "conf.dist: AiPlayerbot.QuestBook.${key} must be documented once, = 0" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the level gate ------------------------------------------------------------------------------------------------------
set(GATE_POLICY "constexpr std::uint32_t MinLevel = 10;")
set(GATE_FN "inline bool H5Applies(std::uint32_t mode, std::uint32_t guidLow, std::uint32_t level)
{
    return level >= MinLevel && cohort::Applies(SanitizeMode(mode), guidLow);
}")
set(GATE_AI "bool PlayerbotAI::QuestNeedApplies()
{
    return sPlayerbotAIConfig.questBookNeedPerObjective && bot &&
        ai::quest_need::H5Applies(sPlayerbotAIConfig.questBookNeedPerObjective, bot->GetGUIDLow(), bot->GetLevel()) && InQuestBookScope();
}")
set(GATE_ITEM "bool PlayerbotAI::QuestNeedItemUseApplies()
{
    return sPlayerbotAIConfig.questBookNeedPerObjectiveItemUse && QuestNeedApplies();
}")

function(check_gate policy ai_cpp out)
  require_all("${policy}" "QuestNeedPolicy.h" r GATE_POLICY GATE_FN)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${ai_cpp}" "PlayerbotAI.cpp" r GATE_AI GATE_ITEM)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- (a) the need set ----------------------------------------------------------------------------------------------------
set(A_FLAG "	bool const perObjective = ai->QuestNeedApplies();")
set(A_NEW "		if (perObjective)
		{
			// one fetch with the bits of the open objectives (the same predicate as below, asked for every objective)
			uint32 openMask = 0;
			for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
			{
				std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };
				if (AI_VALUE2(bool, \"need quest objective\", Qualified::MultiQualify(qualifier, \",\")))
					openMask |= ai::quest_need::ObjectiveBit(objective);
			}

			if (!openMask)
				continue;

			DestinationList destinations = sTravelMgr.GetDestinations(info, openMask, {int32(questId)}, false, 0);

			for (auto destination : destinations)
				needed.push_back(destination->GetEntry());

			continue;
		}
")
set(A_OLD "			// The destinations are those of the whole quest (all objectives): one walk per quest is enough.
			DestinationList destinations = sTravelMgr.GetDestinations(info, uint32(TravelDestinationPurpose::QuestAllObjective), {int32(questId)}, false, 0);

			for (auto destination : destinations)
				needed.push_back(destination->GetEntry());

			break;")
set(A_KILL "    ai->QuestNeedOnCredit(questId);
    ai->QuestNeedOnKillCredit(entry);")
set(A_ITEM "            ai->QuestNeedOnCredit(pair.first->GetQuestId());")

function(check_need values_cpp quest_action_cpp ai_cpp out)
  require_all("${values_cpp}" "QuestValues.cpp" r A_FLAG A_NEW A_OLD)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # the new walk comes before the old one inside the quest loop
  string(FIND "${values_cpp}" "${A_NEW}" at_new)
  string(FIND "${values_cpp}" "${A_OLD}" at_old)
  if(NOT at_new LESS at_old)
    set(${out} "QuestValues.cpp: the per-objective walk must come before the old walk" PARENT_SCOPE)
    return()
  endif()
  require_all("${quest_action_cpp}" "QuestAction.cpp" r A_KILL A_ITEM)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${ai_cpp}" "PlayerbotAI.cpp" r A_CREDIT_FN A_CREDIT_SET)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()
set(A_CREDIT_FN "void PlayerbotAI::QuestNeedOnCredit(uint32 questId)
{
    if (!QuestNeedApplies())
        return;")
set(A_CREDIT_SET "aiObjectContext->GetValue<std::vector<int32>>(\"needed quest entries\")")

# ---- (b) item use --------------------------------------------------------------------------------------------------------
set(B_DICE "	bool tryRandomCast = !urand(0, 20);")
set(B_UNIT "			if (ai->CanCastSpell(spellId, unit, 0, false, nullptr, true))
				return ai->QuestNeedItemUseApplies() ? UseByNeed(itemId, spellId, guidP) : tryRandomCast;")
set(B_GO "			if (ai->CanCastSpell(spellId, gameObject, 0, false, true))
				return ai->QuestNeedItemUseApplies() ? UseByNeed(itemId, spellId, guidP) : tryRandomCast;")
set(B_NOLINK "	if (!linked)
	{
		if (state)
			state->stats.refusedNolink++;
		return false;
	}")
set(B_CAP "		if (state->cap.Check(itemId, guidP.GetRawValue(), WorldTimer::getMSTime()) != ai::quest_need::CapVerdict::Allowed)")
set(B_LINK "linked = ai::quest_need::Link(link) != ai::quest_need::LinkKind::None;")
set(B_USE "            if (used)
                ai->QuestNeedOnItemUse(item->GetEntry(), guidP.GetRawValue(), ai::quest_need::CreditKey(guidP.GetEntry(), guidP.IsGameObject()));")

function(check_item values_cpp rpg_cpp out)
  require_all("${values_cpp}" "QuestValues.cpp" r B_DICE B_UNIT B_GO B_NOLINK B_CAP B_LINK)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # the dice is drawn before the spell checks, as before (mode 0 = the same calls)
  string(FIND "${values_cpp}" "${B_DICE}" at_dice)
  string(FIND "${values_cpp}" "${B_UNIT}" at_unit)
  if(NOT at_dice LESS at_unit)
    set(${out} "QuestValues.cpp: the dice line must stay before the spell checks" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "return tryRandomCast;" bare "${values_cpp}")
  list(LENGTH bare n)
  if(NOT n EQUAL 0)
    set(${out} "QuestValues.cpp: a bare dice return bypasses ItemUse" PARENT_SCOPE)
    return()
  endif()
  require_all("${rpg_cpp}" "RpgSubActions.cpp" r B_USE)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- measurement gates ---------------------------------------------------------------------------------------------------
set(M_TICK "    if (sPlayerbotAIConfig.questBookNeedPerObjectiveLog || sPlayerbotAIConfig.questBookNeedPerObjectiveItemUse)
        QuestNeedHourTick(uint32(time(nullptr)));")
set(M_PICK "    if (sPlayerbotAIConfig.questBookNeedPerObjectiveLog)
    {
        needTarget = GetTarget();")
set(M_COUNT "    bool result = AttackAction::Execute(event);
    if (needLog && needPick && result && GetTarget() == needTarget)
    {
        needLog->stats.picksQuest++;")
set(M_WRITE "    if (!questNeed_ || !hourStart || !sPlayerbotAIConfig.questBookNeedPerObjectiveLog)
        return;")

function(check_measure ai_cpp choose_target_cpp out)
  require_all("${ai_cpp}" "PlayerbotAI.cpp" r M_TICK M_WRITE)
  if(r)
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  require_all("${choose_target_cpp}" "ChooseTargetActions.cpp" r M_PICK M_COUNT)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- purity and the unit test ----------------------------------------------------------------------------------------------
set(T_EXE "add_executable(questneed_policy_tests
  \"\${PB_MODULE_DIR}/t/questneed_policy_tests.cpp\")")
set(T_ADD "add_test(NAME questneed_policy
  COMMAND questneed_policy_tests")

function(check_pure policy tests_cmake out)
  # comments out, then no game header, no config, no log
  string(REGEX REPLACE "//[^\n]*" "" code "${policy}")
  string(REGEX MATCHALL "#include \"[^\"]*\"" includes "${code}")
  foreach(inc IN LISTS includes)
    if(NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      set(${out} "QuestNeedPolicy.h: game include ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(bad "sPlayerbotAIConfig" "sLog" "sObjectMgr" "Player*" "WorldTimer")
    string(FIND "${code}" "${bad}" at)
    if(NOT at EQUAL -1)
      set(${out} "QuestNeedPolicy.h: names ${bad}" PARENT_SCOPE)
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
check_gate("${policy}" "${ai_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_need("${values_cpp}" "${quest_action_cpp}" "${ai_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_item("${values_cpp}" "${rpg_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_measure("${ai_cpp}" "${choose_target_cpp}" r)
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

string(REPLACE "uint32 questBookNeedPerObjective = 0;" "uint32 questBookNeedPerObjective = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestBook.NeedPerObjective.ItemUse\", 0)" "\"AiPlayerbot.QuestBook.NeedPerObjective.ItemUse\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "ItemUse loader default 1")

string(REPLACE "if (!ai::quest_need::ValidMode(questBookNeedPerObjective))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "            {\"QuestBook.NeedPerObjective.Log\", Value(questBookNeedPerObjectiveLog)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "Log not in [ConfigEcho]")

string(REPLACE "\nAiPlayerbot.QuestBook.NeedPerObjective.ItemUse = 0\n" "\nAiPlayerbot.QuestBook.NeedPerObjective.ItemUse = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "dist ItemUse = 1")

string(REPLACE "constexpr std::uint32_t MinLevel = 10;" "constexpr std::uint32_t MinLevel = 1;" m "${policy}")
check_gate("${m}" "${ai_cpp}" r)
expect_caught("${r}" "level gate 1")

string(REPLACE "    return level >= MinLevel && cohort::Applies(SanitizeMode(mode), guidLow);" "    return cohort::Applies(SanitizeMode(mode), guidLow);" m "${policy}")
check_gate("${m}" "${ai_cpp}" r)
expect_caught("${r}" "level gate dropped")

string(REPLACE "bot->GetLevel()) && InQuestBookScope();\n}" "bot->GetLevel());\n}" m "${ai_cpp}")
check_gate("${policy}" "${m}" r)
expect_caught("${r}" "scope dropped")

string(REPLACE "    return sPlayerbotAIConfig.questBookNeedPerObjectiveItemUse && QuestNeedApplies();" "    return sPlayerbotAIConfig.questBookNeedPerObjectiveItemUse;" m "${ai_cpp}")
check_gate("${policy}" "${m}" r)
expect_caught("${r}" "ItemUse without the parent switch")

string(REPLACE "openMask |= ai::quest_need::ObjectiveBit(objective);" "openMask |= uint32(TravelDestinationPurpose::QuestAllObjective);" m "${values_cpp}")
check_need("${m}" "${quest_action_cpp}" "${ai_cpp}" r)
expect_caught("${r}" "the whole quest's mask in the new walk")

string(REPLACE "uint32(TravelDestinationPurpose::QuestAllObjective), {int32(questId)}" "openMask, {int32(questId)}" m "${values_cpp}")
check_need("${m}" "${quest_action_cpp}" "${ai_cpp}" r)
expect_caught("${r}" "the old walk changed")

string(REPLACE "    ai->QuestNeedOnCredit(questId);\n" "" m "${quest_action_cpp}")
check_need("${values_cpp}" "${m}" "${ai_cpp}" r)
expect_caught("${r}" "no refresh at a kill credit")

string(REPLACE "    if (!QuestNeedApplies())\n        return;\n    // the objective values" "    // the objective values" m "${ai_cpp}")
check_need("${values_cpp}" "${quest_action_cpp}" "${m}" r)
expect_caught("${r}" "refresh without the switch")

string(REPLACE "${B_UNIT}" "			if (ai->CanCastSpell(spellId, unit, 0, false, nullptr, true))\n				return tryRandomCast;" m "${values_cpp}")
check_item("${m}" "${rpg_cpp}" r)
expect_caught("${r}" "dice kept for the treated bot")

string(REPLACE "${B_DICE}\n" "" m "${values_cpp}")
string(REPLACE "${B_UNIT}" "${B_UNIT}\n${B_DICE}" m "${m}")
check_item("${m}" "${rpg_cpp}" r)
expect_caught("${r}" "dice drawn after the spell checks")

string(REPLACE "		if (state)\n			state->stats.refusedNolink++;\n		return false;" "		if (state)\n			state->stats.refusedNolink++;\n		return true;" m "${values_cpp}")
check_item("${m}" "${rpg_cpp}" r)
expect_caught("${r}" "use without a link")

string(REPLACE "${B_CAP}" "		if (false)" m "${values_cpp}")
check_item("${m}" "${rpg_cpp}" r)
expect_caught("${r}" "cap not checked")

string(REPLACE "            if (used)\n                ai->QuestNeedOnItemUse(" "            if (false)\n                ai->QuestNeedOnItemUse(" m "${rpg_cpp}")
check_item("${values_cpp}" "${m}" r)
expect_caught("${r}" "item use not recorded")

string(REPLACE "${M_TICK}" "    if (true)\n        QuestNeedHourTick(uint32(time(nullptr)));" m "${ai_cpp}")
check_measure("${m}" "${choose_target_cpp}" r)
expect_caught("${r}" "hour tick without the switches")

string(REPLACE "    if (sPlayerbotAIConfig.questBookNeedPerObjectiveLog)\n    {\n        needTarget = GetTarget();" "    {\n        needTarget = GetTarget();" m "${choose_target_cpp}")
check_measure("${ai_cpp}" "${m}" r)
expect_caught("${r}" "pick count without .Log")

string(REPLACE "if (needLog && needPick && result && GetTarget() == needTarget)" "if (needLog && needPick)" m "${choose_target_cpp}")
check_measure("${ai_cpp}" "${m}" r)
expect_caught("${r}" "pick counted without the engagement")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" "${tests_cmake}" r)
expect_caught("${r}" "game include in the policy")

check_pure("${policy}\nstd::uint32_t x = sPlayerbotAIConfig.questBookNeedPerObjective;" "${tests_cmake}" r)
expect_caught("${r}" "config read in the policy")

string(REPLACE "add_test(NAME questneed_policy\n" "add_test(NAME questneed_policy_off\n" m "${tests_cmake}")
check_pure("${policy}" "${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
