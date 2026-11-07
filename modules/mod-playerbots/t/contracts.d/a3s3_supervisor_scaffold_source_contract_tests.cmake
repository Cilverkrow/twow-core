cmake_policy(VERSION 3.16)
# A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026): the per-bot supervisor and planner scaffold,
# AiPlayerbot.Supervisor (0 off = default, 1 all, 2 cohort B, 3 cohort A; other = 0 + error line). Design
# ws-00/projekt-durchleuchtung/A3-S3-ENTWURF.md (c98e6ded), section 5. The scaffold decides nothing. This contract pins that:
#   - the switch: member default 0, key read with default 0 and clamped by supervisor::ValidMode, documented = 0, echoed;
#   - one tick: PlayerbotAI::UpdateAI, in the engine pass (after the countdown and the reaction gate), gated by the switch and
#     its cohort; the else branch releases and drops the instance (reload 1 -> 0); no other ->Tick( in the module;
#   - the pure headers (SupervisorPolicy.h, PlannerPolicy.h) name no game header and read no config; <cstdio> only in
#     SupervisorPolicy.h, for std::snprintf in the two pure line formatters (decision F2 a: no log line in the scaffold);
#   - the supervisor writes nothing of the bot: no travel-target writer, no delay setter, no strategy change, no teleport, no
#     log call; Tick is the bare counter; the mailbox is declared only and nothing calls it; ~Supervisor is out of line.
# The pair that gives the supervisor its first behaviour changes this check on purpose, together with its own A/B.
# Prepared ratchet (NOT armed here): the count of direct travel-target writer calls (SetTarget|SetStatus|SetNullTravelTarget)
# in the module is printed; the writer-deletion pairs (step 8) arm it at "<= the count of their base".
# Pure rules: t/supervisor_policy_tests.cpp, t/planner_policy_tests.cpp. Every check_* returns "" when it holds, else the
# reason; the negative probes at the end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s3: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s3: PB_MODULE_DIR is required")
endif()

