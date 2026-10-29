#pragma once

#include <atomic>
#include <cstdint>

namespace ai::los_cache
{
// twow-repo#602 ROT-01 REF-next v2 tick (OB-00 / OB-30 29.10.2026; AiPlayerbot.Rotation.LosCacheMs, code default 500, 0 = rollback,
// a perf fix): under REF-next v2 [BotUpdate] p99 went from 4 to 33 ms. OB-30's slow-update profile: IMPOSSIBLE cast checks cost a
// median 129 us instead of 9 us (mind flay / vampiric embrace / holy fire) and "reach spell - USELESS" 1351 calls / 928 ms per run.
// Cause: with Rotation.CastLosGate a LINE_OF_SIGHT verdict makes a cast IMPOSSIBLE, so the engine tries every alternative of the node,
// and each one builds a new Spell and runs CheckCast with the core's VMap line-of-sight query against the same unseen target - every
// pass, until hotfix 8.2 drops the target after ~20 s; and every cast's reach prerequisite ran a full CanCastSpell (CheckCast with
// LoS) plus a second VMap query before answering "in sight and in range: not useful". With the cache:
// - the bot keeps its last line-of-sight verdict for one target for LosCacheMs; a cast on a target the cache knows is out of sight is
//   IMPOSSIBLE at once (no Spell, no CheckCast) - except a spell with SPELL_ATTR_EX2_IGNORE_LOS, which never needs sight;
// - reach answers "in sight and in range" first, from the cached verdict, before its CanCastSpell (the same answer it gave after it);
// - reach's own sight query goes through the cache.
// The worst behaviour effect is a reaction up to LosCacheMs later once sight returns. Pure rules, no game types.

constexpr std::uint32_t DefaultMs = 500;
constexpr std::uint32_t MaxMs = 5000;   // a larger value is not a cache any more: the read maps it to DefaultMs

// A cached verdict is fresh while younger than the switch's milliseconds; 0 = off, never fresh.
inline bool Fresh(std::uint32_t ttlMs, std::uint32_t ageMs)
{
    return ttlMs && ageMs < ttlMs;
}

// The gate skips the CheckCast of a cast only on the cached target, only when it is known to be out of sight, and never for a spell
// that ignores line of sight (SPELL_ATTR_EX2_IGNORE_LOS) or whose entry is unknown (treated as ignoring: the old path decides).
inline bool SkipCast(bool fresh, bool sameTarget, bool cachedInLos, bool spellIgnoresLos)
{
    return fresh && sameTarget && !cachedInLos && !spellIgnoresLos;
}

// Reach: a target in sight and within the range is "not useful" whatever CanCastSpell says (both of its branches answer false then),
// so the answer may come first, without the CanCastSpell.
inline bool ReachInPlace(bool on, bool inLos, float distance, float range)
{
    return on && inLos && distance <= range;
}

// Counters for the [CastLosGate] minute line. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> cached{ 0 };       // casts made IMPOSSIBLE from the cached out-of-sight verdict (no CheckCast)
inline std::atomic<std::uint64_t> reachEarly{ 0 };   // reach answers "in sight and in range" before the CanCastSpell
}
