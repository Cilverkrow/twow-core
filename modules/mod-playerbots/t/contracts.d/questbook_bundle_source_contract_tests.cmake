cmake_policy(VERSION 3.16)
# A3 step 2 (ADR-0041, OB-00 order 08.11.2026): quest book bundle scoring QB-B, AiPlayerbot.QuestBook.Bundle (0 off = default,
# 1 all, 2 cohort B, 3 cohort A; other = 0 + error line) and AiPlayerbot.QuestBook.BundleRadius (30..300, default 100), acting only
# where the quest book is active. Plan ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md, section 11. This
# contract pins that:
#   - the switch: member defaults 0 / 100, keys read with defaults 0 / 100, the mode clamped by questbook::ValidMode and the radius
#     by questbundle::ClampRadius, documented = 0 / = 100, echoed next to each other in byte order;
#   - the per-bot bit (and the run speed) is set only when the quest book is active and the switch applies (PlayerTravelInfo);
#   - the ranking runs once, in the async quest fetch right after the whole-map giver fallback and before the giver trace line; it
#     returns at once without the snapshot bit, and its body touches no Player, no ObjectMgr, no AI value and no log (async thread);
#   - the [QuestBundle] pick line is written on the bot thread right after the QB-S pick line, gated by the active book and the
#     switch;
#   - the counters bundle_ranked / bundle_regroup / bundle_capped come after order_scored, in the enum and in the [QuestBookMin] line;
#   - QuestBundlePolicy.h names no game header and reads no config (comments aside); the unit test is registered.
# Pure rules: t/questbundle_policy_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at the
# end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "questbook bundle: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "questbook bundle: PB_MODULE_DIR is required")
endif()

