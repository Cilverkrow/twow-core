# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A01, card 51): FleeManager::calculatePossibleDestinations evaluates the same
# candidate angle once per distance ring (x/y use maxAllowedDistance, not dist): in the corpse case
# ~550 evaluations over ~99-117 distinct angle bit patterns (about 80% repeats). With
# AiPlayerbot.Perf.FleeMemo = 1 a call-local memo keyed by the exact float bits of the angle replays
# the first outcome (rejected, or the accepted point with its metrics) within the same time(0)
# second. intersectsOri stays per iteration, no RNG, selectOptimalDestination unchanged.
# Default 0 = old path.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A01: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A01: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A01: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A01: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A01: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A01: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("FleeManager.cpp" flee)
read_source("FleeManager.h" flee_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

region("${flee}" "void FleeManager::calculatePossibleDestinations" "bool FleeManager::isTooCloseToEdge" calc)
region("${flee}" "FleePoint* FleeManager::selectOptimalDestination" "bool FleeManager::CalculateDestination" select)

# --- Switch read once per call, memo is a local of this call (no member, no static, no TLS).
require_text("${calc}" "const bool fleeMemo = sPlayerbotAIConfig.perfFleeMemo;" "switch read once per call")
require_text("${calc}" "std::unordered_map<std::uint32_t, FleeMemoEntry> memo;" "call-local memo")
require_order("${calc}" "std::unordered_map<std::uint32_t, FleeMemoEntry> memo;" "float distIncrement" "memo declared before the ring loop")
require_count("${flee}" "perfFleeMemo" 1 "switch read only in calculatePossibleDestinations")
forbid_text("${flee}" "static std::unordered_map" "static memo")
forbid_text("${flee}" "thread_local" "thread-local memo")
forbid_text("${flee}" "static FleeMemoEntry" "static memo entry")
forbid_text("${flee}" "shared_mutex" "shared_mutex")
forbid_text("${flee_h}" "FleeMemo" "memo state in the FleeManager object")
forbid_text("${flee_h}" "memo" "memo state in the FleeManager object")
require_text("${flee}" "struct FleeMemoEntry" "memo entry type")
require_order("${flee}" "namespace\n{" "struct FleeMemoEntry" "memo entry in an anonymous namespace")
# A fresh entry means 'rejected': every early 'continue' of the evaluation leaves it that way, and
# only the accept branch flips it (an entry defaulting to accepted would replay phantom (0,0,0) points).
region("${flee}" "struct FleeMemoEntry" "};" memo_entry)
require_text("${memo_entry}" "bool accepted = false;" "memo entry defaults to rejected")
require_count("${flee}" "accepted = true" 1 "accepted set only in the accept branch")
forbid_text("${calc}" "urand(" "RNG in the candidate search")
forbid_text("${calc}" "rand_norm" "RNG in the candidate search")

# --- intersectsOri stays per iteration and runs BEFORE the memo lookup.
set(ori "if (intersectsOri(angle, enemyOri, angleIncrement)) continue;")
set(gate "if (fleeMemo)\n                {")
require_count("${calc}" "${ori}" 1 "intersectsOri check")
require_order("${calc}" "${ori}" "FleeMemoEntry* memoSlot = nullptr;" "intersectsOri before the memo")
require_order("${calc}" "FleeMemoEntry* memoSlot = nullptr;" "${gate}" "slot reset before the gate")

# --- Key = exact float bits, stamp = second read before the lookup, hit only in the same second.
require_order("${calc}" "${gate}" "const time_t memoNow = time(0);" "stamp inside the gate")
require_text("${calc}" "static_assert(sizeof(memoKey) == sizeof(angle)" "key width check")
require_order("${calc}" "const time_t memoNow = time(0);" "std::memcpy(&memoKey, &angle, sizeof(memoKey));" "stamp before key")
require_order("${calc}" "std::memcpy(&memoKey, &angle, sizeof(memoKey));" "auto memoIt = memo.find(memoKey);" "key before lookup")
require_text("${calc}" "if (memoIt != memo.end() && memoIt->second.stampSecond == memoNow)" "same-second hit condition")

# --- A hit replays the stored outcome exactly (duplicate point incl. metrics; rejected = skip).
require_text("${calc}" "FleePoint *point = new FleePoint(GetBotAI(bot), memoHit.x, memoHit.y, memoHit.z);" "replayed point")
require_text("${calc}" "point->minDistance = memoHit.minDistance;" "replayed minDistance")
require_text("${calc}" "point->sumDistance = memoHit.sumDistance;" "replayed sumDistance")
require_order("${calc}" "if (memoHit.accepted)" "point->sumDistance = memoHit.sumDistance;" "replay only accepted")
require_count("${calc}" "points.push_back(point);" 2 "push sites (replay + original accept)")
# The hit block pushes at most one replayed point and always ends with 'continue' AFTER the
# accepted-only block, so a hit (accepted or rejected) never falls through into a second evaluation.
region("${calc}" "if (memoIt != memo.end() && memoIt->second.stampSecond == memoNow)" "memoSlot = &memo[memoKey];" hit)
require_count("${hit}" "points.push_back(point);" 1 "one replay push in the hit block")
require_count("${hit}" "continue;" 1 "one continue in the hit block")
require_order("${hit}" "if (memoHit.accepted)" "points.push_back(point);" "replay push only when accepted")
string(FIND "${hit}" "points.push_back(point);" hit_push)
string(SUBSTRING "${hit}" ${hit_push} -1 hit_tail)
require_order("${hit_tail}" "}" "continue;" "continue after the accepted-only block")
require_count("${calc}" "memo[memoKey]" 1 "one memo insert (miss path)")

# --- A miss pre-records 'rejected' with the stamp, then runs the ORIGINAL evaluation once.
require_order("${calc}" "memoSlot = &memo[memoKey];" "*memoSlot = FleeMemoEntry();" "slot reset")
require_order("${calc}" "*memoSlot = FleeMemoEntry();" "memoSlot->stampSecond = memoNow;" "stamp stored after reset")
require_order("${calc}" "memoSlot->stampSecond = memoNow;" "float x = botPosX + cos(angle) * maxAllowedDistance" "memo before the evaluation")

# Original evaluation order, each step exactly once (no second copy of the evaluation).
set(step_x "float x = botPosX + cos(angle) * maxAllowedDistance, y = botPosY + sin(angle) * maxAllowedDistance, z = botPosZ + CONTACT_DISTANCE;")
set(step_edge "if (MoveStyleValue::CheckForEdges(GetBotAI(bot)) && isTooCloseToEdge(x, y, z, angle)) continue;")
set(step_force "if (forceMaxDistance && sServerFacade.IsDistanceLessThan(sServerFacade.GetDistance2d(bot, x, y), maxAllowedDistance - sPlayerbotAIConfig.tooCloseDistance))")
set(step_z "bot->UpdateAllowedPositionZ(x, y, z);")
set(step_water "if (terrain && terrain->IsInWater(x, y, z))")
set(step_los "(target && !target->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true))")
set(step_metrics "calculateDistanceToCreatures(point);")
set(step_accept "if (sServerFacade.IsDistanceGreaterOrEqualThan(point->minDistance - start.minDistance, sPlayerbotAIConfig.followDistance))")
foreach(step step_x step_edge step_force step_z step_water step_los step_accept)
  require_count("${calc}" "${${step}}" 1 "evaluation step ${step}")
endforeach()
require_count("${calc}" "${step_metrics}" 1 "candidate metrics once")
require_count("${calc}" "calculateDistanceToCreatures(&start);" 1 "start metrics once")
require_order("${calc}" "${step_x}" "${step_edge}" "x before edge")
require_order("${calc}" "${step_edge}" "${step_force}" "edge before forceMaxDistance")
require_order("${calc}" "${step_force}" "${step_z}" "forceMaxDistance before height")
require_order("${calc}" "${step_z}" "${step_water}" "height before water")
require_order("${calc}" "${step_water}" "${step_los}" "water before target LOS")
require_order("${calc}" "${step_los}" "FleePoint *point = new FleePoint(GetBotAI(bot), x, y, z);" "LOS before point")
require_order("${calc}" "FleePoint *point = new FleePoint(GetBotAI(bot), x, y, z);" "${step_accept}" "point before accept")

# Accept records the final point + metrics before the push; reject path deletes as before.
region("${calc}" "${step_accept}" "delete point;" accept)
require_order("${accept}" "if (memoSlot)" "memoSlot->accepted = true;" "record only with a slot")
foreach(field x y z minDistance sumDistance)
  require_text("${accept}" "memoSlot->${field} = point->${field};" "recorded ${field}")
endforeach()
require_order("${accept}" "memoSlot->sumDistance = point->sumDistance;" "points.push_back(point);" "record before push")

# --- Selection untouched (strict '>' keeps the first of equal duplicates).
require_text("${select}" "if (!best || isBetterThan(point, best))" "selection loop")
forbid_text("${select}" "memo" "memo in selectOptimalDestination")
require_text("${flee}" "return point->sumDistance - other->sumDistance > 0;" "strict comparison")

# --- Switch: default off, documented with value 0.
require_text("${config_h}" "bool perfFleeMemo = false;" "switch member")
require_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool perfFleeMemo = false;" "switch after the #541 switches")
require_text("${config_cpp}" "perfFleeMemo = config.GetBoolDefault(\"AiPlayerbot.Perf.FleeMemo\", false);" "switch read, default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.FleeMemo = 0\n" "documented key, value 0")


# --- The same-second stamp is sound only while value recomputation is keyed on time(0) seconds:
# CalculatedValue::Get recomputes only when time(0) moved on, and "possible targets no los" stays a
# CalculatedValue (PossibleTargetsValue -> NearestUnitsValue -> ObjectGuidListCalculatedValue).
read_source("strategy/Value.h" value_h)
read_source("strategy/values/ValueContext.h" value_ctx)
region("${value_h}" "class CalculatedValue : public UntypedValue, public Value<T>" "class SingleCalculatedValue" calculated)
require_order("${calculated}" "virtual T Get() override" "time_t now = time(0);" "CalculatedValue::Get keyed on time(0)")
require_text("${calculated}" "if (!lastCheckTime || (checkInterval < 2 && (now - lastCheckTime > 0.1)) || now - lastCheckTime >= checkInterval / 2)" "CalculatedValue recompute rule (whole seconds)")
require_text("${value_ctx}" "creators[\"possible targets no los\"] = [](PlayerbotAI* ai) { return new PossibleTargetsValue(ai, \"possible targets\", sPlayerbotAIConfig.sightDistance, true); };" "possible targets no los creator")
# No class between PossibleTargetsValue and CalculatedValue may override Get() (for example with a
# millisecond clock or a skip-search cache); that would break the same-second argument silently.
read_source("strategy/values/PossibleTargetsValue.h" possible_h)
read_source("strategy/values/NearestUnitsValue.h" nearest_h)
region("${possible_h}" "class PossibleTargetsValue : public NearestUnitsValue" "class AllTargetsValue" possible)
region("${nearest_h}" "class NearestUnitsValue : public ObjectGuidListCalculatedValue" "class NearestStealthedUnitsValue" nearest)
region("${value_h}" "class ObjectGuidListCalculatedValue : public CalculatedValue<std::list<ObjectGuid> >" "class GuidPositionCalculatedValue" guid_list)
foreach(chain possible nearest guid_list)
  if("${${chain}}" MATCHES "[^A-Za-z0-9_]Get[ \t]*\\(")
    message(FATAL_ERROR "#541 A01: forbidden Get() override in the 'possible targets no los' class chain (${chain}): the same-second memo stamp relies on CalculatedValue::Get")
  endif()
endforeach()

message(STATUS "flee_memo source contract passed")
