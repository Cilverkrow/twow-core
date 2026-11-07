cmake_policy(VERSION 3.16)
# A3 step 2 (ADR-0041, OB-00 07.11.2026): the quest book core QB-K, AiPlayerbot.QuestBook (0 off = default, 1 all, 2 cohort B,
# 3 cohort A; other = 0 + error line) and AiPlayerbot.QuestBook.Shadow (default 0). Plan
# ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md. This contract pins that:
#   - the switch: members default 0 / false, keys read with default 0 and clamped by questbook::ValidMode, documented = 0,
#     echoed in byte order between ProfessionUse.Trace and QuestFeasibility.Blocklist.count;
#   - one observe per engine pass: PlayerbotAI::UpdateAI after the reaction gate, right before the supervisor gate, for a roster
#     bot on its own only; the else branch releases and drops the instance; one place makes a QuestBook; a shadow book is never
#     handed to a caller (ActiveQuestBook is null in shadow);
#   - the K sites (K1 accept, K2 giver value + giver travel, K3 objective travel, K4 progress, K5 turn-in yield) ask
#     ActiveQuestBook() and keep the old rule in the else path; the async travel search never calls ActiveQuestBook (only the
#     PlayerTravelInfo constructor on the bot thread does); ActiveQuestBook() has exactly the listed callers;
#   - OnCredit only from the three quest packet actions (kill, item, complete);
#   - QuestBookPolicy.h names no game header and reads no config (comments aside);
#   - the line formats [QuestBook] state=left_log / complete_stale and [QuestBookMin], the minute report wired once and silent
#     with switch and shadow off, and the unit test registered.
# Pure rules: t/questbook_policy_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at the
# end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "questbook core: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "questbook core: PB_MODULE_DIR is required")
endif()

