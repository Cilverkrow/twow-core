# CLI-GRP 28.10.2026 (Refs twow-repo#629; OB-00 go (b), owner: "tanks in dungeons have to perform"): the test-run record's
# tank measures (schema 13) - aggro loss per run and per pull, the engagement start per boss kill, the tank strategy per
# member. Descriptive in G1 phase D, the base of the G2 tank comparison. Rules pure (DcAggroTally.h, gtest
# TestTestRunRecord); this pins the wiring in the monitor.

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
    message(FATAL_ERROR "tank measures: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

read_dc("src/TestRun/DcTestRunJob.cpp" job)
read_dc("src/TestRun/DcTestRunRecord.h" rec_h)

# Aggro loss: an attacker on a member other than the run's tank, inside TrackEngagement's scan, then the tally.
require_order("${job}" "void DcTestRunJob::TrackEngagement(Player* tank)" "            if (member != tank && c && !c->IsCritter() && !c->IsTotem())
                nonTankAttacked = true;" "loss = a creature on a non-tank member")
require_order("${job}" "    _engaged = DcTestRun::UpdateEngagement(_engaged, sample);" "    _aggro.Observe(sample.anyAliveInCombat, nonTankAttacked);" "tally after the sample")
require_order("${job}" "    if (_pullOpen && sample.anyAliveInCombat)" "            ++_pullEntry.aggroLossTicks;" "per pull")
# Boss engagement start: a new boss in the sample starts an engagement; a kill takes it.
require_order("${job}" "    if (sample.bossEntry && sample.bossEntry != _prevBossEntry)" "        _bossEngageS[sample.bossEntry] = _totalMs / 1000;" "engagement start")
require_order("${job}" "                    kill.engageT = eng->second;" "                _record.bossTimeline.push_back(kill);" "kill carries its engagement start")
# Tank strategy, read once in the monitor.
require_order("${job}" "    if (!_tankStrategyRead)" "c.tankStrategy = ai->HasStrategy(\"tank\", BotState::BOT_STATE_COMBAT);" "tank strategy per member")
string(FIND "${rec_h}" "std::uint32_t schema = 13;" s13)
if(s13 EQUAL -1)
  message(FATAL_ERROR "tank measures: record schema is not 13")
endif()