set(TAG "questbook bundle")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("QuestBundlePolicy.h" policy)
read_source("QuestBundle.cpp" qb_cpp)
read_source("QuestBook.h" book_h)
read_source("QuestBook.cpp" book_cpp)
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
set(ECHO "            {\"QuestBook.Bundle\", Value(questBookBundle)},
            {\"QuestBook.BundleRadius\", Value(questBookBundleRadius)},")

function(check_switch config_h config_cpp conf_dist out)
  foreach(needle "uint32 questBookBundle = 0;" "uint32 questBookBundleRadius = 100;")
    string(FIND "${config_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.h: member default missing: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "questBookBundle = uint32(config.GetIntDefault(\"AiPlayerbot.QuestBook.Bundle\", 0));"
      "if (!ai::questbook::ValidMode(questBookBundle))"
      "        questBookBundle = 0;"
      "questBookBundleRadius = ai::questbundle::ClampRadius(config.GetIntDefault(\"AiPlayerbot.QuestBook.BundleRadius\", 100));"
      "${ECHO}")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(pair "Bundle=0" "BundleRadius=100")
    string(REPLACE "=" ";" kv "${pair}")
    list(GET kv 0 key)
    list(GET kv 1 value)
    string(REGEX MATCHALL "\nAiPlayerbot\\.QuestBook\\.${key} = [^\n]*" lines "${conf_dist}")
    list(LENGTH lines n)
    if(NOT n EQUAL 1 OR NOT lines STREQUAL "\nAiPlayerbot.QuestBook.${key} = ${value}")
      set(${out} "conf.dist: AiPlayerbot.QuestBook.${key} must be documented once, = ${value}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- snapshot, ranking, pick line, counters ------------------------------------------------------------------------------
set(SNAPSHOT "    questBundle = questBook && sPlayerbotAIConfig.questBookBundle &&
        ai::questbook::Applies(sPlayerbotAIConfig.questBookBundle, player->GetGUIDLow());
    if (questBundle)
        runSpeed = player->GetSpeed(MOVE_RUN);")
set(RANK_CALL "                        giverPoints += uint32(points.size());
            }

            // A3 step 2 QB-B (AiPlayerbot.QuestBook.Bundle, default 0): the giver sites by quests per second (the distinct quests within
            // BundleRadius over route + accept time), after the whole-map fallback that brings most giver points. Reads the bot-thread
            // snapshot only. Every other bot: returns at once, the list stays as it is.
            ai::questbundle::RankQuestBundles(list, travelInfo);

            if (traceGivers && (giverFetched || giverFallback))")
set(RANK_GATE "    if (!info.HasQuestBundle() || list.empty())
        return;")
set(PICK_GATE "    if (!sPlayerbotAIConfig.questBookBundle || !bot || !ai || !ai->ActiveQuestBook() ||
        !questbook::Applies(sPlayerbotAIConfig.questBookBundle, bot->GetGUIDLow()))
        return;")
set(PICK_CALL "                ai::questsource::NotePick(bot, ai, destination, position);
                // A3 step 2 QB-B (AiPlayerbot.QuestBook.Bundle, default 0): one line per giver pick of a bot the switch applies to,
                // with the bundle recounted from this choice list. Every other bot: no line.
                ai::questbundle::NotePick(bot, ai, destination, position, partitionedList);
                break;")
set(PICK_FMT "\"[QuestBundle] bot=%s guid=%u cohort=%c quest=%u entry=%d bundle=%u sites=%u dist=%u\"")
set(ENUM "    OrderScored,     // QB-S: a fetch whose winners were put in XP-per-minute order
    BundleRanked,    // QB-B: a fetch whose giver sites (two or more) were put in quests-per-second order
    BundleRegroup,   // QB-B: the best site is not the first-fit one
    BundleCapped,    // QB-B: a fetch with more than MaxSites giver sites (the rest followed unranked)
    Max")
set(MIN_TAIL "source_regroup=%llu order_scored=%llu \"
            \"bundle_ranked=%llu bundle_regroup=%llu bundle_capped=%llu\",")
set(MIN_TAKE "take(Count::OrderScored), take(Count::BundleRanked),
            take(Count::BundleRegroup), take(Count::BundleCapped));")

function(check_wiring travel_cpp choose_cpp qb_cpp book_h book_cpp all_sources out)
  string(FIND "${travel_cpp}" "${SNAPSHOT}" at)
  if(at EQUAL -1)
    set(${out} "TravelMgr.cpp: the bundle bit is not gated by the active book and the switch" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${choose_cpp}" "${RANK_CALL}" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: RankQuestBundles is not right after the giver fallback of the quest fetch job" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "RankQuestBundles\\(" n)
  if(NOT n EQUAL 3)
    set(${out} "RankQuestBundles( appears ${n} times (3 expected: declaration, definition, the one call)" PARENT_SCOPE)
    return()
  endif()
  # The ranking body runs on the async thread: no Player, no ObjectMgr, no AI value, no log.
  string(FIND "${qb_cpp}" "void RankQuestBundles(" begin)
  string(FIND "${qb_cpp}" "void NotePick(" end)
  if(begin EQUAL -1 OR end EQUAL -1 OR NOT begin LESS end)
    set(${out} "QuestBundle.cpp: RankQuestBundles must come before NotePick" PARENT_SCOPE)
    return()
  endif()
  math(EXPR len "${end} - ${begin}")
  string(SUBSTRING "${qb_cpp}" ${begin} ${len} body)
  string(FIND "${body}" "${RANK_GATE}" at)
  if(at EQUAL -1)
    set(${out} "QuestBundle.cpp: RankQuestBundles does not return at once without the snapshot bit" PARENT_SCOPE)
    return()
  endif()
  foreach(word "Player*" "GetBot" "sObjectMgr" "AI_VALUE" "GAI_VALUE" "sLog" "->GetSession")
    string(FIND "${body}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "QuestBundle.cpp: RankQuestBundles (async thread) uses ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "${PICK_GATE}" "${PICK_FMT}")
    string(FIND "${qb_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "QuestBundle.cpp: the pick line is not gated by the active book and the switch, or its format changed" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${choose_cpp}" "${PICK_CALL}" at)
  if(at EQUAL -1)
    set(${out} "ChooseTravelTargetAction.cpp: the bundle NotePick does not follow the QB-S pick line" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "questbundle::NotePick\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "questbundle::NotePick( is called ${n} times (1 expected: the pick)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${book_h}" "${ENUM}" at)
  if(at EQUAL -1)
    set(${out} "QuestBook.h: the bundle counters are not right after OrderScored, before Max" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "${MIN_TAIL}" "${MIN_TAKE}")
    string(FIND "${book_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "QuestBook.cpp: the bundle counters are not appended after order_scored in [QuestBookMin]" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the pure header and the test ----------------------------------------------------------------------------------------
function(check_pure text tests_cmake out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${text}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include <(algorithm|cstddef|cstdint|numeric|vector)>$")
      set(${out} "QuestBundlePolicy.h: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Player " "Quest const" "PlayerbotAI" "std::mutex" "WorldTimer"
      "WorldPosition" "TravelDestination")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "QuestBundlePolicy.h: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "add_executable(questbundle_policy_tests" "add_test(NAME questbundle_policy\n")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: the questbundle_policy unit test is not registered" PARENT_SCOPE)
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
check_wiring("${travel_cpp}" "${choose_cpp}" "${qb_cpp}" "${book_h}" "${book_cpp}" "${all_sources}" r)
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

string(REPLACE "uint32 questBookBundle = 0;" "uint32 questBookBundle = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.QuestBook.Bundle\", 0)" "\"AiPlayerbot.QuestBook.Bundle\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::questbook::ValidMode(questBookBundle))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "ai::questbundle::ClampRadius(config.GetIntDefault(\"AiPlayerbot.QuestBook.BundleRadius\", 100))"
  "uint32(config.GetIntDefault(\"AiPlayerbot.QuestBook.BundleRadius\", 100))" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no radius clamp")

string(REPLACE "            {\"QuestBook.Bundle\", Value(questBookBundle)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "\nAiPlayerbot.QuestBook.Bundle = 0\n" "\nAiPlayerbot.QuestBook.Bundle = 1\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "documented 1")

string(REPLACE "\nAiPlayerbot.QuestBook.BundleRadius = 100\n" "\nAiPlayerbot.QuestBook.BundleRadius = 300\n" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "radius documented 300")

string(REPLACE "    questBundle = questBook && sPlayerbotAIConfig.questBookBundle &&" "    questBundle = sPlayerbotAIConfig.questBookBundle &&" m "${travel_cpp}")
check_wiring("${m}" "${choose_cpp}" "${qb_cpp}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "bundle bit without an active book")

string(REPLACE "            ai::questbundle::RankQuestBundles(list, travelInfo);\n" "" m "${choose_cpp}")
check_wiring("${travel_cpp}" "${m}" "${qb_cpp}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "ranking not called")

check_wiring("${travel_cpp}" "${choose_cpp}" "${qb_cpp}" "${book_h}" "${book_cpp}" "${all_sources}\nai::questbundle::RankQuestBundles(list, info);" r)
expect_caught("${r}" "a second ranking call (before the fallback)")

string(REPLACE "${RANK_GATE}" "    if (list.empty())\n        return;" m "${qb_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "ranking without the snapshot bit")

string(REPLACE "    float const speed = info.GetRunSpeed();" "    float const speed = sObjectMgr.GetPlayer(0) ? 7.0f : info.GetRunSpeed();" m "${qb_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "ObjectMgr in the async ranking")

string(REPLACE "    std::vector<std::size_t> const order = Order(scores);" "    std::vector<std::size_t> const order = Order(scores);\n    sLog.outBasic(\"x\");" m "${qb_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "a log call in the async ranking")

string(REPLACE "    if (!sPlayerbotAIConfig.questBookBundle || !bot || !ai || !ai->ActiveQuestBook() ||" "    if (!bot || !ai ||" m "${qb_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${m}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "pick line without the gate")

string(REPLACE "                ai::questbundle::NotePick(bot, ai, destination, position, partitionedList);\n" "" m "${choose_cpp}")
check_wiring("${travel_cpp}" "${m}" "${qb_cpp}" "${book_h}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "pick line not called")

string(REPLACE "    BundleCapped,    // QB-B: a fetch with more than MaxSites giver sites (the rest followed unranked)\n" "" m "${book_h}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${qb_cpp}" "${m}" "${book_cpp}" "${all_sources}" r)
expect_caught("${r}" "a counter missing in the enum")

string(REPLACE "bundle_ranked=%llu bundle_regroup=%llu bundle_capped=%llu" "bundle_ranked=%llu bundle_regroup=%llu" m "${book_cpp}")
check_wiring("${travel_cpp}" "${choose_cpp}" "${qb_cpp}" "${book_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "a counter missing in [QuestBookMin]")

check_pure("${policy}\n#include \"playerbot/playerbot.h\"" "${tests_cmake}" r)
expect_caught("${r}" "game include in the policy")

check_pure("${policy}\nstd::uint32_t r = sPlayerbotAIConfig.questBookBundleRadius;" "${tests_cmake}" r)
expect_caught("${r}" "config read in the policy")

string(REPLACE "add_test(NAME questbundle_policy\n" "add_test(NAME questbundle_policy_off\n" m "${tests_cmake}")
check_pure("${policy}" "${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
