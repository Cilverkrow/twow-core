# G1 dry 25.10.2026 (CLI-GRP, Refs twow-repo#629): the first `.dc test plan start` after a reset creates the test driver, and
# both provisioning steps that land through the async DB queue were reported as Unavailable - "account ... still being
# written" and, on the next attempt, "character 'Dcdriver' not found" while the new character's save was still queued - so
# HandleTestPlanStart refused the plan, although the scheduler waits out a PendingLogin. Both are a login in flight now.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

file(READ "${DC_SOURCE_DIR}/src/TestRun/DcTestDriver.cpp" driver)
string(REPLACE "\r" "" driver "${driver}")

function(require_text needle description)
  string(FIND "${driver}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "test driver pending: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order first second description)
  string(FIND "${driver}" "${first}" a)
  string(FIND "${driver}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "test driver pending: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

# A character still being written: pending before the one-attempt latch replays a refusal.
require_order("            if (_awaitingFlush)
            {
                if (why)
                    *why = \"test driver '\" + name + \"' was just created and is \""
              "            if (_provisionTried)" "flush check before the refusal replay")
require_text("                    return Readiness::PendingLogin;
                }
                if (why)
                    *why = ManualSetup(name);" "the just-created character is pending, only a real miss is the manual recipe")
# An account still being written: pending, and not latched as the refusal.
require_text("                    _accountPending = true;" "the account branch marks itself")
require_order("                if (_accountPending)
                {
                    _accountPending = false;
                    return Readiness::PendingLogin;
                }"
              "                _provisionRefusal = provisionWhy;
                return Readiness::Unavailable;" "account pending before the refusal")
require_text("                _accountPending = false;" "a renamed driver starts clean")
