# CLI-GRP 29.10.2026 (g2/CAUSE-DRUIDE-FERAL-SITZ, OB-00 go; Refs twow-repo#629): the druid tank's bear gate asked
# HasStrategy("bear"), a NAME the engine never holds (it keys strategies by getName(); "bear" and "tank feral" build the
# same class, getName() "tank feral"), so the druid tank pulled in caster form. Now the TYPE. The druid comes back as a
# forced-only seat (tank=druid), never drawn. Rules pure (gtests TestTestComp / TestTestPlanSchedule).

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
    message(FATAL_ERROR "druid bear: missing ${description}: ${needle}")
  endif()
endfunction()

read_dc("src/Ai/Dungeon/DungeonClear/Util/DcTankForm.cpp" form)
read_dc("src/TestRun/DcTestComp.h" comp)

require_text("${form}" "        if (!botAI->ContainsStrategy(ai::STRATEGY_TYPE_TANK))" "bear gate by strategy type")
string(FIND "${form}" "HasStrategy(\"bear\"" byname)
if(NOT byname EQUAL -1)
  message(FATAL_ERROR "druid bear: the gate asks the name \"bear\" again - the engine never holds it")
endif()
require_text("${comp}" "    inline constexpr Slot kForcedOnlyTankPool[] = {
        { 11, \"bear\",       \"feral\", \"tank\" },  // druid" "druid as a forced-only seat")
require_text("${comp}" "        // { 11, \"feral\",      \"feral\", \"tank\" },  // druid" "druid still out of the drawn tank pool")
