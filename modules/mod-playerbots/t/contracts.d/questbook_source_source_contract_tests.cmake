cmake_policy(VERSION 3.16)
# A3 step 2 (ADR-0041, OB-00 07.11.2026): quest book source choice and order QB-S, AiPlayerbot.QuestBook.SourceChoice (0 off =
# default, 1 all, 2 cohort B, 3 cohort A; other = 0 + error line), acting only where the quest book is active. Plan
# ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md, section 7. This contract pins that:
#   - the switch: member default 0, key read with default 0 and clamped by questbook::ValidMode, documented = 0, echoed in byte
#     order with KillSeconds and SingleSpawnMargin;
#   - the per-bot snapshot is filled only when the quest book is active and the switch applies (PlayerTravelInfo constructor);
#   - the ranking runs once, in the async quest fetch after the merge and before the hub sort; it returns at once without the
#     snapshot bit, and its body touches no Player, no ObjectMgr and no AI value (async thread);
#   - the source facts are written once, at load (SetSourceStats only in LoadQuestTravelTable), with the [QuestSourceLoad] line;
#   - the [QuestSource] pick line is written on the bot thread, gated by the active book and the switch;
#   - QuestSourcePolicy.h names no game header and reads no config (comments aside); the unit test is registered.
# Pure rules: t/questsource_policy_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at the
# end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "questbook source: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "questbook source: PB_MODULE_DIR is required")
endif()

set(TAG "questbook source")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("QuestSourcePolicy.h" policy)
read_source("QuestSource.cpp" qs_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel_cpp)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose_cpp)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(APPEND all_sources "${t}\n")
endforeach()

