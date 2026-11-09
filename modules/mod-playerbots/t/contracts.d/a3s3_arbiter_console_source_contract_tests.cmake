cmake_policy(VERSION 3.16)
# A3 step 3 arbiter shadow run (OB-00 09.11.2026; PREREG A3-S3-PREREG-ARBITER-SCHATTEN v1.2 c1e88eaa + addendum f2e2a8c9,
# evaluator arb-shadow-eval.awk 5a673e15): the live arbiter switch "rndbot arbiter on|off". On pin 2n no console path switched
# AiPlayerbot.Supervisor.Arbiter at run time (HandleConsoleReload is not registered, ".reload config" does not touch
# PlayerbotAIConfig, ".bot" is not console-enabled). Pinned here:
#   (a) registration: the "arbiter" branch in RandomPlayerbotMgr::HandlePlayerbotConsoleCommand, local console only (isRA refused
#       first), parse before set, one [ArbiterCtl] line, before the handler map; HandleConsoleReload stays unregistered;
#   (b) the switch is one process-wide std::atomic<int> override in SupervisorShadow.cpp, set only by the console branch,
#       read only through ArbiterOn at the supervisor gate (the config key stays the start value, default 0 unchanged);
#   (c) no PlayerbotAIConfig::Initialize: the branch calls no Initialize and touches no sPlayerbotAIConfig field; the
#       module's Initialize() calls stay the 3 of the base (startup hook, PlayerbotMgr, the unregistered console reload);
#   (d) OB-00 condition 09.11.: the line formats the frozen evaluator reads ([Supervisor], [ArbiterMin], [BotUpdate],
#       [BotUpdateCohort]) stay byte-identical: sha256 of each formatter / emitter region, taken at release/9.x 4e578ee5
#       and main 9e15610f (identical in both);
#   (e) the pure rules (ParseArbiterSwitch, ArbiterEffective) are unit-tested in t/supervisor_policy_tests.cpp.
# Every check_* returns "" when it holds, else the reason; the negative probes at the end must all be caught.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3 arbiter console: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3 arbiter console: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 arbiter console")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

function(expect_caught what reason)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

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

# The text from the first `start` up to and including the first `stop` after it ("" when start or stop is missing).
function(region text start stop out)
  set(${out} "" PARENT_SCOPE)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "${stop}" end)
  if(end EQUAL -1)
    return()
  endif()
  string(LENGTH "${stop}" sl)
  math(EXPR end "${end} + ${sl}")
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("SupervisorShadow.h" shadow_h)
read_source("SupervisorShadow.cpp" shadow_cpp)
read_source("SupervisorPolicy.h" policy)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
file(READ "${PB_MODULE_DIR}/t/supervisor_policy_tests.cpp" unit_test)
string(REPLACE "\r" "" unit_test "${unit_test}")

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
endforeach()

# ---- (a) registration --------------------------------------------------------------------------------------------------------
set(ARB_BRANCH "    if (cmd == \"arbiter\" || cmd.find(\"arbiter \") == 0)\n    {\n        if (isRA)\n        {\n            sLog.outError(\"[ArbiterCtl] rejected: local mangosd console required\");\n            handler->SendSysMessage(\"rndbot arbiter is restricted to the local mangosd console.\");\n            return true;\n        }\n        int const value = ai::supervisor::ParseArbiterSwitch(cmd.c_str() + 7);\n        if (value == ai::supervisor::kArbiterFollowConfig)\n        {\n            sLog.outError(\"[ArbiterCtl] usage: rndbot arbiter on|off\");\n            return true;\n        }\n        int const prev = ai::supervisor::SetArbiterOverride(value == ai::supervisor::kArbiterOn);\n        sLog.outString(\"[ArbiterCtl] arbiter=%d prev=%s source=console\", value,\n            prev == ai::supervisor::kArbiterFollowConfig ? \"conf\" : (prev == ai::supervisor::kArbiterOn ? \"1\" : \"0\"));\n        return true;\n    }\n\n    std::map<std::string, ConsoleCommandHandler> handlers;\n")
set(IS_RA "    if (handler->GetSession()) //Client command\n        isRA = true;\n    else if (static_cast<CliHandler*>(handler) && static_cast<CliHandler*>(handler)->GetAccountId()) //RA call with account.\n        isRA = true;\n")

