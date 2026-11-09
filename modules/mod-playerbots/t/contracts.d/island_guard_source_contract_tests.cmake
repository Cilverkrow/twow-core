cmake_policy(VERSION 3.16)
# twow-repo#575 A3 step 4 M-c (OB-00 06.11.2026; evidence ws-10/cli-pathfinding/a3-step4-route/count-mc-ma/
# RESULT-COUNT-MC-MA-20261106.md, plan PLAN-M-c-20261106.md; CLI-PATH): AiPlayerbot.Path.IslandGuard (cohort modes, default 0).
# The far-move arrival point (ClosestCorrectPoint, no component check) put bots onto navmesh micro-islands; with the key the
# point moves to main ground, and a bot already on an island steps off it before a route is built. Pins: the hook order in
# FarMoveArrivalPoint and MoveTo2, the defaults, the log fields, the module-only scope, and the thread model of the shared
# tile label cache (OB-00: read and filled by several map threads - publish-once, immutable values, compute outside the lock).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "island guard: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "island guard: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "island guard: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "island guard: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "island guard: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "island guard: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

# Counts the plain-text occurrences of needle in text (no regex).
function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_length)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${at} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("IslandGuardPolicy.h" policy)
read_source("IslandGuard.h" guard_h)
read_source("IslandGuard.cpp" guard)
read_source("strategy/actions/MovementActions.cpp" movement)
read_source("strategy/values/LastMovementValue.h" last_movement)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("WorldPosition.cpp" world_position)

# 0. The movement TU includes the guard header (CI run 42279661623: without it ai::island is undeclared; host contracts read text only).
require_text("${movement}" "#include \"playerbot/IslandGuard.h\"" "MovementActions.cpp includes IslandGuard.h")

# 1. Defaults: on (A/B ACCEPT 09.11.2026), threshold 200, dz 10, step-off 7 - in the member, the loader and the .dist.
require_text("${config_h}" "uint32 pathIslandGuard = 1;" "switch member default 1 (IslandGuard A/B ACCEPT 09.11.2026; 0 = rollback)")
require_text("${config_h}" "uint32 pathIslandMaxPolys = 200;" "threshold member default")
require_text("${config_cpp}" "pathIslandGuard = ai::island::ClampMode(config.GetIntDefault(\"AiPlayerbot.Path.IslandGuard\", 1));"
  "loader: unknown mode = 0, default 1")
require_text("${config_cpp}" "ai::island::ClampMaxPolys(config.GetIntDefault(\"AiPlayerbot.Path.IslandMaxPolys\"" "threshold loader clamps")
require_text("${config_cpp}" "ai::island::ClampSnapDz(config.GetFloatDefault(\"AiPlayerbot.Path.IslandSnapDz\"" "snap dz loader clamps")
require_text("${config_cpp}" "ai::island::ClampStepOffYards(config.GetFloatDefault(\"AiPlayerbot.Path.IslandStepOffYards\"" "step-off loader clamps")
foreach(entry IN ITEMS
    "{\"Path.IslandGuard\", Value(pathIslandGuard)},"
    "{\"Path.IslandMaxPolys\", Value(pathIslandMaxPolys)},"
    "{\"Path.IslandSnapDz\", Value(pathIslandSnapDz)},"
    "{\"Path.IslandStepOffYards\", Value(pathIslandStepOffYards)},")
  require_text("${config_cpp}" "${entry}" "config echo entry")
endforeach()
require_order("${config_cpp}" "{\"Path.IslandStepOffYards\"" "{\"Path.PartialNoop\"" "echo entries sorted")
foreach(line IN ITEMS
    "\nAiPlayerbot.Path.IslandGuard = 1\n"
    "\nAiPlayerbot.Path.IslandMaxPolys = 200\n"
    "\nAiPlayerbot.Path.IslandSnapDz = 10\n"
    "\nAiPlayerbot.Path.IslandStepOffYards = 7\n")
  require_text("${conf_dist}" "${line}" ".dist default")
endforeach()
require_text("${conf_dist}" "70 % of the route starts, on the main mesh for 7.5 %" ".dist reason for the 200-poly threshold (OB-00 point 2)")
# OB-10 review 09.11. (#976/#977): the A/B result block stands once, at the IslandGuard key; the neighbour key
# Unstuck.CombatFightClock (same mode wording) keeps its own text and default 0.
count_text("${conf_dist}" "# IslandGuard A/B (crossover B/C" ig_ab_blocks)
if(NOT ig_ab_blocks EQUAL 1)
  message(FATAL_ERROR "island guard: .dist IslandGuard A/B block expected once, found ${ig_ab_blocks}")
