cmake_policy(VERSION 3.16)
# A3 R9 / R2 producer, pair R9-1 (R9-REPLAN-INTERFACE v5 9575095d; v4 d44daade normative in A3-BOT-LOGIK 4.3; A3-GERUEST-L60 E2):
# the pure progress meter ReplanPolicy.h, unit suite t/replan_policy_tests.cpp. Pinned here:
#   (a) ReplanPolicy.h is pure: standard headers only, no game type, no log, no config read, no lock;
#   (b) the spec defaults (N 600 s, G 10 yd, RestCapS 300, MaxReplansPerHour 6, Close 7200 s, 2nd-stall window 2 h, 2 Keeps in a
#       row, reloc_loop at the 3rd, planned loop at the 4th) and the forced rules (never rate_limited; Death/Revive/Rescue not
#       loop-counted; only a hearth or a teleport ack with a confirmed leg is planned);
#   (c) tests.cmake registers replan_policy;
#   (d) since the wiring pair R9-2 (twow-repo#575): only ReplanPolicy.h, PlannerShadow.h and PlannerShadow.cpp name the meter
#       (ProgressMeter); including the header for its types is fine everywhere (the R9-2 wiring is pinned in
#       r9_replan_wiring_source_contract_tests.cmake);
#   (e) OB-10 nits: NoteGiveUp is called only for a stall decision (2 occurrences with the definition), and the minute counters
#       carry forced_after_rate_limited (Counters, Add, the [ReplanMin] field).
# Every check_* returns "" when it holds, else the reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "r9 replan_policy: PB_SOURCE_DIR is required")
endif()
get_filename_component(PB_MODULE_ROOT "${PB_SOURCE_DIR}/../.." ABSOLUTE)

set(TAG "r9 replan_policy")

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

# Number of non-overlapping occurrences of needle (plain text, no regex, no list splitting).
function(count_text text needle out_var)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nl)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${nl}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/ReplanPolicy.h" policy)
read_file("${PB_MODULE_ROOT}/tests.cmake" tests)

