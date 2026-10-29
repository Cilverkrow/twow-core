# twow-repo#602 ROT-01 REF-next v2 tick (OB-00 / OB-30 29.10.2026): AiPlayerbot.Rotation.LosCacheMs, code default 500 (0 = rollback, a perf
# fix). A bot keeps one line-of-sight verdict per target (PlayerbotAI LosCacheGet / LosCacheSet / IsWithinLosCached); with
# Rotation.CastLosGate a cast on a target the cache knows is out of sight is IMPOSSIBLE without a new Spell / CheckCast (spells with
# SPELL_ATTR_EX2_IGNORE_LOS or an unknown entry take the full check), and the reach prerequisite answers "in sight and in range" from
# the cache before its CanCastSpell. This contract pins the dist key, the config member and read (> 5000 = the default), the parsed value
# in [ConfigEcho] (owner rule #615, sorted), the pure policy, the cache methods (0 = nothing kept), the gate early-out and its
# IGNORE_LOS exception, the verdict recording, the reach early answer and the cached sight query, and the cached= / reach_early=
# counters in the [CastLosGate] minute line. The rules are tested in t/los_cache_policy_tests.cpp.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#602 LosCacheMs: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#602 LosCacheMs: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("LosCachePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/actions/GenericSpellActions.cpp" spell_cpp)
read_source("strategy/actions/ReachTargetActions.h" reach_h)

# 1. The switch: default 500 in the dist, the member and the read; > 5000 = the default; parsed value in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.LosCacheMs = 500\n" "dist key, default 500")
require_text("${config_h}" "uint32 rotationLosCacheMs = 500;" "config member, default 500")
require_text("${config_cpp}" "rotationLosCacheMs = config.GetIntDefault(\"AiPlayerbot.Rotation.LosCacheMs\", 500);   // twow-repo#602 REF-next v2 tick\n    if (rotationLosCacheMs > ai::los_cache::MaxMs)\n        rotationLosCacheMs = ai::los_cache::DefaultMs;" "config read, > 5000 = the default")
require_text("${config_cpp}" "            {\"Rotation.InvalidTargetQuiet\", Value(rotationInvalidTargetQuiet)},\n            {\"Rotation.LosCacheMs\", Value(rotationLosCacheMs)},\n            {\"Rotation.MeleeFleeBreak\", Value(rotationMeleeFleeBreak)},\n" "parsed value in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std only) and its rules.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>")
    message(FATAL_ERROR "#602 LosCacheMs: LosCachePolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "constexpr std::uint32_t DefaultMs = 500;" "the default")
require_text("${policy}" "return ttlMs && ageMs < ttlMs;" "0 = off, never fresh")
require_text("${policy}" "return fresh && sameTarget && !cachedInLos && !spellIgnoresLos;" "skip only a fresh out-of-sight verdict, never for an ignore-LoS spell")
require_text("${policy}" "return on && inLos && distance <= range;" "reach: in sight and within range")

# 3. The cache in PlayerbotAI: one verdict, nothing kept when off.
require_text("${ai_h}" "bool LosCacheGet(ObjectGuid guid, bool& inLos) const;" "LosCacheGet")
require_text("${ai_h}" "bool IsWithinLosCached(Unit* target);" "IsWithinLosCached")
require_text("${ai_cpp}" "    if (guid != losCacheGuid || !ai::los_cache::Fresh(sPlayerbotAIConfig.rotationLosCacheMs, WorldTimer::getMSTimeDiff(losCacheAt, WorldTimer::getMSTime())))\n        return false;" "a hit only for the same target while fresh")
require_text("${ai_cpp}" "void PlayerbotAI::LosCacheSet(ObjectGuid guid, bool inLos)\n{\n    if (!sPlayerbotAIConfig.rotationLosCacheMs)\n        return;" "nothing kept when off")
require_text("${ai_cpp}" "    inLos = bot->IsWithinLOSInMap(target, true);\n    LosCacheSet(target->GetObjectGuid(), inLos);" "a miss queries the VMap once and remembers")

# 4. The gate: the early-out comes before its CanCastSpell, the IGNORE_LOS exception, the verdict is recorded after a real CheckCast.
string(FIND "${spell_cpp}" "bool CastSpellAction::isPossible()" ip)
string(SUBSTRING "${spell_cpp}" ${ip} 7000 isp)
require_text("${isp}" "bool const ignoresLos = !losSpell || losSpell->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS);" "ignore-LoS spells (and unknown entries) are never skipped")
require_text("${isp}" "        if (ai::los_cache::SkipCast(fresh, true, cachedInLos, ignoresLos))\n        {\n            ai::los_cache::cached.fetch_add(1, std::memory_order_relaxed);\n            return sPlayerbotAIConfig.rotationCastTrace ? RotEval(false, ai::rot_cast::EvalCode(false, uint8(SPELL_FAILED_LINE_OF_SIGHT))) : false;\n        }" "the cached out-of-sight verdict: IMPOSSIBLE, counted, the D0 trace code of a LINE_OF_SIGHT")
require_order("${isp}" "ai::los_cache::SkipCast(fresh, true, cachedInLos, ignoresLos)" "bool losPossible = ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true, false, false, &losResult);" "the early-out before the gate's CanCastSpell")
require_order("${isp}" "ai::cast_los_gate::Applies(sPlayerbotAIConfig.rotationCastLosGate" "ai::los_cache::SkipCast(" "the cache acts only inside the gate")
require_text("${isp}" "            if (!ignoresLos)\n                ai->LosCacheSet(spellTarget->GetObjectGuid(), false);" "a real LINE_OF_SIGHT verdict is remembered")

# 5. Reach: the in-sight-and-in-range answer first (only with the switch), counted and noted; the sight query through the cache.
require_text("${reach_h}" "#include \"playerbot/LosCachePolicy.h\"" "the policy include")
require_text("${reach_h}" "                        if (sPlayerbotAIConfig.rotationLosCacheMs &&\n                            ai::los_cache::ReachInPlace(true, ai->IsWithinLosCached(target), bot->GetDistance(target, true, DIST_CALC_COMBAT_REACH), range))" "reach early answer, only with the switch")
require_order("${reach_h}" "ai::los_cache::ReachInPlace(true, ai->IsWithinLosCached(target)" "if (!spellName.empty() && !ai->CanCastSpell(spellName, target, true, nullptr, true, true, true))" "the early answer before reach's CanCastSpell")
require_text("${reach_h}" "bool const inLos = sPlayerbotAIConfig.rotationLosCacheMs ? ai->IsWithinLosCached(target) : bot->IsWithinLOSInMap(target, true);" "the sight query through the cache, off = the old query")

# 6. The [CastLosGate] minute line: cached= and reach_early= appended last, as increments.
require_text("${mgr_cpp}" "#include \"playerbot/LosCachePolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "sLog.outBasic(\"[CastLosGate] mode=%u gated=%llu dropped=%llu skipped=%llu cached=%llu reach_early=%llu\", sPlayerbotAIConfig.rotationCastLosGate," "the minute line with cached= / reach_early= last")
require_text("${mgr_cpp}" "        (unsigned long long)ai::los_cache::cached.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::los_cache::reachEarly.exchange(0, std::memory_order_relaxed));" "the counters as increments")

message(STATUS "#602 LosCacheMs source contract passed")
