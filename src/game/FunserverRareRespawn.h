#ifndef TW_FUNSERVER_RARE_RESPAWN_H
#define TW_FUNSERVER_RARE_RESPAWN_H

#include <algorithm>
#include <cstdint>

// Issue twow-repo#298 / #322: accelerated respawn for audited open-world rares.
// new_seconds = max(minSeconds, min(floor(old_seconds / divisor), maxSeconds))
// The defaults (60, 60, 1440) turn one hour into one minute, clamped to
// [1 minute, 24 minutes]: 14h -> 14m, 7h30 -> 7m30, >= 24h -> 24m. Integer
// division rounds down, so the mapping is deterministic for every input.
// Callers guarantee divisor >= 1 and minSeconds <= maxSeconds (World config).
uint32_t constexpr FUNSERVER_RARE_RESPAWN_DEFAULT_DIVISOR = 60;
uint32_t constexpr FUNSERVER_RARE_RESPAWN_DEFAULT_MIN_SECONDS = 60;
uint32_t constexpr FUNSERVER_RARE_RESPAWN_DEFAULT_MAX_SECONDS = 1440;

inline uint32_t ScaleFunserverRareRespawnDelay(uint32_t seconds,
    uint32_t divisor = FUNSERVER_RARE_RESPAWN_DEFAULT_DIVISOR,
    uint32_t minSeconds = FUNSERVER_RARE_RESPAWN_DEFAULT_MIN_SECONDS,
    uint32_t maxSeconds = FUNSERVER_RARE_RESPAWN_DEFAULT_MAX_SECONDS)
{
    return std::max<uint32_t>(minSeconds, std::min<uint32_t>(seconds / std::max<uint32_t>(divisor, 1u), maxSeconds));
}

#endif