# ---- (a) pure -----------------------------------------------------------------------------------------------------------
function(check_pure policy out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${policy}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include <(cmath|cstddef|cstdint|cstdio)>$")
      set(${out} "ReplanPolicy.h: include '${inc}' (standard headers only)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(word "Player*" "Player&" "Unit*" "WorldPosition" "sLog" "sPlayerbotAIConfig" "getMSTime" "std::atomic" "std::mutex"
               "static std::" "thread_local")
    string(FIND "${policy}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "ReplanPolicy.h: names '${word}' (pure header)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) defaults and forced rules --------------------------------------------------------------------------------------
function(check_rules policy out)
  foreach(needle
      "std::uint32_t stallMs = 600000;"
      "float gainYd = 10.0f;"
      "std::uint32_t restCapMs = 300000;"
      "std::uint32_t maxReplansPerHour = 6;"
      "std::uint32_t closeForS = 7200;"
      "std::uint32_t secondStallWindowMs = 7200000;"
      "std::uint32_t keepLimit = 2;"
      "std::uint32_t relocLoop = 3;"
      "std::uint32_t plannedLoopRelocs = 4;"
      "constexpr float CellYd = 50.0f;"
      "constexpr float CellZYd = 10.0f;"
      "    return s != RelocSource::Death && s != RelocSource::Revive && s != RelocSource::Rescue;"
      "    return planned && legIndex != NoLeg && (s == RelocSource::Hearth || s == RelocSource::TeleportAck);"
      "        if (forced == Reason::None && v.d != Decision::Keep && DecisionsLastHour(now) >= m_p.maxReplansPerHour)"
      "    virtual Verdict OnStall(StallEvent const&) = 0;"
      "    virtual Verdict OnRelocate(RelocationEvent const&) = 0;")
    count_text("${policy}" "${needle}" n)
    if(NOT n EQUAL 1)
      set(${out} "ReplanPolicy.h: '${needle}' not found once" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) registration ---------------------------------------------------------------------------------------------------
function(check_registered tests out)
  count_text("${tests}" "add_test(NAME replan_policy\n  COMMAND replan_policy_tests" n)
  if(NOT n EQUAL 1)
    set(${out} "tests.cmake: replan_policy not registered" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (d) callers: only the planner state (PlannerShadow.h) and the planner tick (PlannerShadow.cpp) run the meter ----------
# R9-2 (twow-repo#575, DESIGN-R9-2 D13) replaced the R9-1 inert check with this allowlist. Including the header for its types is
# fine everywhere (RelocSource, Counters, the sink interface).
set(METER_ALLOWED "ReplanPolicy.h" "PlannerShadow.h" "PlannerShadow.cpp")

function(check_inert_text name text out)
  if(name IN_LIST METER_ALLOWED)
    set(${out} "" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${text}" "ProgressMeter" at)
  if(NOT at EQUAL -1)
    set(${out} "${name}: uses ProgressMeter (only ${METER_ALLOWED} may run the meter)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_inert src_dir out)
  file(GLOB_RECURSE sources "${src_dir}/*.cpp" "${src_dir}/*.h")
  foreach(path IN LISTS sources)
    get_filename_component(name "${path}" NAME)
    file(READ "${path}" text)
    check_inert_text("${name}" "${text}" r)
    if(NOT "${r}" STREQUAL "")
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) OB-10 nits (R9-2): a give-up comes only from a stall decision; the minute counters carry forced_after_rate_limited ----
function(check_nits policy out)
  count_text("${policy}" "NoteGiveUp(" n)
  if(NOT n EQUAL 2)
    set(${out} "ReplanPolicy.h: 'NoteGiveUp(' found ${n} times, want 2 (the stall decision in Feed and the definition)" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "    std::uint32_t stalls = 0, relocates = 0, keep = 0, replan = 0, close = 0, forced = 0, rateLimited = 0, forcedAfterRateLimited = 0,\n                  gaps = 0, plannedCredit = 0, hops = 0, grindNoKill = 0, offMap = 0, forcedDropped = 0;"
      "inline void Add(Counters& a, Counters const& b)"
      "forced_after_rate_limited=%u"
      "dropped=%u forced_dropped=%u\""
      "    a.forcedDropped += b.forcedDropped;")
    count_text("${policy}" "${needle}" n)
    if(NOT n EQUAL 1)
      set(${out} "ReplanPolicy.h: '${needle}' not found once" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

check_pure("${policy}" r)
fail_if("${r}")
check_rules("${policy}" r)
fail_if("${r}")
check_registered("${tests}" r)
fail_if("${r}")
check_inert("${PB_SOURCE_DIR}" r)
fail_if("${r}")
check_nits("${policy}" r)
fail_if("${r}")

# ---- negative probes ----------------------------------------------------------------------------------------------------
function(expect_caught what reason)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

string(REPLACE "#include <cstdio>" "#include <cstdio>\n#include \"playerbot/PlayerbotAIConfig.h\"" m "${policy}")
check_pure("${m}" r)
expect_caught("game include in the pure header" "${r}")
string(REPLACE "class ProgressMeter\n{" "class ProgressMeter\n{\n    void Log() { sLog.outBasic(\"x\"); }" m "${policy}")
check_pure("${m}" r)
expect_caught("log in the pure header" "${r}")
string(REPLACE "std::uint32_t stallMs = 600000;" "std::uint32_t stallMs = 300000;" m "${policy}")
check_rules("${m}" r)
expect_caught("N changed" "${r}")
string(REPLACE "std::uint32_t relocLoop = 3;" "std::uint32_t relocLoop = 4;" m "${policy}")
check_rules("${m}" r)
expect_caught("reloc_loop limit changed" "${r}")
string(REPLACE "if (forced == Reason::None && v.d != Decision::Keep && DecisionsLastHour"
  "if (v.d != Decision::Keep && DecisionsLastHour" m "${policy}")
check_rules("${m}" r)
expect_caught("forced decisions rate-limited" "${r}")
string(REPLACE " && s != RelocSource::Rescue;" ";" m "${policy}")
check_rules("${m}" r)
expect_caught("rescue teleports loop-counted" "${r}")
string(REPLACE "planned && legIndex != NoLeg && " "planned && " m "${policy}")
check_rules("${m}" r)
expect_caught("planned without a confirmed leg" "${r}")
string(REPLACE "add_test(NAME replan_policy\n" "add_test(NAME replan_policy_off\n" m "${tests}")
check_registered("${m}" r)
expect_caught("suite not registered" "${r}")
check_inert_text("Supervisor.cpp" "#include \"ReplanPolicy.h\"\nai::replan::ProgressMeter meter;\n" r)
expect_caught("a meter outside the planner files" "${r}")
check_inert_text("PlannerShadow.cpp" "#include \"ReplanPolicy.h\"\nai::replan::ProgressMeter meter;\n" r)
if(NOT "${r}" STREQUAL "")
  message(FATAL_ERROR "${TAG}: the planner tick must stay allowed to run the meter: ${r}")
endif()
check_inert_text("PlannerPolicy.h" "#include \"ReplanPolicy.h\"\nai::replan::RelocSource src = ai::replan::RelocSource::Unknown;\n" r)
if(NOT "${r}" STREQUAL "")
  message(FATAL_ERROR "${TAG}: a type-only include must stay allowed: ${r}")
endif()
string(REPLACE "        Apply(v, in.atMs, r);\n" "        Apply(v, in.atMs, r);\n        if (v.d != Decision::Keep)\n            NoteGiveUp(m_key, in.atMs);\n"
  m "${policy}")
check_nits("${m}" r)
expect_caught("a relocation decision recorded as a give-up (OB-10 nit 1)" "${r}")
string(REPLACE "\"forced_after_rate_limited=%u gaps=%u" "\"gaps=%u" m "${policy}")
check_nits("${m}" r)
expect_caught("forced_after_rate_limited missing from [ReplanMin]" "${r}")
string(REPLACE " forced_dropped=%u\"" "\"" m "${policy}")
check_nits("${m}" r)
expect_caught("forced_dropped missing from [ReplanMin] (R9-a)" "${r}")

message(STATUS "${TAG}: all checks passed")
