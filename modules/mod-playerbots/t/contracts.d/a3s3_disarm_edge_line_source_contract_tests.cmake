cmake_policy(VERSION 3.16)
# A3 step 3, R9b V12 (OB-00 13.11.2026, decision V12 (b); RESULT-R9B-RUN-N1-20261113.md): the disarming edge's ev=mode line
# (cause=arbiter_disarm) goes outside the shadow's line budget of 300 per minute. In R9b the edge wrote one line per bot
# (200) within seconds, the minute dropped one line per cohort, and the disarm check V12 (a) fell back to "not checkable".
# This contract pins:
#   - EmitEdgeLine (SupervisorShadow.h/.cpp): no budget taken, nothing counted as dropped, the line counted in lines under the
#     shadow's lock while a shadow minute runs, then written;
#   - exactly one caller: Supervisor::WriteModeLine, and only for edgeLine = true; every other line keeps EmitEnforceLine;
#   - edgeLine = true only from DisarmEnforce (one line per bot and disarming edge); the arming edge stays in the budget;
#   - the default of edgeLine is false.
# Reads named files and the module's src/ (GLOB_RECURSE). Every check_* returns "" when it holds, else the reason; the
# negative probes at the end reuse them on mutated text, each caught for its own reason.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 disarm edge line: PB_SOURCE_DIR is required")
endif()

set(TAG "a3s3 disarm edge line")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("SupervisorShadow.h" shadow_h)
read_source("SupervisorShadow.cpp" shadow_cpp)
read_source("Supervisor.h" sup_h)
read_source("Supervisor.cpp" sup_cpp)

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
endforeach()

# Plain-text count (a ";" inside a MATCHALL hit would split the list and double count).
function(count_text text needle out)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nl)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${nl}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# The text from the first `start` to the first `stop` after it ("" when start is missing).
function(region text start stop out)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "${stop}" end)
  if(end EQUAL -1)
    set(${out} "${rest}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${rest}" 0 ${end} body)
    set(${out} "${body}" PARENT_SCOPE)
  endif()
endfunction()

set(DECL_EDGE "void EmitEdgeLine(std::uint32_t guidLow, char const* text);")
set(DEF_EDGE "void EmitEdgeLine(std::uint32_t guidLow, char const* text)\n{")
set(EDGE_COUNT "        std::lock_guard<std::mutex> lock(g_mutex);\n        if (g_on > 0)\n            ++g_min[cohortIdx].lines;\n    }\n    sLog.outBasic(\"%s\", text);")
set(DISARM_CALL "    WriteModeLine(from, Mode::Passive, result, \"arbiter_disarm\", arbiter, true);")
set(ARM_CALL "    WriteModeLine(Mode::Passive, Mode::Quest, result, \"arbiter_enforce\", 2);")
set(BRANCH "    if (edgeLine)\n        EmitEdgeLine(line.guid, buf);\n    else\n        EmitEnforceLine(line.guid, buf);\n}")
set(DEFAULT_ARG "    void WriteModeLine(Mode from, Mode to, TransitionResult const& result, char const* cause, std::uint32_t arbiter,\n        bool edgeLine = false);")