endif()
require_text("${conf_dist}" "#   0 = off (rollback), 1 = every bot (default), 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid); other\n#   values act like 0.\nAiPlayerbot.Path.IslandGuard = 1\n" ".dist: the IslandGuard mode text sits at its key")
require_text("${conf_dist}" "#   0 = off (default), 1 = every bot, 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid); other values act like 0.\nAiPlayerbot.Unstuck.CombatFightClock = 0\n" ".dist: CombatFightClock mode text and default 0 unchanged")
require_text("${config_h}" "0 off (rollback), 1 all (default\n    // since the IslandGuard A/B, ACCEPT 09.11.2026)" "member comment: 0 = rollback, default 1")
require_text("${policy}" "constexpr std::uint32_t DefaultMaxPolys = 200;" "policy default threshold")
require_text("${policy}" "constexpr float DefaultSnapDz = 10.0f;" "policy default snap dz")
require_text("${policy}" "constexpr float DefaultStepOffYards = 7.0f;" "policy default step-off")

# 2. FarMoveArrivalPoint: ClosestCorrectPoint -> switch -> island check -> island>none = no point -> island>main moves the point
#    -> danger map -> out. One hook covers the start (ArriveBackYards / ArriveAltYards) and the ArriveSafe steps.
between("${movement}" "bool FarMoveArrivalPoint(Player* bot" "    out = point;" arrival)
require_order("${arrival}" "point.ClosestCorrectPoint(20.0f, 50.0f, bot->GetInstanceId())"
  "ai::cohort::Applies(sPlayerbotAIConfig.pathIslandGuard, bot->GetGUIDLow())" "island check after the arrival snap")
require_order("${arrival}" "ai::cohort::Applies(sPlayerbotAIConfig.pathIslandGuard" "ai::island::CheckArrival(" "the check only with the switch")
require_order("${arrival}" "ai::island::CheckArrival(" "if (check.snap == ai::island::ArrivalSnap::IslandNoMain)" "no main ground handled")
require_order("${arrival}" "if (check.snap == ai::island::ArrivalSnap::IslandNoMain)\n            return false;"
  "if (sPlayerbotAIConfig.dangerMapEnabled)" "island before the danger map (the danger cell of the moved point counts)")
require_text("${arrival}" "point = WorldPosition(point.getMapId(), x, y, z, 0.0f);" "island>main moves the point")
count_text("${movement}" "ai::island::CheckArrival(" arrival_hooks)
if(NOT arrival_hooks EQUAL 1)
  message(FATAL_ERROR "island guard: CheckArrival must be called once (in FarMoveArrivalPoint), found ${arrival_hooks}")
endif()

# 3. The snap= field: recorded at the far-move start, printed (and reset) at the arrival; the line is unchanged when unchecked.
require_text("${movement}" "FarMoveArrivalPoint(bot, farStart, endPos, sPlayerbotAIConfig.rosterFarMoveArriveBackYards, arrive, &islandCheck)"
  "start records the check of the back point")
require_text("${movement}" "FarMoveArrivalPoint(bot, farStart, endPos, far_move::ArriveAltYards, arrive, &islandCheck)"
  "start records the check of the alt point")
require_text("${movement}" "lastMove.farMoveIslandSnap = uint8(islandCheck.snap);" "start stores the snap")
require_order("${movement}" "ai::island::ArrivalSnap(lastMove.farMoveIslandSnap)" "lastMove.clear();\n                lastMove.farMoveAt = 0;"
  "arrival reads the snap before the movement is cleared")
require_text("${movement}" "\" snap=%s snap_d=%.1f comp=%u\"" "snap= field format (OB-00 point 3)")
require_text("${movement}" "far_dist=%.0f origin=%s%s\"" "[FarMoveArrive] carries the optional island field")
require_text("${movement}" "if (islandSnap != ai::island::ArrivalSnap::Unchecked)" "field only for checked bots")
foreach(field IN ITEMS "uint8 farMoveIslandSnap = 0;" "farMoveIslandSnap = other.farMoveIslandSnap;" "time_t islandCheckAt = 0;"
    "islandCheckAt = other.islandCheckAt;")
  require_text("${last_movement}" "${field}" "LastMovement field / copy")
endforeach()
require_text("${policy}" "Unchecked,    // switch off for this bot" "ArrivalSnap 0 = unchecked (LastMovement default)")

# 4. Step-off: only the fallback, before the route is built, after the far-move branch; gated and rate-limited.
between("${movement}" "bool MovementAction::MoveTo2(" "TravelPath movePath = ResolveMovePath(startPos, endPos, mover, lastMove, &noRoute);" moveto)
require_order("${moveto}" "bot->TeleportTo(arrive.getMapId()" "ai::island::FindStepOff(" "step-off after the far-move branch")
require_order("${moveto}" "if (FlyDirect(startPos, endPos, flyMovePosition, lastMove.lastPath))" "ai::island::FindStepOff(" "step-off after FlyDirect")
require_order("${moveto}" "ai::island::StepOffAllowed(" "ai::island::FindStepOff(" "gate before the search")
require_order("${moveto}" "lastMove.islandCheckAt = islandNow;" "ai::island::FindStepOff(" "rate limit stamped before the search")
require_text("${moveto}" "ai::cohort::Applies(sPlayerbotAIConfig.pathIslandGuard, bot->GetGUIDLow())" "step-off only for bots the switch applies to")
require_text("${moveto}" "lastMove.farMoveAt != 0" "no step-off during a far move")
require_text("${moveto}" "off.found && bot->IsWithinLOS(off.x, off.y, off.z)" "step-off needs line of sight")
require_text("${moveto}" "mm.MovePoint(bot->GetMapId(), off.x, off.y, off.z, FORCED_MOVEMENT_RUN, false);" "straight move (no path from an island)")
require_text("${moveto}" "[IslandStepOff] bot=%u" "step-off line")
require_text("${moveto}" "[IslandGuard] checked=%u island=%u snapped=%u no_main=%u stepoff=%u across=%u label_tiles=%u label_us=%llu tiles=%u label_bytes=%llu"
  "minute summary")