set(TAG "a3s3 supervisor scaffold")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("SupervisorPolicy.h" policy)
read_source("PlannerPolicy.h" planner)
read_source("Supervisor.h" sup_h)
read_source("Supervisor.cpp" sup_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
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
function(check_switch config_h config_cpp conf_dist out)
  string(FIND "${config_h}" "uint32 supervisor = 0;" at)
  if(at EQUAL -1)
    set(${out} "PlayerbotAIConfig.h: member not default 0: uint32 supervisor = 0;" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "supervisor = uint32(config.GetIntDefault(\"AiPlayerbot.Supervisor\", 0));"
      "if (!ai::supervisor::ValidMode(supervisor))"
      "        supervisor = 0;"
      "            {\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)},\n            {\"Supervisor\", Value(supervisor)},\n            {\"ThreadSafeTeleportAck\"")
    string(FIND "${config_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAIConfig.cpp: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "\nAiPlayerbot\\.Supervisor = [^\n]*" lines "${conf_dist}")
  list(LENGTH lines n)
  if(NOT n EQUAL 1)
    set(${out} "conf.dist: AiPlayerbot.Supervisor documented ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  if(NOT lines STREQUAL "\nAiPlayerbot.Supervisor = 0")
    set(${out} "conf.dist: AiPlayerbot.Supervisor is not 0" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the one tick --------------------------------------------------------------------------------------------------------
set(SCPHASE "        SC_PHASE(\"UpdateAI.UpdateAIInternal\", bot ? bot->GetName() : \"(null)\");")
set(GATE "        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, bot->GetGUIDLow()))\n            EnsureSupervisor()->Tick(WorldTimer::getMSTime());\n        else if (supervisor_)\n        {\n            supervisor_->Release();\n            supervisor_.reset();\n        }\n\n${SCPHASE}")
set(COUNTDOWN "        aiInternalUpdateDelay = 0;\n        isWaiting = false;\n    }")
set(REACTION "if(!UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()) && CanUpdateAIInternal())")
set(PASS "        UpdateAIInternal(elapsed, minimal);")

function(check_tick ai_cpp ai_h all_sources out)
  string(FIND "${ai_cpp}" "${GATE}" gate)
  if(gate EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the gated tick with the release branch right before the UpdateAIInternal phase is missing" PARENT_SCOPE)
    return()
  endif()
  # The engine pass follows the tick with no return in between (the tick counts passes that run).
  string(LENGTH "${GATE}" gate_len)
  math(EXPR after_gate "${gate} + ${gate_len}")
  string(SUBSTRING "${ai_cpp}" ${after_gate} -1 rest)
  string(FIND "${rest}" "${PASS}" pass)
  if(pass EQUAL -1)
    set(${out} "PlayerbotAI.cpp: no UpdateAIInternal after the tick" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${pass} between)
  string(FIND "${between}" "return" ret)
  if(NOT ret EQUAL -1)
    set(${out} "PlayerbotAI.cpp: a return between the tick and UpdateAIInternal" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${ai_cpp}" "${COUNTDOWN}" countdown)
  string(FIND "${ai_cpp}" "${REACTION}" reaction)
  if(countdown EQUAL -1 OR reaction EQUAL -1)
    set(${out} "PlayerbotAI.cpp: the countdown or the reaction gate anchor is gone" PARENT_SCOPE)
    return()
  endif()
  if(NOT countdown LESS reaction OR NOT reaction LESS gate)
    set(${out} "PlayerbotAI.cpp: the tick is not after the countdown and inside the engine pass" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "->Tick\\(" n)
  if(NOT n EQUAL 1)
    set(${out} "->Tick( appears ${n} times in the module (1 expected)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "EnsureSupervisor\\(\\)->" n)
  if(NOT n EQUAL 1)
    set(${out} "EnsureSupervisor()-> used ${n} times (1 expected)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${all_sources}" "make_unique<ai::supervisor::Supervisor>" n)
  if(NOT n EQUAL 1)
    set(${out} "a Supervisor is made in ${n} places (1 expected: EnsureSupervisor)" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "namespace ai::supervisor { class Supervisor; }" "std::unique_ptr<ai::supervisor::Supervisor> supervisor_;")
    string(FIND "${ai_h}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "PlayerbotAI.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the pure headers ----------------------------------------------------------------------------------------------------
function(check_pure name text allowed out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${text}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "${allowed}")
      set(${out} "${name}: not a pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Group*" "PlayerbotAI" "std::mutex" "WorldTimer")
    string(FIND "${text}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "${name}: names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()
set(POLICY_INCLUDES "^#include (\"CohortPolicy\\.h\"|<cstddef>|<cstdint>|<cstdio>)$")
set(PLANNER_INCLUDES "^#include (<cstddef>|<cstdint>)$")

# ---- no behaviour yet ----------------------------------------------------------------------------------------------------
function(check_no_behaviour sup_h sup_cpp all_sources out)
  set(code "${sup_h}\n${sup_cpp}")
  foreach(word
      "SetTarget" "SetStatus" "SetForced" "SetExpireIn" "SetConditions" "SetNullTravelTarget" "SetAIInternalUpdateDelay"
      "aiInternalUpdateDelay" "ChangeStrategy" "ResetStrategies" "TeleportTo" "DoSpecificAction" "GetAiObjectContext"
      "sPlayerbotAIConfig" "sLog" "\"travel target\"")
    string(FIND "${code}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "Supervisor: scaffold must not use ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${sup_cpp}" "void Supervisor::Tick(std::uint32_t /*nowMs*/)\n{\n    ++ticks_;\n}" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: the scaffold tick is not the bare counter" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_cpp}" "void Supervisor::Release()\n{\n}" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: the scaffold release is not empty" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_cpp}" "Supervisor::~Supervisor() = default;" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.cpp: ~Supervisor is not defined out of line" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${sup_h}" "    ~Supervisor();" at)
  if(at EQUAL -1)
    set(${out} "Supervisor.h: ~Supervisor must be declared only (unique_ptr to the incomplete type in PlayerbotAI.h)" PARENT_SCOPE)
    return()
  endif()
  # The mailbox is declared only: nobody calls or defines it in this scaffold.
  count_regex("${all_sources}" "supervisor::(Post|Drain|Forget)\\(" n)
  if(NOT n EQUAL 0)
    set(${out} "the mailbox is called ${n} times (0 expected in the scaffold)" PARENT_SCOPE)
    return()
  endif()
  count_regex("${sup_cpp}" "(Post|Drain|Forget)\\(" n)
  if(NOT n EQUAL 0)
    set(${out} "Supervisor.cpp: the mailbox is wired (declared only in the scaffold)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- the unit tests are registered ---------------------------------------------------------------------------------------
function(check_unit_tests tests_cmake out)
  foreach(needle
      "add_executable(supervisor_policy_tests" "add_test(NAME supervisor_policy\n  COMMAND supervisor_policy_tests"
      "add_executable(planner_policy_tests" "add_test(NAME planner_policy\n  COMMAND planner_policy_tests")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "tests.cmake: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- run on the real sources ---------------------------------------------------------------------------------------------
check_switch("${config_h}" "${config_cpp}" "${conf_dist}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_tick("${ai_cpp}" "${ai_h}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_pure("SupervisorPolicy.h" "${policy}" "${POLICY_INCLUDES}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_pure("PlannerPolicy.h" "${planner}" "${PLANNER_INCLUDES}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_no_behaviour("${sup_h}" "${sup_cpp}" "${all_sources}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()
check_unit_tests("${tests_cmake}" r)
if(NOT r STREQUAL "")
  message(FATAL_ERROR "${TAG}: ${r}")
endif()

# ---- prepared ratchet (not armed) ----------------------------------------------------------------------------------------
count_regex("${all_sources}" "(SetTarget|SetStatus|SetNullTravelTarget)\\(" writers)
message(STATUS "${TAG}: direct travel-target writer calls in the module: ${writers} (ratchet prepared, not armed)")

# ---- negative probes: each mutation must be caught -----------------------------------------------------------------------
function(expect_caught result description)
  if(result STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${description}")
  endif()
endfunction()

string(REPLACE "AiPlayerbot.Supervisor = 0" "AiPlayerbot.Supervisor = 1" m "${conf_dist}")
check_switch("${config_h}" "${config_cpp}" "${m}" r)
expect_caught("${r}" "conf.dist default 1")

string(REPLACE "uint32 supervisor = 0;" "uint32 supervisor = 1;" m "${config_h}")
check_switch("${m}" "${config_cpp}" "${conf_dist}" r)
expect_caught("${r}" "member default 1")

string(REPLACE "\"AiPlayerbot.Supervisor\", 0)" "\"AiPlayerbot.Supervisor\", 1)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "loader default 1")

string(REPLACE "if (!ai::supervisor::ValidMode(supervisor))" "if (false)" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "no mode clamp")

string(REPLACE "            {\"Supervisor\", Value(supervisor)},\n" "" m "${config_cpp}")
check_switch("${config_h}" "${m}" "${conf_dist}" r)
expect_caught("${r}" "not in [ConfigEcho]")

string(REPLACE "        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, bot->GetGUIDLow()))\n" "        if (true)\n" m "${ai_cpp}")
check_tick("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "ungated tick")

string(REPLACE "        else if (supervisor_)\n        {\n            supervisor_->Release();\n            supervisor_.reset();\n        }\n" "" m "${ai_cpp}")
check_tick("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "no release on reload 1 -> 0")

string(REPLACE "${GATE}" "${SCPHASE}" m "${ai_cpp}")
string(REPLACE "${COUNTDOWN}" "${COUNTDOWN}\n${GATE}" m "${m}")
check_tick("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "tick before the reaction gate (every world tick)")

string(REPLACE "${COUNTDOWN}" "    }" m "${ai_cpp}")
string(REPLACE "${GATE}" "${GATE}\n${COUNTDOWN}" m "${m}")
check_tick("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "tick before the countdown")

string(REPLACE "${GATE}" "${GATE}\n        if (!bot)\n            return;" m "${ai_cpp}")
check_tick("${m}" "${ai_h}" "${all_sources}" r)
expect_caught("${r}" "a return between the tick and the engine pass")

check_tick("${ai_cpp}" "${ai_h}" "${all_sources}\nsupervisor_->Tick(0)" r)
expect_caught("${r}" "second tick")

check_tick("${ai_cpp}" "${ai_h}" "${all_sources}\nauto s = std::make_unique<ai::supervisor::Supervisor>(ai)" r)
expect_caught("${r}" "a second place makes a Supervisor")

check_pure("SupervisorPolicy.h" "${policy}\n#include \"playerbot/playerbot.h\"" "${POLICY_INCLUDES}" r)
expect_caught("${r}" "game include in the supervisor policy")

check_pure("SupervisorPolicy.h" "${policy}\nbool x = sPlayerbotAIConfig.enabled" "${POLICY_INCLUDES}" r)
expect_caught("${r}" "config read in the supervisor policy")

check_pure("PlannerPolicy.h" "${planner}\n#include <cstdio>" "${PLANNER_INCLUDES}" r)
expect_caught("${r}" "<cstdio> in the planner policy")

check_pure("PlannerPolicy.h" "${planner}\n#include <algorithm>" "${PLANNER_INCLUDES}" r)
expect_caught("${r}" "a std include beyond cstddef/cstdint in the planner policy")

string(REPLACE "    ++ticks_;\n" "    ++ticks_;\n    target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);\n" m "${sup_cpp}")
check_no_behaviour("${sup_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "a travel-target writer in the supervisor")

string(REPLACE "    ++ticks_;\n" "    ++ticks_;\n    ai_->SetAIInternalUpdateDelay(1000);\n" m "${sup_cpp}")
check_no_behaviour("${sup_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "a delay setter in the supervisor")

string(REPLACE "    ++ticks_;\n" "    ++ticks_;\n    sLog.outString(\"[SupervisorMin]\");\n" m "${sup_cpp}")
check_no_behaviour("${sup_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "a log line in the scaffold")

string(REPLACE "    ++ticks_;\n" "    ticks_ += 2;\n" m "${sup_cpp}")
check_no_behaviour("${sup_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "the tick is not the bare counter")

string(REPLACE "Supervisor::~Supervisor() = default;" "" m "${sup_cpp}")
check_no_behaviour("${sup_h}" "${m}" "${all_sources}" r)
expect_caught("${r}" "no out-of-line destructor")

check_no_behaviour("${sup_h}" "${sup_cpp}" "${all_sources}\nai::supervisor::Post(guid, ev)" r)
expect_caught("${r}" "a mailbox call in the scaffold")

string(REPLACE "add_test(NAME supervisor_policy\n" "add_test(NAME other\n" m "${tests_cmake}")
check_unit_tests("${m}" r)
expect_caught("${r}" "supervisor unit test not registered")

string(REPLACE "add_executable(planner_policy_tests" "add_executable(other_tests" m "${tests_cmake}")
check_unit_tests("${m}" r)
expect_caught("${r}" "planner unit test not built")

message(STATUS "${TAG}: OK")