# No ";" in a MATCHALL regex: each hit would split the result list in two.
function(count_regex text regex out)
  string(REGEX MATCHALL "${regex}" hits "${text}")
  list(LENGTH hits n)
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# ---- the switch ----------------------------------------------------------------------------------------------------------
set(ECHO "            {\"QuestBook.Exclude.count\", Value(uint32(questBookExclude.size()))},
            {\"QuestBook.KillSeconds\", Value(questBookKillSeconds)},
            {\"QuestBook.NeedPerObjective\", Value(questBookNeedPerObjective)},
            {\"QuestBook.NeedPerObjective.ItemUse\", Value(questBookNeedPerObjectiveItemUse)},
            {\"QuestBook.NeedPerObjective.Log\", Value(questBookNeedPerObjectiveLog)},
            {\"QuestBook.Shadow\", Value(questBookShadow)},
            {\"QuestBook.SingleSpawnMargin\", Value(questBookSingleSpawnMargin)},
            {\"QuestBook.SourceChoice\", Value(questBookSourceChoice)},
            {\"QuestBook.StaleCompleteSeconds\"")

function(check_switch config_h config_cpp conf_dist out)
  string(FIND "${config_h}" "uint32 questBookSourceChoice = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: member not off by default: uint32 questBookSourceChoice = 0;" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "questBookSourceChoice = uint32(config.GetIntDefault(\"AiPlayerbot.QuestBook.SourceChoice\", 0));"
      "if (!ai::questbook::ValidMode(questBookSourceChoice))"
      "        questBookSourceChoice = 0;"
      "${ECHO}")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "\nAiPlayerbot\\.QuestBook\\.SourceChoice = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.QuestBook.SourceChoice = 0")
    set(${out} "conf.dist: AiPlayerbot.QuestBook.SourceChoice must be documented once, = 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- snapshot, ranking, load, pick line ----------------------------------------------------------------------------------
set(SNAPSHOT "    questSourceChoice = questBook && sPlayerbotAIConfig.questBookSourceChoice &&
        ai::questbook::Applies(sPlayerbotAIConfig.questBookSourceChoice, player->GetGUIDLow());
    if (questSourceChoice)")
set(RANK_CALL "            ai::questsource::RankQuestSources(list, travelInfo);

            // #421 B: widened search - quest hubs first (in this job, not on the map thread).
            if (hubsFirst)
                SortQuestHubsFirst(list);")
set(RANK_GATE "    if (!info.HasQuestSourceChoice() || list.empty())
        return;")
set(PICK_GATE "    if (!sPlayerbotAIConfig.questBookSourceChoice || !bot || !ai || !ai->ActiveQuestBook() ||
        !questbook::Applies(sPlayerbotAIConfig.questBookSourceChoice, bot->GetGUIDLow()))
        return;")
set(PICK_CALL "                target->SetTarget(destination, position);
                hasTarget = true;")
set(LOAD_LINE "sLog.outBasic(\"[QuestSourceLoad] destinations=%u entries=%u ms=%u\"")
set(GO_CHANCE "    float const chance = entry < 0 ? GameObjectLootChance(uint32(-entry), quest->ReqItemId[index])")

function(check_wiring travel_cpp choose_cpp qs_cpp all_sources out)
  string(FIND "${travel_cpp}" "${SNAPSHOT}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: the snapshot is not gated by the active book and the switch" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${choose_cpp}" "${RANK_CALL}" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: RankQuestSources is not right before the hub sort of the quest fetch job" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "RankQuestSources\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "RankQuestSources( appears ${n} times (3 expected: declaration, definition, the one call)" PARENT_SCOPE)
    return()
  endif()
  # The ranking body runs on the async thread: no Player, no ObjectMgr, no AI value, no log.
  string(FIND "${qs_cpp}" "void RankQuestSources(" begin)
  string(FIND "${qs_cpp}" "void NotePick(" end)
  if(begin EQUAL -1 OR end EQUAL -1 OR NOT begin LESS end)
    set(${out} "QuestSource.cpp: RankQuestSources must come before NotePick" PARENT_SCOPE)
    return()
  endif()
  math(EXPR len "${end} - ${begin}")
  string(SUBSTRING "${qs_cpp}" ${begin} ${len} body)
  string(FIND "${body}" "${RANK_GATE}" at)
  if(at EQUAL -1)
    set(${out} "QuestSource.cpp: RankQuestSources does not return at once without the snapshot bit" PARENT_SCOPE)
    return()
  endif()
  foreach(word "Player*" "GetBot" "sObjectMgr" "AI_VALUE" "GAI_VALUE" "sLog" "->GetSession")
    string(FIND "${body}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "QuestSource.cpp: RankQuestSources (async thread) uses ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${qs_cpp}" "${PICK_GATE}" at)
  if(at EQUAL -1)
    set(${out} "QuestSource.cpp: NotePick is not gated by the active book and the switch" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${choose_cpp}" "${PICK_CALL}\n                // A3 step 2 QB-S" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: NotePick does not follow the pick" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "->SetSourceStats\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "SetSourceStats is called ${n} times (1 expected: the load)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${travel_cpp}" "${LOAD_LINE}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: the [QuestSourceLoad] line is missing" PARENT_SCOPE)
    return()
  endif()
  # The shared "loot chance" value returns 0 for every game object (the negative entry overflows into the high guid bits), so a
  # game object source reads its chance from GameObjectLootChance with the positive entry.
  string(FIND "${travel_cpp}" "${GO_CHANCE}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: a game object source does not read its drop chance through GameObjectLootChance(-entry)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the pure header and the test ----------------------------------------------------------------------------------------
function(check_pure text tests_cmake out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${text}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include (\"PlannerPolicy\\.h\"|<cstddef>|<cstdint>)$")
      set(${out} "QuestSourcePolicy.h: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Player " "Quest const" "PlayerbotAI" "std::mutex" "WorldTimer")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "QuestSourcePolicy.h: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "add_executable(questsource_policy_tests" "add_test(NAME questsource_policy\n")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: the questsource_policy unit test is not registered" PARENT_SCOPE)
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
check_wiring("${travel_cpp}" "${choose_cpp}" "${qs_cpp}" "${all_sources}" r)
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

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
function(expect_caught reason what)
  if(reason STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

string(REPLACE "uint32 questBookSourceChoice = 0;" "uint32 questBookSourceChoice = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestBook.SourceChoice\", 0)" "\"AiPlayerbot.QuestBook.SourceChoice\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::questbook::ValidMode(questBookSourceChoice))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "            {\"QuestBook.SourceChoice\", Value(questBookSourceChoice)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "\nAiPlayerbot.QuestBook.SourceChoice = 0\n" "\nAiPlayerbot.QuestBook.SourceChoice = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "documented 1")

string(REPLACE "    questSourceChoice = questBook && sPlayerbotAIConfig.questBookSourceChoice &&" "    questSourceChoice = sPlayerbotAIConfig.questBookSourceChoice &&" m "${travel_cpp}")
check_wiring("${m}" "${choose_cpp}" "${qs_cpp}" "${all_sources}" r)
expect_caught("${r}" "snapshot without an active book")

string(REPLACE "${RANK_CALL}" "            // #421 B: widened search - quest hubs first (in this job, not on the map thread).\n            if (hubsFirst)\n                SortQuestHubsFirst(list);" m "${choose_cpp}")
check_wiring("${travel_cpp}" "${m}" "${qs_cpp}" "${all_sources}" r)
expect_caught("${r}" "ranking not called")

check_wiring("${travel_cpp}" "${choose_cpp}" "${qs_cpp}" "${all_sources}\nai::questsource::RankQuestSources(list, info);" r)
expect_caught("${r}" "a second ranking call")

string(REPLACE "${RANK_GATE}" "    if (list.empty())\n        return;" m "${qs_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "ranking without the snapshot bit")

string(REPLACE "    float const speed = info.GetRunSpeed();" "    float const speed = sObjectMgr.GetPlayer(0) ? 7.0f : info.GetRunSpeed();" m "${qs_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "ObjectMgr in the async ranking")

string(REPLACE "    if (!sPlayerbotAIConfig.questBookSourceChoice || !bot || !ai || !ai->ActiveQuestBook() ||" "    if (!bot || !ai ||" m "${qs_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${all_sources}" r)
expect_caught("${r}" "pick line without the gate")

check_wiring("${travel_cpp}" "${choose_cpp}" "${qs_cpp}" "${all_sources}\nobjective->SetSourceStats(ai::questsource::Stats{});" r)
expect_caught("${r}" "source facts written after the load")

string(REPLACE "${GO_CHANCE}" "    float const chance = GAI_VALUE2(float, \"loot chance\", std::to_string(entry) + \" \" + std::to_string(quest->ReqItemId[index]));" m "${travel_cpp}")
check_wiring("${m}" "${choose_cpp}" "${qs_cpp}" "${all_sources}" r)
expect_caught("${r}" "game object chance through the shared loot chance value")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" "${tests_cmake}" r)
expect_caught("${r}" "game include in the policy")

check_pure("${policy}\nfloat k = sPlayerbotAIConfig.questBookKillSeconds;" "${tests_cmake}" r)
expect_caught("${r}" "config read in the policy")

string(REPLACE "add_test(NAME questsource_policy\n" "add_test(NAME questsource_policy_off\n" m "${tests_cmake}")
check_pure("${policy}" "${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