function(check_registration mgr out)
  string(FIND "${mgr}" "${ARB_BRANCH}" at)
  if(at EQUAL -1)
    set(${out} "RandomPlayerbotMgr.cpp: the arbiter branch (console only, parse, set, [ArbiterCtl]) is not right before the handler map" PARENT_SCOPE)
    return()
  endif()
  count_text("${mgr}" "cmd == \"arbiter\"" n)
  if(NOT n EQUAL 1)
    set(${out} "RandomPlayerbotMgr.cpp: ${n} arbiter branches (1 expected)" PARENT_SCOPE)
    return()
  endif()
  # The branch sits in HandlePlayerbotConsoleCommand after the isRA rule (local console = no session and no RA account).
  string(FIND "${mgr}" "${IS_RA}" ra)
  string(FIND "${mgr}" "bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(" fn)
  if(ra EQUAL -1 OR fn EQUAL -1 OR NOT ra GREATER fn OR NOT at GREATER ra)
    set(${out} "RandomPlayerbotMgr.cpp: the arbiter branch is not in HandlePlayerbotConsoleCommand after the isRA rule" PARENT_SCOPE)
    return()
  endif()
  # Not a handler-map entry (the map runs for game sessions and RA too) and the full reload stays unregistered.
  string(REGEX MATCH "handlers\\[\"(arbiter|reload)[^\"]*\"\\]" bad "${mgr}")
  if(NOT bad STREQUAL "")
    set(${out} "RandomPlayerbotMgr.cpp: ${bad} registered in the handler map" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) the atomic, its one writer and its one reader --------------------------------------------------------------------
set(ATOMIC "namespace\n{\n// Live arbiter switch (OB-00 09.11.2026): kArbiterFollowConfig until the console sets it.\nstd::atomic<int> g_arbiterOverride{kArbiterFollowConfig};\n}\n\nbool ArbiterOn(std::uint32_t configValue)\n{\n    return ArbiterEffective(g_arbiterOverride.load(std::memory_order_relaxed), configValue);\n}\n\nint SetArbiterOverride(bool on)\n{\n    return g_arbiterOverride.exchange(on ? kArbiterOn : kArbiterOff, std::memory_order_relaxed);\n}\n")
set(GATE "            EnsureSupervisor()->Tick(WorldTimer::getMSTime(), ai::supervisor::ArbiterOn(sPlayerbotAIConfig.supervisorArbiter));\n")

function(check_switch shadow_h shadow_cpp policy ai_cpp config_h all_sources out)
  string(FIND "${shadow_cpp}" "${ATOMIC}" at)
  if(at EQUAL -1)
    set(${out} "SupervisorShadow.cpp: the override is not one std::atomic<int> read by ArbiterOn and exchanged by SetArbiterOverride" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "bool ArbiterOn(std::uint32_t configValue);" "int SetArbiterOverride(bool on);")
    string(FIND "${shadow_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "SupervisorShadow.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "constexpr int kArbiterFollowConfig = -1;" "constexpr int kArbiterOff = 0;" "constexpr int kArbiterOn = 1;"
      "inline bool ArbiterEffective(int override, std::uint32_t configValue)\n{\n    if (override == kArbiterOn)\n        return true;\n    if (override == kArbiterOff)\n        return false;\n    return configValue == 1;\n}")
    string(FIND "${policy}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "SupervisorPolicy.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${ai_cpp}" "${GATE}" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the supervisor gate does not pass ArbiterOn(config)" PARENT_SCOPE)
    return()
  endif()
  # One writer (the console branch), one reader (the gate), one atomic; the config default stays 0.
  count_text("${all_sources}" "SetArbiterOverride(" w)
  count_text("${all_sources}" "ArbiterOn(" r)
  count_text("${all_sources}" "g_arbiterOverride" g)
  if(NOT w EQUAL 3 OR NOT r EQUAL 3 OR NOT g EQUAL 3)
    set(${out} "SetArbiterOverride( ${w} (3: decl, def, console), ArbiterOn( ${r} (3: decl, def, gate), g_arbiterOverride ${g} (3)" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${config_h}" "    uint32 supervisorArbiter = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: Supervisor.Arbiter member not default 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) no PlayerbotAIConfig::Initialize --------------------------------------------------------------------------------------