set(TAG "questbook core")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("QuestBookPolicy.h" policy)
read_source("QuestBook.h" qb_h)
read_source("QuestBook.cpp" qb_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/actions/AcceptQuestAction.cpp" accept_cpp)
read_source("strategy/actions/DropQuestAction.cpp" drop_cpp)
read_source("strategy/actions/QuestAction.cpp" quest_action_cpp)
read_source("strategy/values/QuestValues.cpp" quest_values_cpp)
read_source("strategy/values/TravelValues.cpp" travel_values_cpp)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

# All module sources, for the call-site counts.
file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(APPEND all_sources "${t}\n")
endforeach()

function(count_regex text regex out)
  string(REGEX MATCHALL "${regex}" hits "${text}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ECHO "            {\"ProfessionUse.Trace\", Value(professionUseTrace)},
            {\"QuestBook\", Value(questBook)},
            {\"QuestBook.AboveLevels\", Value(questBookAboveLevels)},
            {\"QuestBook.Exclude.count\", Value(uint32(questBookExclude.size()))},
            {\"QuestBook.Shadow\", Value(questBookShadow)},
            {\"QuestBook.StaleCompleteSeconds\", Value(questBookStaleCompleteSeconds)},
            {\"QuestFeasibility.Blocklist.count\"")

function(check_switch config_h config_cpp conf_dist out)
  foreach(needle "uint32 questBook = 0;" "bool questBookShadow = false;")
    string(FIND "${config_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.h: member not off by default: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "questBook = uint32(config.GetIntDefault(\"AiPlayerbot.QuestBook\", 0));"
      "if (!ai::questbook::ValidMode(questBook))"
      "        questBook = 0;"
      "questBookShadow = config.GetIntDefault(\"AiPlayerbot.QuestBook.Shadow\", 0) != 0;"
      "${ECHO}")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(key "QuestBook" "QuestBook\\.Shadow")
    string(REGEX MATCHALL "\nAiPlayerbot\\.${key} = [^\n]*" lines "${conf_dist}")
    list(LENGTH lines n)
    if(NOT n EQUAL 1)
      set(${out} "conf.dist: AiPlayerbot.${key} documented ${n} times (1 expected)" PARENT_SCOPE)
      return()
    endif()
    if(NOT lines MATCHES "^\nAiPlayerbot\\.${key} = 0$")
      set(${out} "conf.dist: AiPlayerbot.${key} is not 0" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- one observe per engine pass -----------------------------------------------------------------------------------------
set(QB_GATE "        {
            bool const active = sPlayerbotAIConfig.questBook && ai::questbook::Applies(sPlayerbotAIConfig.questBook, bot->GetGUIDLow());
            if ((active || sPlayerbotAIConfig.questBookShadow) && InQuestBookScope())
                EnsureQuestBook(!active)->Observe(WorldTimer::getMSTime());
            else if (questBook_)
            {
                questBook_->Release();
                questBook_.reset();
            }
        }

        // A3 step 3 (ADR-0041, AiPlayerbot.Supervisor, default 0)")
set(REACTION "if(!UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()) && CanUpdateAIInternal())")
set(SCOPE "return bot && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !HasRealPlayerMaster();")
set(ACTIVE "return questBook_ && !questBook_->Shadow() ? questBook_.get() : nullptr;")

function(check_gate ai_cpp ai_h all_sources out)
  string(FIND "${ai_cpp}" "${QB_GATE}" gate)
  if(gate EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the gated observe with the release branch right before the supervisor gate is missing" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${ai_cpp}" "${REACTION}" reaction)
  if(reaction EQUAL -1 OR NOT reaction LESS gate)
    set(${out} "PlayerbotAI.cpp: the observe is not inside the engine pass (after the reaction gate)" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "${SCOPE}" "${ACTIVE}")
    string(FIND "${ai_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAI.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_regex("${all_sources}" "->Observe\\(WorldTimer" n)
  if(NOT n EQUAL 1)
    set(${out} "the quest book is observed in ${n} places (1 expected)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "make_unique<ai::questbook::QuestBook>" n)
  if(NOT n EQUAL 1)
    set(${out} "a QuestBook is made in ${n} places (1 expected: EnsureQuestBook)" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "namespace ai::questbook { class QuestBook; }" "std::unique_ptr<ai::questbook::QuestBook> questBook_;"
      "ai::questbook::QuestBook* ActiveQuestBook() const;")
    string(FIND "${ai_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAI.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the K sites: book first, old rule in the else path -------------------------------------------------------------------
set(K1 "    if (rosterOnItsOwn)
        if (ai::questbook::QuestBook* book = ai->ActiveQuestBook())
            return !book->Accept(quest, freeSlots).IsYes();
    return ai::quest_accept::SkipForRosterBot(rosterOnItsOwn, bot->GetLevel(), bot->GetQuestLevelForPlayer(quest),")
set(K4 "    if (PlayerbotAI* botAi = GetBotAI(bot))
        if (ai::questbook::QuestBook* book = botAi->ActiveQuestBook())
            return book->HasProgress(quest);

    uint32 questId = quest->GetQuestId();")
set(K2V_BOOK "                    if (quest && questBook)
                    {
                        if (ai::questbook::Excluded(questId) ||
                            ai::questbook::TemplateWindow(quest, level, MaNGOS::XP::GetGrayLevel(level)) != ai::questbook::NoReason::None)
                            continue;
                    }
                    else if (quest && (level < quest->GetMinLevel() ||")
set(K2V_BIT "    bool const questBook = ai->ActiveQuestBook() != nullptr;")
set(K2T_BOOK "        if (!forceThisQuest && info.HasQuestBook())")
set(K2T_OLD "        else if (!forceThisQuest && (int32)quest->GetQuestLevel() >= (int32)info.GetLevel() + (int32)5)
            return refuse(ai::quest_search::GiverDropQuestLevel);")
set(K3_BOOK "        if (info.HasQuestBook())
        {
            if (ai::questbook::Excluded(GetQuestId()))")
set(K3_OLD "        else if ((int32)GetQuestTemplate()->GetQuestLevel() > (int32)info.GetLevel() + (int32)1)
            return false;")
set(TRAVEL_BIT "    questBook = ai->ActiveQuestBook() != nullptr;")
# K5: its own block right before S1's (which its contracts pin unchanged), yielding with the count.
set(K5 "            if (ai->ActiveQuestBook() && !ai->HasRealPlayerMaster())
            {")
set(K5_YIELD "                if (ai::quest_failure_class::GrindYieldsToTurnIn(true, routableOpen, routeBackoffRunning))
                {
                    ai::questbook::Note(bot->GetGUIDLow(), ai::questbook::Count::TurnInYield);
                    return false;
                }
            }

            // twow-repo#601 S1 (AiPlayerbot.QuestFirstProgression.FailureClasses, OB-10 answer 3)")

function(check_sites accept drop values travel_cpp travel_values all_sources out)
  foreach(pair "AcceptQuestAction.cpp|accept|K1" "DropQuestAction.cpp|drop|K4" "QuestValues.cpp|values|K2V_BOOK"
      "QuestValues.cpp|values|K2V_BIT" "TravelMgr.cpp|travel_cpp|K2T_BOOK" "TravelMgr.cpp|travel_cpp|K2T_OLD"
      "TravelMgr.cpp|travel_cpp|K3_BOOK" "TravelMgr.cpp|travel_cpp|K3_OLD" "TravelMgr.cpp|travel_cpp|TRAVEL_BIT"
      "TravelValues.cpp|travel_values|K5" "TravelValues.cpp|travel_values|K5_YIELD")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 file)
    list(GET parts 1 var)
    list(GET parts 2 needle)
    string(FIND "${${var}}" "${${needle}}" at)
    if(at EQUAL -1)
      set(${out} "${file}: the ${needle} site (book first, old rule kept) is missing" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The async travel search (IsPossible off the bot thread) reads the bit, it never asks the bot: one ActiveQuestBook() in
  # TravelMgr.cpp, the PlayerTravelInfo constructor.
  count_regex("${travel_cpp}" "ActiveQuestBook\\(\\)" n)
  if(NOT n EQUAL 1)
    set(${out} "TravelMgr.cpp: ActiveQuestBook() ${n} times (1 expected: the PlayerTravelInfo constructor)" PARENT_SCOPE)
    return()
  endif()
  # The callers: K1, K4, K2 value, travel bit, K5, three credits, plus the declaration and the definition.
  count_regex("${all_sources}" "ActiveQuestBook\\(\\)" n)
  if(NOT n EQUAL 10)
    set(${out} "ActiveQuestBook() appears ${n} times in the module (10 expected); a new caller belongs in this contract" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_credits quest_action all_sources out)
  count_regex("${quest_action}" "book->OnCredit\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "QuestAction.cpp: OnCredit ${n} times (3 expected: kill, item, complete)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "->OnCredit\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "->OnCredit( appears ${n} times in the module (3 expected, all in QuestAction.cpp)" PARENT_SCOPE)
    return()
  endif()
  foreach(anchor "bool QuestUpdateAddKillAction::Execute" "bool QuestUpdateAddItemAction::Execute"
      "bool QuestUpdateCompleteAction::Execute")
    string(FIND "${quest_action}" "${anchor}" at)
    if(at EQUAL -1)
      set(${out} "QuestAction.cpp: anchor ${anchor} is gone" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the pure header -----------------------------------------------------------------------------------------------------
function(check_pure name text out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${text}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include (\"CohortPolicy\\.h\"|<cstddef>|<cstdint>)$")
      set(${out} "${name}: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Player " "Quest const" "PlayerbotAI" "std::mutex" "WorldTimer")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "${name}: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- lines, report, test -------------------------------------------------------------------------------------------------
set(LEFT_FMT "\"[QuestBook] state=left_log bot=%s guid=%u cohort=%c quest=%u reason=%s mode=%s\"")
set(STALE_FMT "\"[QuestBook] state=complete_stale bot=%s guid=%u cohort=%c quest=%u age_s=%u mode=%s\"")
set(MIN_FMT "\"[QuestBookMin] cohort=%c mode=%u shadow=%u accept_yes=%llu accept_no_window=%llu accept_no_grey=%llu \"
            \"accept_no_full=%llu accept_no_excl=%llu target_no_window=%llu target_no_excl=%llu turnin_yield=%llu complete_stale=%llu \"
            \"left_log=%llu credit_dropped=%llu\"")
set(SILENT "if ((!sPlayerbotAIConfig.questBook && !sPlayerbotAIConfig.questBookShadow) || now < lastReport + 60)")
set(REPORT "    ReportDuelRule();       // twow-repo#602 W24: [DuelRule] once per minute (AiPlayerbot.Rotation.DuelRule)
    ReportQuestBook();      // A3 step 2: [QuestBookMin] once per minute (AiPlayerbot.QuestBook / QuestBook.Shadow)")
set(TEST "add_executable(questbook_policy_tests
  \"\${PB_MODULE_DIR}/t/questbook_policy_tests.cpp\")")
set(TEST_RUN "add_test(NAME questbook_policy
  COMMAND questbook_policy_tests")

function(check_lines qb_cpp mgr_cpp tests_cmake all_sources out)
  foreach(needle "${LEFT_FMT}" "${STALE_FMT}" "${MIN_FMT}" "${SILENT}")
    string(FIND "${qb_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "QuestBook.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${mgr_cpp}" "${REPORT}" at)
  if(at EQUAL -1)
    set(${out} "RandomPlayerbotMgr.cpp: ReportQuestBook() is not wired after ReportDuelRule()" PARENT_SCOPE)
    return()
  endif()
  # No ";" in a MATCHALL regex: each hit would split the result list in two.
  count_regex("${all_sources}" "ReportQuestBook\\(\\)" n)
  if(NOT n EQUAL 3)
    set(${out} "ReportQuestBook() appears ${n} times (3 expected: the declaration, the definition, the one call)" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "${TEST}" "${TEST_RUN}")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: the questbook_policy unit test is not registered" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run -----------------------------------------------------------------------------------------------------------------
set(failures "")
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_gate("${ai_cpp}" "${ai_h}" "${all_sources}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${travel_cpp}" "${travel_values_cpp}" "${all_sources}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_credits("${quest_action_cpp}" "${all_sources}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_pure("QuestBookPolicy.h" "${policy}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_lines("${qb_cpp}" "${mgr_cpp}" "${tests_cmake}" "${all_sources}" r)
if(r)
  list(APPEND failures "${r}")
endif()
if(failures)
  string(REPLACE ";" "\n  " text "${failures}")
  message(FATAL_ERROR "${TAG}:\n  ${text}")
endif()

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
function(expect_caught reason what)
  if(reason STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

string(REPLACE "uint32 questBook = 0;" "uint32 questBook = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestBook\", 0)" "\"AiPlayerbot.QuestBook\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::questbook::ValidMode(questBook))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "\"AiPlayerbot.QuestBook.Shadow\", 0)" "\"AiPlayerbot.QuestBook.Shadow\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "shadow default 1")

string(REPLACE "            {\"QuestBook.Shadow\", Value(questBookShadow)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "shadow not in [ConfigEcho]")

string(REPLACE "            {\"QuestBook\", Value(questBook)},\n" "" m "${config_cpp}")
string(REPLACE "            {\"QuestGroups" "            {\"QuestBook\", Value(questBook)},\n            {\"QuestGroups" m "${m}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "echo out of byte order")

string(REPLACE "\nAiPlayerbot.QuestBook = 0\n" "\nAiPlayerbot.QuestBook = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "documented 1")

string(REPLACE "\nAiPlayerbot.QuestBook.Shadow = 0\n" "\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "shadow undocumented")

string(REPLACE " && InQuestBookScope())" ")" m "${ai_cpp}")
check_gate("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "no roster scope")

string(REPLACE "            else if (questBook_)\n            {\n                questBook_->Release();\n                questBook_.reset();\n            }\n" "" m "${ai_cpp}")
check_gate("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "no release on reload 1 -> 0")

string(REPLACE "${ACTIVE}" "return questBook_.get();" m "${ai_cpp}")
check_gate("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "the shadow book handed to callers")

string(REPLACE "${QB_GATE}" "        // A3 step 3 (ADR-0041, AiPlayerbot.Supervisor, default 0)" m "${ai_cpp}")
string(REPLACE "${REACTION}" "${QB_GATE}\n${REACTION}" m "${m}")
check_gate("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "observe before the reaction gate (every world tick)")

check_gate("${ai_cpp}" "${ai_h}" "${all_sources}\nbook->Observe(WorldTimer::getMSTime())" r)
expect_caught("${r}" "second observe")

check_gate("${ai_cpp}" "${ai_h}" "${all_sources}\nauto b = std::make_unique<ai::questbook::QuestBook>(ai, false)" r)
expect_caught("${r}" "a second place makes a QuestBook")

string(REPLACE "${K1}" "    return ai::quest_accept::SkipForRosterBot(rosterOnItsOwn, bot->GetLevel(), bot->GetQuestLevelForPlayer(quest)," m "${accept_cpp}")
check_sites("${m}" "${drop_cpp}" "${quest_values_cpp}" "${travel_cpp}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "K1 gone")

string(REPLACE "    uint32 questId = quest->GetQuestId();" "    return false;" m "${drop_cpp}")
check_sites("${accept_cpp}" "${m}" "${quest_values_cpp}" "${travel_cpp}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "K4 old rule dropped")

string(REPLACE "${K2T_OLD}" "" m "${travel_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${m}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "K2 travel old +5 rule dropped")

string(REPLACE "${K3_OLD}" "" m "${travel_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${m}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "K3 old +1 rule dropped")

string(REPLACE "        if (info.HasQuestBook())\n" "        if (info.GetPlayer() && GetBotAI(info.GetPlayer())->ActiveQuestBook())\n" m "${travel_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${m}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "the async search asks the bot")

string(REPLACE "                    else if (quest && (level < quest->GetMinLevel() ||" "                    if (quest && (level < quest->GetMinLevel() ||" m "${quest_values_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${m}" "${travel_cpp}" "${travel_values_cpp}" "${all_sources}" r)
expect_caught("${r}" "K2 value: old deltas run for book bots too")

string(REPLACE "            if (ai->ActiveQuestBook() && !ai->HasRealPlayerMaster())\n" "            if (false)\n" m "${travel_values_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${travel_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "K5 yield gone")

string(REPLACE "                    ai::questbook::Note(bot->GetGUIDLow(), ai::questbook::Count::TurnInYield);\n" "" m "${travel_values_cpp}")
check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${travel_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "K5 yield not counted")

check_sites("${accept_cpp}" "${drop_cpp}" "${quest_values_cpp}" "${travel_cpp}" "${travel_values_cpp}" "${all_sources}\nif (ai->ActiveQuestBook())" r)
expect_caught("${r}" "an unlisted ActiveQuestBook caller")

check_credits("${quest_action_cpp}" "${all_sources}\nbook->OnCredit(1);" r)
expect_caught("${r}" "a credit outside the quest packet actions")

string(REPLACE "                book->OnCredit(pair.first->GetQuestId());\n" "" m "${quest_action_cpp}")
check_credits("${m}" "${all_sources}" r)
expect_caught("${r}" "item credit gone")

check_pure("QuestBookPolicy.h" "${policy}\n#include \"playerbot/playerbot.h\"" r)
expect_caught("${r}" "game include in the policy")

check_pure("QuestBookPolicy.h" "${policy}\nbool x = sPlayerbotAIConfig.enabled;" r)
expect_caught("${r}" "config read in the policy")

check_pure("QuestBookPolicy.h" "${policy}\nint f(Player* p);" r)
expect_caught("${r}" "a Player in the policy")

string(REPLACE "${SILENT}" "if (now < lastReport + 60)" m "${qb_cpp}")
check_lines("${m}" "${mgr_cpp}" "${tests_cmake}" "${all_sources}" r)
expect_caught("${r}" "minute line with switch and shadow off")

string(REPLACE "state=complete_stale" "state=stale" m "${qb_cpp}")
check_lines("${m}" "${mgr_cpp}" "${tests_cmake}" "${all_sources}" r)
expect_caught("${r}" "renamed KPI line")

string(REPLACE "    ReportQuestBook();      // A3 step 2: [QuestBookMin] once per minute (AiPlayerbot.QuestBook / QuestBook.Shadow)\n" "" m "${mgr_cpp}")
check_lines("${qb_cpp}" "${m}" "${tests_cmake}" "${all_sources}" r)
expect_caught("${r}" "report not wired")

string(REPLACE "add_test(NAME questbook_policy\n" "add_test(NAME questbook_policy_off\n" m "${tests_cmake}")
check_lines("${qb_cpp}" "${mgr_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
