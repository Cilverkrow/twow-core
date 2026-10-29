# CLI-GRP G2 (OB-00 28.10.2026, warrior-prot vs paladin-prot; Refs twow-repo#629): `.dc test plan start ... tank=<class>`
# forces every child run's tank seat. Rules pure (DcTestComp::BuildComp/TankClassByName, gtests TestTestComp /
# TestTestPlanSchedule); this pins the wiring from the plan to the comp and that the forced seat is never substituted.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "plan tank: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "plan tank: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_dc("src/TestRun/DcTestPlan.cpp" plan)
read_dc("src/TestRun/DcTestPlanManager.cpp" mgr)
read_dc("src/TestRun/DcTestRunManager.cpp" runmgr)
read_dc("src/TestRun/DcTestRunJob.cpp" job)

require_text("${plan}" "                out.spec.tankClass = DcTestComp::TankClassByName(val);" "parse tank=")
require_text("${mgr}" "&err, &runId, plan.spec.tankClass);" "the plan passes its tank class to every child")
require_text("${runmgr}" "_reservedGuids, planId, &err, tankClass);" "Start passes it to Create")
require_text("${job}" "DcTestComp::BuildComp(seed, tankClass);" "Create builds the comp with it")
# The forced seat fails its set-up instead of the role-pool substitution.
require_order("${job}" "        if (!guid && tankClass && &slot == &job->_slots.front())" "            for (DcTestComp::Slot const& alt : DcTestComp::RolePool(slot.role))" "no substitution on a forced tank seat")