# (1) the edge line itself: outside the budget, never dropped, counted in lines.
function(check_edge_fn shadow_h shadow_cpp out)
  string(FIND "${shadow_h}" "${DECL_EDGE}" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.h: EmitEdgeLine is not declared" PARENT_SCOPE)
    return()
  endif()
  region("${shadow_cpp}" "${DEF_EDGE}" "\n}\n" body)
  if(body STREQUAL "")
    set(${out} "SupervisorShadow.cpp: EmitEdgeLine is not defined" PARENT_SCOPE)
    return()
  endif()
  foreach(word "BudgetTake" "g_used" "g_window" "LinesPerMinute")
    string(FIND "${body}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "SupervisorShadow.cpp: EmitEdgeLine uses ${word} (the edge line takes no budget)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${body}" "dropped" at)
  if(NOT at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: EmitEdgeLine counts dropped (the edge line is never dropped)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${body}" "${EDGE_COUNT}" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: EmitEdgeLine does not count lines under the lock while a shadow minute runs, then write" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# (2) one caller, in WriteModeLine, only for edgeLine; (3) edgeLine only from the disarming edge; (4) default false.
function(check_callers sup_h sup_cpp all_sources out)
  count_text("${all_sources}" "EmitEdgeLine(" n)
  if(NOT n EQUAL 3)
    set(${out} "EmitEdgeLine( appears ${n} times in src/ (3 expected: declaration, definition, one caller in WriteModeLine)" PARENT_SCOPE)
    return()
  endif()
  # The bound (review 13.11.): DisarmEnforce has one caller (the enforce edge in Tick), WriteModeLine two (arming, disarming),
  # so no other path, and no call with a variable for edgeLine, reaches the edge line.
  count_text("${all_sources}" "DisarmEnforce(" n)
  if(NOT n EQUAL 3)
    set(${out} "DisarmEnforce( appears ${n} times in src/ (3 expected: declaration, definition, the Tick edge; one edge line per bot and edge)" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "WriteModeLine(" n)
  if(NOT n EQUAL 4)
    set(${out} "WriteModeLine( appears ${n} times in src/ (4 expected: declaration, definition, the arming and the disarming edge)" PARENT_SCOPE)
    return()
  endif()
  region("${sup_cpp}" "void Supervisor::WriteModeLine(" "\n}\n" body)
  string(FIND "${body}\n}" "${BRANCH}" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: WriteModeLine does not end with the edgeLine branch (EmitEdgeLine only for edgeLine)" PARENT_SCOPE)
    return()
  endif()
  count_text("${all_sources}" "${DISARM_CALL}" n_disarm)
  string(REGEX MATCHALL "WriteModeLine\\([^;]*, true\\)" hits "${all_sources}")
  list(LENGTH hits n_wml_true)
  if(NOT n_disarm EQUAL 1 OR NOT n_wml_true EQUAL 1)
    set(${out} "WriteModeLine with edgeLine = true: ${n_wml_true} calls, disarm call ${n_disarm} times (only the disarming edge, once)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_cpp}" "${ARM_CALL}" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: the arming edge's line is not the budgeted WriteModeLine call (only the disarming edge is outside)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_h}" "${DEFAULT_ARG}" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.h: WriteModeLine's edgeLine does not default to false" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

check_edge_fn("${shadow_h}" "${shadow_cpp}" r)
if(r)
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_callers("${sup_h}" "${sup_cpp}" "${all_sources}" r)
if(r)
  message(FATAL_ERROR "${TAG}: ${r}")
endif()

# ---- negative probes: each mutation must be caught for its own reason --------------------------------------------------------
function(expect_reason result want description)
  string(FIND "${result}" "${want}" at)
  if(result STREQUAL "" OR at EQUAL -1)
    message(FATAL_ERROR "${TAG}: negative probe not caught for its reason (${want}): ${description}; got: ${result}")
  endif()
endfunction()

string(REPLACE "        std::lock_guard<std::mutex> lock(g_mutex);\n        if (g_on > 0)\n            ++g_min[cohortIdx].lines;"
  "        std::lock_guard<std::mutex> lock(g_mutex);\n        if (!ai::rot_cast::BudgetTake(g_window, g_used, std::uint32_t(time(nullptr)), LinesPerMinute))\n            return;\n        if (g_on > 0)\n            ++g_min[cohortIdx].lines;"
  m "${shadow_cpp}")
check_edge_fn("${shadow_h}" "${m}" r)
expect_reason("${r}" "takes no budget" "the edge line takes the budget")

string(REPLACE "        if (g_on > 0)\n            ++g_min[cohortIdx].lines;\n    }\n    sLog.outBasic"
  "        if (g_on > 0)\n            ++g_min[cohortIdx].lines;\n        if (g_on > 0)\n            ++g_min[cohortIdx].dropped;\n    }\n    sLog.outBasic"
  m "${shadow_cpp}")
check_edge_fn("${shadow_h}" "${m}" r)
expect_reason("${r}" "never dropped" "the edge line counted as dropped")

string(REPLACE "        if (g_on > 0)\n            ++g_min[cohortIdx].lines;\n    }\n    sLog.outBasic" "    }\n    sLog.outBasic" m "${shadow_cpp}")
check_edge_fn("${shadow_h}" "${m}" r)
expect_reason("${r}" "does not count lines" "the edge line not counted in lines")

string(REPLACE "${DECL_EDGE}" "" m "${shadow_h}")
check_edge_fn("${m}" "${shadow_cpp}" r)
expect_reason("${r}" "not declared" "the declaration gone")

check_callers("${sup_h}" "${sup_cpp}" "${all_sources}\n    EmitEdgeLine(guid, buf);" r)
expect_reason("${r}" "one caller in WriteModeLine" "a second caller of the edge line")

check_callers("${sup_h}" "${sup_cpp}" "${all_sources}\n    DisarmEnforce(arbiter);" r)
expect_reason("${r}" "the Tick edge" "a second caller of the disarming edge")

check_callers("${sup_h}" "${sup_cpp}" "${all_sources}\n    WriteModeLine(from, Mode::Passive, result, \"x\", arbiter, edge);" r)
expect_reason("${r}" "the arming and the disarming edge" "a third WriteModeLine call with a variable edgeLine")

string(REPLACE "${ARM_CALL}" "    WriteModeLine(Mode::Passive, Mode::Quest, result, \"arbiter_enforce\", 2, true);" m_cpp "${sup_cpp}")
string(REPLACE "${ARM_CALL}" "    WriteModeLine(Mode::Passive, Mode::Quest, result, \"arbiter_enforce\", 2, true);" m_all "${all_sources}")
check_callers("${sup_h}" "${m_cpp}" "${m_all}" r)
expect_reason("${r}" "only the disarming edge" "the arming edge outside the budget")

string(REPLACE "${DISARM_CALL}" "    WriteModeLine(from, Mode::Passive, result, \"arbiter_disarm\", arbiter);" m_cpp "${sup_cpp}")
string(REPLACE "${DISARM_CALL}" "    WriteModeLine(from, Mode::Passive, result, \"arbiter_disarm\", arbiter);" m_all "${all_sources}")
check_callers("${sup_h}" "${m_cpp}" "${m_all}" r)
expect_reason("${r}" "only the disarming edge" "the disarming edge back in the budget")

string(REPLACE "    if (edgeLine)\n        EmitEdgeLine(line.guid, buf);\n    else\n        EmitEnforceLine(line.guid, buf);"
  "    EmitEdgeLine(line.guid, buf);" m_cpp "${sup_cpp}")
string(REPLACE "    if (edgeLine)\n        EmitEdgeLine(line.guid, buf);\n    else\n        EmitEnforceLine(line.guid, buf);"
  "    EmitEdgeLine(line.guid, buf);" m_all "${all_sources}")
check_callers("${sup_h}" "${m_cpp}" "${m_all}" r)
expect_reason("${r}" "edgeLine branch" "every mode line outside the budget")

string(REPLACE "bool edgeLine = false);" "bool edgeLine = true);" m "${sup_h}")
check_callers("${m}" "${sup_cpp}" "${all_sources}" r)
expect_reason("${r}" "default to false" "edgeLine defaults to true")

message(STATUS "${TAG}: OK")
