# CLI-GRP G1 probe 28.10.2026 (Refs twow-repo#629): with AiPlayerbot.PersistentActiveRoster.Enabled=1 the random-bot holder
# refused the test driver's and the pool's masterless logins ("[PersistentRoster] AddPlayerBot fail-closed"); the ignored
# result left the driver "logging in" until every plan aborted after 2 min ("no issuing GM"). Now both say it at once.
# The text is pure (DcTestPool::LoginRefusal, gtest TestTestPoolPolicy); this pins the wiring.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_dc path out_var)
  file(READ "${DC_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "login refused: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_dc("src/TestRun/DcTestDriver.cpp" driver)
read_dc("src/TestRun/DcTestRunJob.cpp" job)

# Driver: the result decides; a refusal is Unavailable before the login is latched.
require_order("${driver}" "            if (!sRandomPlayerbotMgr.AddPlayerBot(_guid, 0))" "                    *why = refusal;" "driver refusal is a standing refusal")
require_order("${driver}" "                    *why = refusal;
                return Readiness::Unavailable;
            }
            _loginIssued = true;" "TESTDRIVER logging in" "latched only after an accepted login")
# Pool: every slot's result recorded; the spawning stage fails on it first.
require_order("${job}" "        if (!sRandomPlayerbotMgr.AddPlayerBot(slot.guid, 0))" "            job->_loginRefused +=" "pool refusal recorded")
require_order("${job}" "void DcTestRunJob::TickSpawning()
{
    if (!_loginRefused.empty())" "        FailSetup(DcTestPool::LoginRefusal(\"pool characters (\" + _loginRefused + \")\"," "spawning ends on a refusal first")
string(FIND "${driver}" "            sRandomPlayerbotMgr.AddPlayerBot(_guid, 0);" ignored)
if(NOT ignored EQUAL -1)
  message(FATAL_ERROR "login refused: the driver ignores AddPlayerBot's result again")
endif()
