# CLI-GRP 30.10.2026 (OB-00; Refs twow-repo#654, the rogue tank A/B of CLI-ROT): the rogue tank on its premade path 4.3 as a
# forced-only test seat (tank=rogue, never drawn), and its path kit (Spit, Shadow Dance) for that seat from the claim -
# GrantSpecKitFor, OB-00's method decision for the test harness only. The world path stays as it was: LearnSpells keeps
# the persistent-roster gate in front of GrantSpecAuras, and nothing in mod-playerbots calls the new entry. Rules pure
# (gtests TestTestComp / TestTestPlanSchedule).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

set(PB_DIR "${DC_SOURCE_DIR}/../mod-playerbots")

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "rogue tank seat: missing ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nlen)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR from "${at} + ${nlen}")
    string(SUBSTRING "${rest}" ${from} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "rogue tank seat: expected ${expected}x ${description}, found ${n}")
  endif()
endfunction()

read_file("${DC_SOURCE_DIR}/src/TestRun/DcTestComp.h" comp)
read_file("${DC_SOURCE_DIR}/src/TestRun/DcTestComp.cpp" comp_cpp)
read_file("${DC_SOURCE_DIR}/src/TestRun/DcTestRunJob.cpp" job)
read_file("${PB_DIR}/src/playerbot/strategy/actions/AutoLearnSpellAction.cpp" learn)
read_file("${PB_DIR}/src/playerbot/strategy/actions/AutoLearnSpellAction.h" learn_h)

# 1. The seat: forced only, on the premade path "rogue tank" (PremadeSpecName.4.3), never in the drawn tank pool.
require_text("${comp}" "    inline constexpr Slot kForcedOnlyTankPool[] = {
        { 11, \"bear\",       \"feral\", \"tank\" },  // druid
        { 4,  \"rogue tank\", \"tank\",  \"tank\" },  // rogue" "rogue as a forced-only seat")
string(FIND "${comp}" "    inline constexpr Slot kTankPool[] = {" tank_at)
string(SUBSTRING "${comp}" ${tank_at} -1 tank_rest)
string(FIND "${tank_rest}" "    };" tank_end)
string(SUBSTRING "${tank_rest}" 0 ${tank_end} tank_pool)
string(FIND "${tank_pool}" "{ 4," drawn_rogue)
if(NOT drawn_rogue EQUAL -1)
  message(FATAL_ERROR "rogue tank seat: the rogue is in the drawn kTankPool - every seed's comp would change")
endif()
require_text("${comp_cpp}" "{ \"rogue\", 4 }" "tank=rogue by name")

# 2. The claim gives the seat its path kit: once, right after the premade talents, inside the forced-spec block.
count_text("${job}" "ai::GrantSpecKitFor(" 1 "kit grant in the claim")
require_text("${job}" "        GetBotAI(bot)->DoSpecificAction(\"auto talents\");
        // CLI-GRP 30.10.2026 (OB-00 method decision, test harness only)" "kit grant right after the premade talents")
require_text("${job}" "        ai::GrantSpecKitFor(GetBotAI(bot));
        factory.EquipGear();" "kit grant before the gear")

# 3. The world path is untouched (OB-00): LearnSpells still grants the kit only to persistent roster members, the test-seat
#    entry runs the same GrantSpecAuras behind its own checks and logs source=dc_seat, and no mod-playerbots code calls it.
require_text("${learn}" "    if (sPlayerbotAIConfig.specAuraEnabled &&
        (!ai::spec_aura::AurasFor(bot->getClass()).empty() || !ai::spec_aura::KitFor(bot->getClass()).empty()) &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        GrantSpecAuras();" "roster gate in LearnSpells")
require_text("${learn}" "    std::vector<uint32> const kit = path ? ai::spec_aura::WantedKit(bot->getClass(), path, bot->GetLevel()) : std::vector<uint32>();
    if (kit.empty())
        return false;" "test-seat entry: only a seat whose path has a kit at its level")
require_text("${learn}" "reason=spec_aura_off" "test-seat entry: a skip line when SpecAura is off")
require_text("${learn}" "    uint32 const before = knownKit();
    GrantSpecAuras();
    uint32 const after = knownKit();" "test-seat entry: the same GrantSpecAuras, counted")
require_text("${learn}" "[ClassGrant] state=%s source=dc_seat" "test-seat entry: dc_seat log")
count_text("${learn}" "GrantSpecKitForTestSeat()" 2 "test-seat entry in AutoLearnSpellAction.cpp (definition + the one call from GrantSpecKitFor)")
count_text("${learn_h}" "bool GrantSpecKitFor(PlayerbotAI* ai);" 1 "free entry declaration")
file(GLOB_RECURSE pb_sources "${PB_DIR}/src/*.cpp" "${PB_DIR}/src/*.h")
foreach(src IN LISTS pb_sources)
  get_filename_component(name "${src}" NAME)
  if(name STREQUAL "AutoLearnSpellAction.cpp" OR name STREQUAL "AutoLearnSpellAction.h")
    continue()
  endif()
  file(READ "${src}" text)
  string(FIND "${text}" "GrantSpecKitFor" hit)
  if(NOT hit EQUAL -1)
    message(FATAL_ERROR "rogue tank seat: ${src} uses GrantSpecKitFor - the kit grant without the roster is for the dungeon-clear test claim only")
  endif()
endforeach()

message(STATUS "rogue tank seat contract passed")