# 5. Module only (ADR-0041 R6): Detour through MMapManager's per-thread query, never loading tiles, the server snap reused;
#    the ground filter is ClosestCorrectPoint's.
require_text("${guard}" "mmap->GetNavMeshQuery(mapId)" "per-thread dtNavMeshQuery")
forbid_text("${guard}" "loadMap(" "the island guard never loads tiles")
forbid_text("${guard}" "dtAllocNavMeshQuery" "no own query objects")
require_text("${guard}" "PathInfo::FindWalkPoly(query, point, server, closest)" "the server's own snap")
require_text("${guard}" "uint16(NAV_MAGMA_SLIME | NAV_GROUND_STEEP | NAV_WATER)" "ground exclude flags")
require_text("${world_position}" "excludeFlags |= (NAV_MAGMA_SLIME | NAV_GROUND_STEEP | NAV_WATER);" "ClosestCorrectPoint still has the same exclude flags")
require_text("${guard}" "inline uint16 LabelInclude() { return uint16(NAV_GROUND | NAV_WATER); }" "label filter = server path filter")
if(DEFINED CORE_SOURCE_DIR AND EXISTS "${CORE_SOURCE_DIR}/src/game/Maps/MoveMap.cpp")
  file(READ "${CORE_SOURCE_DIR}/src/game/Maps/MoveMap.cpp" movemap)
  forbid_text("${movemap}" "island" "no core hook (ADR-0041 R6): MoveMap.cpp")
  forbid_text("${movemap}" "Island" "no core hook (ADR-0041 R6): MoveMap.cpp")
endif()

# 6. Thread model (OB-00 06.11.2026): one shared cache, values immutable and handed out as shared_ptr<const>, a hit under the
#    shared lock, the labelling outside any lock, publish-once under the unique lock (an earlier publisher wins).
between("${policy}" "class PublishOnceCache" "std::size_t Size() const" cache)
require_text("${cache}" "using Ptr = std::shared_ptr<Value const>;" "immutable published values")
require_order("${cache}" "std::shared_lock<std::shared_mutex> read(lock);" "Ptr fresh = std::make_shared<Value const>(compute());"
  "hit checked under the shared lock first")
require_order("${cache}" "        }\n\n        Ptr fresh = std::make_shared<Value const>(compute());" "std::unique_lock<std::shared_mutex> write(lock);"
  "compute outside the lock, then publish")
require_order("${cache}" "std::unique_lock<std::shared_mutex> write(lock);" "if (slot.value && slot.stamp == stamp)\n            return slot.value;"
  "publish-once: a value published meanwhile is kept")
require_text("${policy}" "mutable std::shared_mutex lock;" "cache lock")
count_text("${guard}" "static PublishOnceCache<" caches)
if(NOT caches EQUAL 1)
  message(FATAL_ERROR "island guard: exactly one shared label cache expected, found ${caches}")
endif()
count_text("${guard}" "Label(mesh, tile, tileIndex)" label_calls)
if(NOT label_calls EQUAL 1)
  message(FATAL_ERROR "island guard: tiles are labelled only inside the cache's compute, found ${label_calls} calls")
endif()
between("${guard}" "Cache().Get(" "&labelledHere);" compute)
require_text("${compute}" "TileLabels fresh = Label(mesh, tile, tileIndex);" "labelling inside the compute callback")
require_text("${guard}" "PublishOnceCache<uint64, TileLabels>::Ptr const labels" "readers hold a const pointer")
forbid_text("${guard}" "const_cast" "published labels are never written")
require_text("${guard}" "(uint64(mapId) << 32) | uint64(tileIndex), uint32(salt)" "key map + tile, stamp = tile salt (reloaded tile relabelled)")
require_text("${guard_h}" "Callable from any" "thread model documented at the entry points")

# 7. The bounded BFS across a tile edge (OB-00 Q1 A): only for CheckAcross, capped at the threshold, open edge = main.
require_text("${guard}" "case TileVerdict::CheckAcross:" "across only for small border components")
require_text("${guard}" "verdict.polys = Across(mesh, ref, maxPolys, openEdge);" "BFS capped at the threshold")
require_text("${policy}" "return !openEdge && bfsSize < maxPolys;" "open edge never an island")
