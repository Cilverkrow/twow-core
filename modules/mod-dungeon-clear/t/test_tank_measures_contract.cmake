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

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "tank measures: missing ${description}: ${needle}")
  endif()
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
require_order("${job}" "void DcTestRunJob::TrackEngagement(Player* tank)" "            if (member->GetObjectGuid() != _aggroTankGuid && c && !c->IsCritter() && !c->IsTotem())
                nonTankAttacked = true;" "loss = a creature on a non-tank member")
require_order("${job}" "    _engaged = DcTestRun::UpdateEngagement(_engaged, sample);" "    _aggro.Observe(sample.anyAliveInCombat, nonTankAttacked, tankUp);" "tally after the sample, with the tank state")
# Pre-review M2: the tank seat is the slot with role "tank"; M1: a tank down is never loss.
require_text("${job}" "            if (slot.role && std::string_view(slot.role) == \"tank\")" "tank seat by role")
require_text("${job}" "    bool const tankUp = seat && seat->IsAlive() && seat->IsInWorld() && seat->GetMapId() == tank->GetMapId();" "tank up")
require_order("${job}" "    if (_pullOpen && sample.anyAliveInCombat)" "            ++_pullEntry.aggroLossTicks;" "per pull")
# Boss engagement start: a new boss in the sample starts an engagement; a kill takes it.
require_order("${job}" "        if (seen == _bossLastSeenS.end() || nowS - seen->second > kEngageGraceS)" "            _bossEngageS[entry] = nowS;" "engagement start per boss with a grace (M3/m1)")
require_order("${job}" "                    if (ref.encounterIndex == bit && ref.isBoss)" "                            kill.engageT = eng->second;" "kill takes the earliest start of its encounter")
# Tank strategy, read once in the monitor.
require_order("${job}" "    if (!_tankStrategyRead)" "c.tankStrategy = ai->ContainsStrategy(ai::STRATEGY_TYPE_TANK);" "tank strategy by TYPE (B1: names differ per class)")
string(FIND "${job}" "HasStrategy(\"tank\"" byname)
if(NOT byname EQUAL -1)
  message(FATAL_ERROR "tank measures: tankStrategy by the name \"tank\" again - no class strategy is called that (pre-review B1)")
endif()
string(FIND "${rec_h}" "std::uint32_t schema = 13;" s13)
if(s13 EQUAL -1)
  message(FATAL_ERROR "tank measures: record schema is not 13")
endif()
