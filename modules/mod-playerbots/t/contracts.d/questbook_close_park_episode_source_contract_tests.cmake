cmake_policy(VERSION 3.16)
# A3 step 2/3, QB6/QB7 pair (A3-BOT-LOGIK 4.5 (8); QB-S3-ANTWORT-20261106.md sections 4-5; CLI-Projekt 20.11.2026 Q1-Q4 (a)):
# the quest book's Close / Park / Episode memory for the planner. This contract pins that:
#   - QuestBook holds one CloseBook, ParkBook and EpisodeBook and forwards to them; IsParked reads the bot's turn-in park only
#     through a const pointer (it never writes there), so the two park lists cannot disagree;
#   - the close memory follows the planner's rule (PlannerShadowPolicy.h Close): the same key, else the first expired slot, else
#     the slot closed longest ago; reason None / closeMs 0 / a span of 2^31 ms or more is no close; deadCycles rises only on Dead;
#     the CloseReason values equal planner::CloseReason;
#   - every episode has a hard end: duration 0 is refused, a duration above MaxEpisodeMs (2 h) is cut, the end is the elapsed
#     time since the start (exact across the getMSTime wrap), Advance never moves the end;
#   - NO CALLER: no module source outside QuestBook.h / QuestBookPolicy.h calls the new methods (the planner's move is its own
#     pair), and QuestBook.cpp is not touched by this pair (the #724 label lines stay as pinned elsewhere);
#   - the unit test is registered.
# Pure rules: t/questbook_policy_tests.cpp (9b, 9c). Every check_* returns "" when it holds, else the reason; the negative probes
# at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "questbook close/park/episode: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "questbook close/park/episode: PB_MODULE_DIR is required")
endif()

set(TAG "questbook close/park/episode")

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
read_source("PlannerShadowPolicy.h" planner_policy)
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
string(REPLACE "\r" "" tests_cmake "${tests_cmake}")

# The module sources that may hold a caller: everything except the book's own two headers. ReplanPolicy.h has an unrelated
# private StartEpisode (the R9 meter); it is matched only through the book's receivers below, never by name alone.
file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(caller_sources "")
foreach(f IN LISTS all_files)
  get_filename_component(n "${f}" NAME)
  if(n STREQUAL "QuestBook.h" OR n STREQUAL "QuestBookPolicy.h")
    continue()
  endif()
  file(READ "${f}" t)
  string(APPEND caller_sources "${t}\n")
endforeach()