function(check_no_initialize mgr all_sources out)
  region("${mgr}" "    if (cmd == \"arbiter\" || cmd.find(\"arbiter \") == 0)" "\n    }\n" branch)
  if(branch STREQUAL "")
    set(${out} "the arbiter branch is missing" PARENT_SCOPE)
    return()
  endif()
  foreach(bad "Initialize" "sPlayerbotAIConfig" "PlayerbotAIConfig" "sConfig" "LoadConfig")
    string(FIND "${branch}" "${bad}" at)
    if(NOT at EQUAL -1)
      set(${out} "the arbiter branch touches ${bad} (only the atomic may change)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_text("${all_sources}" "sPlayerbotAIConfig.Initialize()" n)
  if(NOT n EQUAL 3)
    set(${out} "${n} sPlayerbotAIConfig.Initialize() calls in the module (3 at the base, no new one)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (d) the evaluator's line formats, byte-identical ---------------------------------------------------------------------------
# sha256 (first 16 hex) of each region, from its start up to and including its end marker, at release/9.x 4e578ee5 = main 9e15610f.
# "@SEMI@" stands for ";" (a list element must not hold one).
set(FORMAT_PINS
  "policy|inline std::size_t Sanitize(|\n}\n|ccb8b9c5bb6d3afd"
  "policy|inline std::size_t FormatModeLine(|\n}\n|b268688feb2183f4"
  "policy|inline std::size_t FormatPath(|\n}\n|2128ecce2618a0bc"
  "policy|inline std::size_t FormatWriters(|\n}\n|f8fe1a3a2e87fc9a"
  "policy|inline std::size_t FormatWriteLine(|\n}\n|fc8202db453ccdae"
  "policy|inline std::size_t FormatExpireLine(|\n}\n|db084e74f526c99e"
  "policy|inline std::size_t FormatArbiterMin(|\n}\n|2662747efaa85911"
  "shadow|void Emit(Closed const& closed|\n}\n|31c2f479f3780942"
  "shadow|void EmitMinute(MinuteOut const& minute)|\n}\n|9d109ed068dd4242"
  "mgr|sLog.outBasic(\"[BotUpdate] calls=|)@SEMI@\n|614c24fe1571082d"
  "mgr|sLog.outBasic(\"[BotUpdateCohort] cohort=|)@SEMI@\n|f7ee6ffa3d7e9149")

function(check_formats policy shadow mgr out)
  foreach(pin IN LISTS FORMAT_PINS)
    string(REPLACE "|" ";" parts "${pin}")
    list(GET parts 0 file)
    list(GET parts 1 start)
    list(GET parts 2 stop)
    string(REPLACE "@SEMI@" ";" stop "${stop}")   # a ";" inside a list element would split it
    list(GET parts 3 want)
    region("${${file}}" "${start}" "${stop}" body)
    string(SHA256 h "${body}")
    string(SUBSTRING "${h}" 0 16 h)
    if(NOT h STREQUAL want)
      set(${out} "line format changed (evaluator 5a673e15 reads it byte-identical): ${start} sha ${h} != ${want}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The console line has its own tag, never one the evaluator reads.
  string(REGEX MATCH "\"\\[(Supervisor|ArbiterMin|BotUpdate|BotUpdateCohort)\\] (rejected|usage|arbiter=)" bad "${mgr}")
  if(NOT bad STREQUAL "")
    set(${out} "the console line uses an evaluator tag: ${bad}" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) unit test ------------------------------------------------------------------------------------------------------------
function(check_unit unit_test out)
  foreach(needle "void TestArbiterConsole()" "    TestArbiterConsole();" "ParseArbiterSwitch(\"2\") == kArbiterFollowConfig"
      "ArbiterEffective(kArbiterOn, 0) && !ArbiterEffective(kArbiterOff, 1)")
    string(FIND "${unit_test}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "t/supervisor_policy_tests.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the real sources -----------------------------------------------------------------------------------------------------
check_registration("${mgr}" r)
fail_if("${r}")
check_switch("${shadow_h}" "${shadow_cpp}" "${policy}" "${ai_cpp}" "${config_h}" "${all_sources}" r)
fail_if("${r}")
check_no_initialize("${mgr}" "${all_sources}" r)
fail_if("${r}")
check_formats("${policy}" "${shadow_cpp}" "${mgr}" r)
fail_if("${r}")
check_unit("${unit_test}" r)
fail_if("${r}")

# ---- negative probes ------------------------------------------------------------------------------------------------------
# (a) the remote-admin refusal dropped.
string(REPLACE "        if (isRA)\n        {\n            sLog.outError(\"[ArbiterCtl] rejected" "        if (false)\n        {\n            sLog.outError(\"[ArbiterCtl] rejected" m "${mgr}")
check_registration("${m}" r)
expect_caught("console-only refusal dropped" "${r}")
# (a) registered as a handler-map entry instead.
check_registration("${mgr}\n    handlers[\"arbiter\"] = &RandomPlayerbotMgr::HandleConsoleArbiter;" r)
expect_caught("arbiter in the handler map" "${r}")
# (a) the full reload registered.
check_registration("${mgr}\n    handlers[\"reload\"] = &RandomPlayerbotMgr::HandleConsoleReload;" r)
expect_caught("reload registered" "${r}")
# (b) a plain bool instead of the atomic.
string(REPLACE "std::atomic<int> g_arbiterOverride{kArbiterFollowConfig};" "int g_arbiterOverride = kArbiterFollowConfig;" m "${shadow_cpp}")
check_switch("${shadow_h}" "${m}" "${policy}" "${ai_cpp}" "${config_h}" "${all_sources}" r)
expect_caught("override not atomic" "${r}")
# (b) the gate reads the config directly again (console without effect).
string(REPLACE "ai::supervisor::ArbiterOn(sPlayerbotAIConfig.supervisorArbiter));" "sPlayerbotAIConfig.supervisorArbiter == 1);" m "${ai_cpp}")
check_switch("${shadow_h}" "${shadow_cpp}" "${policy}" "${m}" "${config_h}" "${all_sources}" r)
expect_caught("gate ignores the override" "${r}")
# (b) a second reader or writer.
check_switch("${shadow_h}" "${shadow_cpp}" "${policy}" "${ai_cpp}" "${config_h}" "${all_sources}\n    if (ai::supervisor::ArbiterOn(0))" r)
expect_caught("a second reader" "${r}")
check_switch("${shadow_h}" "${shadow_cpp}" "${policy}" "${ai_cpp}" "${config_h}" "${all_sources}\n    ai::supervisor::SetArbiterOverride(true);" r)
expect_caught("a second writer" "${r}")
# (b) the override ignored: config wins.
string(REPLACE "    if (override == kArbiterOn)\n        return true;\n" "" m "${policy}")
check_switch("${shadow_h}" "${shadow_cpp}" "${m}" "${ai_cpp}" "${config_h}" "${all_sources}" r)
expect_caught("override on ignored" "${r}")
# (c) the branch reloads the whole config.
string(REPLACE "        int const prev = ai::supervisor::SetArbiterOverride(" "        sPlayerbotAIConfig.Initialize();\n        int const prev = ai::supervisor::SetArbiterOverride(" m "${mgr}")
check_no_initialize("${m}" "${all_sources}" r)
expect_caught("Initialize in the branch" "${r}")
# (c) the branch writes the config field.
string(REPLACE "        int const prev = ai::supervisor::SetArbiterOverride(" "        sPlayerbotAIConfig.supervisorArbiter = uint32(value);\n        int const prev = ai::supervisor::SetArbiterOverride(" m "${mgr}")
check_no_initialize("${m}" "${all_sources}" r)
expect_caught("config field written" "${r}")
# (c) a new Initialize() call elsewhere.
check_no_initialize("${mgr}" "${all_sources}\n    sPlayerbotAIConfig.Initialize();" r)
expect_caught("a 4th Initialize call" "${r}")
# (d) one byte changed in each kind of format region.
string(REPLACE "\"[ArbiterMin] cohort=%c bundles=%u lines=%u" "\"[ArbiterMin] cohort=%c bundles=%u  lines=%u" m "${policy}")
check_formats("${m}" "${shadow_cpp}" "${mgr}" r)
expect_caught("[ArbiterMin] format" "${r}")
string(REPLACE "\"[Supervisor] bot=%u cohort=%c arbiter=1 ev=write" "\"[Supervisor] bot=%u cohort=%c arbiter=%u ev=write" m "${policy}")
check_formats("${m}" "${shadow_cpp}" "${mgr}" r)
expect_caught("[Supervisor] write format" "${r}")
string(REPLACE "        FormatArbiterMin(buf, sizeof(buf), i ? 'B' : 'A', minute.sums[i]);\n        sLog.outBasic(\"%s\", buf);" "        FormatArbiterMin(buf, sizeof(buf), i ? 'B' : 'A', minute.sums[i]);\n        sLog.outError(\"%s\", buf);" m "${shadow_cpp}")
check_formats("${policy}" "${m}" "${mgr}" r)
expect_caught("[ArbiterMin] emitted with another log prefix" "${r}")
string(REPLACE "[BotUpdateCohort] cohort=%c calls=%llu sum_us=%llu" "[BotUpdateCohort] cohort=%c calls=%llu sum_us=%llu " m "${mgr}")
check_formats("${policy}" "${shadow_cpp}" "${m}" r)
expect_caught("[BotUpdateCohort] format" "${r}")
string(REPLACE "sLog.outString(\"[ArbiterCtl] arbiter=%d" "sLog.outString(\"[Supervisor] arbiter=%d" m "${mgr}")
check_formats("${policy}" "${shadow_cpp}" "${m}" r)
expect_caught("console line with the [Supervisor] tag" "${r}")
# (e) the unit test dropped from main.
string(REPLACE "    TestArbiterConsole();" "" m "${unit_test}")
check_unit("${m}" r)
expect_caught("unit test not run" "${r}")

message(STATUS "A3S3_ARBITER_CONSOLE_SOURCE_CONTRACT=PASS")