# Each needle is one variable (a ";" inside would split an ARGN list).
function(missing text needle_var out)
  string(FIND "${text}" "${${needle_var}}" at)
  if(at EQUAL -1)
    set(${out} "missing: ${${needle_var}}" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

function(first_missing text out)
  foreach(var IN LISTS ARGN)
    missing("${text}" ${var} r)
    if(r)
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- QuestBook: the members and the read-only turn-in park -----------------------------------------------------------------
set(H1 "    CloseBook closes_;")
set(H2 "    ParkBook parks_;")
set(H3 "    EpisodeBook episode_;")
set(H4 "bool IsParked(std::uint32_t questId, std::uint32_t nowMs, turnin_park::Book const* turnInParks = nullptr) const")
set(H5 "return parks_.IsParked(questId, nowMs) || (turnInParks && turnin_park::IsParked(*turnInParks, questId, nowMs));")
set(H6 "closes_.Close(questId, sourceKey, untilMs, reason, nowMs, dropQuest);")
set(H7 "Episode const* ActiveEpisode(std::uint32_t nowMs) const { return episode_.Active(nowMs); }")
set(H8 "bool IsExcluded(std::uint32_t questId) const { return Excluded(questId); }")
set(H9 "#include \"playerbot/QuestTurnInParkPolicy.h\"")
function(check_book qb out)
  first_missing("${qb}" r H1 H2 H3 H4 H5 H6 H7 H8 H9)
  if(r)
    set(${out} "QuestBook.h: ${r}" PARENT_SCOPE)
    return()
  endif()
  # read-only: the book never takes the turn-in park by non-const pointer or reference
  string(REGEX MATCH "turnin_park::Book[ ]*[*&]" w "${qb}")
  if(w)
    set(${out} "QuestBook.h: the turn-in park is taken writable (${w})" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "turnin_park::(ParkNow|RecordFailure|SlotFor|MarkAbandoned)" w "${qb}")
  if(w)
    set(${out} "QuestBook.h: the book writes the turn-in park (${w})" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the close memory: the planner's rule ----------------------------------------------------------------------------------
set(C1 "        if (!questId || reason == std::uint8_t(CloseReason::None) || !Before(nowMs, untilMs) ||
            std::uint32_t(untilMs - nowMs) >= 0x80000000u)
            return;")
set(C2 "        Entry* target = Find(questId, sourceKey);
        if (!target)
            for (Entry& e : entries_)
                if (!Live(e, nowMs))
                {
                    target = &e;
                    break;
                }
        if (!target)
        {
            target = &entries_[0];
            for (Entry& e : entries_)
                if (Before(e.closedAtMs, target->closedAtMs))
                    target = &e;
        }")
set(C3 "target->deadCycles = reason == std::uint8_t(CloseReason::Dead) && dead < 255 ? std::uint8_t(dead + 1) : dead;")
set(C4 "    static constexpr std::size_t Slots = 16;

    void Close(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs,")
set(R0 "    None = 0,         // never stored")
set(R1 "    Dead = 1,         // values as planner::CloseReason")
set(R2 "    R9 = 2,")
set(R3 "    Suppressed = 3,")
set(P1 "    None = 0,
    Dead = 1,
    R9 = 2,
    Suppressed = 3,")
function(check_close pol planner out)
  first_missing("${pol}" r C1 C2 C3 C4 R0 R1 R2 R3)
  if(r)
    set(${out} "QuestBookPolicy.h close memory: ${r}" PARENT_SCOPE)
    return()
  endif()
  first_missing("${planner}" r P1)
  if(r)
    set(${out} "PlannerShadowPolicy.h: planner::CloseReason values changed (${r}); the book's CloseReason must follow" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the episode: a hard end -----------------------------------------------------------------------------------------------
set(E1 "constexpr std::uint32_t MaxEpisodeMs = 7200000;")
set(E2 "        if (!questId || kind == EpisodeKind::None || !durationMs || Active(nowMs))
            return false;
        std::uint32_t const d = durationMs < MaxEpisodeMs ? durationMs : MaxEpisodeMs;")
set(E3 "        if (std::uint32_t(nowMs - startMs_) < durationMs_)
            return &cur_;
        cur_ = Episode{};
        lastEnd_ = EpisodeEnd::Expired;
        return nullptr;")
set(E4 "        if (!e || e->questId != questId)
            return false;
        cur_.stage = stage;
        return true;")
function(check_episode pol out)
  first_missing("${pol}" r E1 E2 E3 E4)
  if(r)
    set(${out} "QuestBookPolicy.h episode: ${r}" PARENT_SCOPE)
    return()
  endif()
  # Advance must not touch the end
  string(FIND "${pol}" "bool Advance(" a)
  string(FIND "${pol}" "bool End(" b)
  if(a EQUAL -1 OR b EQUAL -1 OR b LESS a)
    set(${out} "QuestBookPolicy.h episode: Advance / End not found in order" PARENT_SCOPE)
    return()
  endif()
  math(EXPR len "${b} - ${a}")
  string(SUBSTRING "${pol}" ${a} ${len} adv)
  string(REGEX MATCH "(durationMs_|startMs_|untilMs)[ ]*[-+]?=" w "${adv}")
  if(w)
    set(${out} "QuestBookPolicy.h episode: Advance moves the end (${w})" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- no caller -------------------------------------------------------------------------------------------------------------
# The book's receivers in the module: book->, questBook->, questBook_->, ActiveQuestBook()-> and EnsureQuestBook(...)->.
set(NAMES "Close|IsClosed|OpenSources|CloseDeadCycles|CloseCount|DropRequested|Park|Unpark|IsParked|Parked|IsExcluded|StartEpisode|AdvanceEpisode|EndEpisode|ActiveEpisode|LastEpisodeEnd")
set(RECV "([^A-Za-z0-9_](book|questBook|questBook_)|QuestBook\\([^()]*\\))[ ]*->[ ]*(${NAMES})[ ]*\\(")
# Names only the book has (the R9 meter's private StartEpisode is not called through a book receiver and is not listed here).
set(UNIQUE "(->|\\.)(CloseDeadCycles|CloseCount|DropRequested|AdvanceEpisode|EndEpisode|ActiveEpisode|LastEpisodeEnd)[ ]*\\(")
function(check_no_caller sources qbcpp out)
  string(REGEX MATCH "${RECV}" w "${sources}")
  if(w)
    set(${out} "a caller of the new book methods: '${w}' (the planner's move is its own pair)" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "${UNIQUE}" w "${sources}")
  if(w)
    set(${out} "a caller of the new book methods: '${w}'" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCH "(closes_|parks_|episode_)\\." w "${qbcpp}")
  if(w)
    set(${out} "QuestBook.cpp uses the new memory (${w}): this pair has no caller" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

set(T1 "add_executable(questbook_policy_tests")
set(T2 "add_test(NAME questbook_policy\n")
function(check_test tc out)
  first_missing("${tc}" r T1 T2)
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- run -------------------------------------------------------------------------------------------------------------------
set(failures "")
check_book("${qb_h}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_close("${policy}" "${planner_policy}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_episode("${policy}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_no_caller("${caller_sources}" "${qb_cpp}" r)
if(r)
  list(APPEND failures "${r}")
endif()
check_test("${tests_cmake}" r)
if(r)
  list(APPEND failures "tests.cmake: ${r}")
endif()
if(failures)
  string(REPLACE ";" "\n  " f "${failures}")
  message(FATAL_ERROR "${TAG}:\n  ${f}")
endif()

# ---- negative probes -------------------------------------------------------------------------------------------------------
function(expect_caught r what)
  if(NOT r)
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

string(REPLACE "${H5}" "return parks_.IsParked(questId, nowMs);" m "${qb_h}")
check_book("${m}" r)
expect_caught("${r}" "IsParked ignores the turn-in park")
string(REPLACE "turnin_park::Book const* turnInParks" "turnin_park::Book* turnInParks" m "${qb_h}")
check_book("${m}" r)
expect_caught("${r}" "the turn-in park taken writable")
check_book("${qb_h}\nvoid X(turnin_park::Book const& b) { turnin_park::ParkNow(const_cast<turnin_park::Book&>(b), 1, 0, 1, false); }" r)
expect_caught("${r}" "the book writes the turn-in park")

string(REPLACE "if (Before(e.closedAtMs, target->closedAtMs))" "if (Before(e.untilMs, target->untilMs))" m "${policy}")
check_close("${m}" "${planner_policy}" r)
expect_caught("${r}" "ends-first eviction instead of the planner's oldest close")
string(REPLACE " ||\n            std::uint32_t(untilMs - nowMs) >= 0x80000000u)" ")" m "${policy}")
check_close("${m}" "${planner_policy}" r)
expect_caught("${r}" "no 2^31 span cap")
string(REPLACE "    Dead = 1,\n    R9 = 2," "    Dead = 2,\n    R9 = 1," m "${planner_policy}")
check_close("${policy}" "${m}" r)
expect_caught("${r}" "planner reason values drift")

string(REPLACE "kind == EpisodeKind::None || !durationMs || Active(nowMs)" "kind == EpisodeKind::None || Active(nowMs)" m "${policy}")
check_episode("${m}" r)
expect_caught("${r}" "an episode without an end")
string(REPLACE "if (std::uint32_t(nowMs - startMs_) < durationMs_)" "if (Before(nowMs, cur_.untilMs))" m "${policy}")
check_episode("${m}" r)
expect_caught("${r}" "a wrap-prone end check")
string(REPLACE "        cur_.stage = stage;\n        return true;" "        cur_.stage = stage;\n        durationMs_ += 1000;\n        return true;" m "${policy}")
check_episode("${m}" r)
expect_caught("${r}" "Advance moves the end")

check_no_caller("${caller_sources}\n    if (book->ActiveEpisode(now)) return;" "${qb_cpp}" r)
expect_caught("${r}" "a planner-side caller through book->")
check_no_caller("${caller_sources}\n    ai->ActiveQuestBook()->Close(q, k, u, 2, now);" "${qb_cpp}" r)
expect_caught("${r}" "a caller through ActiveQuestBook()->")
check_no_caller("${caller_sources}\n    questBook_->IsParked(q, now);" "${qb_cpp}" r)
expect_caught("${r}" "a caller through questBook_->")
check_no_caller("${caller_sources}" "${qb_cpp}\n    episode_.Start(1, EpisodeKind::Escort, 1, 1);" r)
expect_caught("${r}" "QuestBook.cpp uses the new memory")
check_no_caller("${caller_sources}\n    parkingLot.Park(std::move(future));\n    meter.StartEpisode(s);" "${qb_cpp}" r)
if(r)
  message(FATAL_ERROR "${TAG}: false positive on an unrelated Park / StartEpisode: ${r}")
endif()

string(REPLACE "add_test(NAME questbook_policy\n" "add_test(NAME questbook_policy_off\n" m "${tests_cmake}")
check_test("${m}" r)
expect_caught("${r}" "unit test not registered")

message(STATUS "${TAG}: OK")
