#ifndef TW_FUNSERVER_RARE_RESPAWN_H
#define TW_FUNSERVER_RARE_RESPAWN_H

#include <algorithm>
#include <cstdint>

// Issue twow-repo#298: accelerated respawn for audited open-world rares.
// new_seconds = max(60, min(floor(old_seconds / 60), 1440))
// One hour of configured respawn becomes one minute; the result is clamped to
// [1 minute, 24 minutes]. Integer division rounds down, so the mapping is
// deterministic for every input (14h -> 14m, 7h30 -> 7m30, >= 24h -> 24m).
inline uint32_t ScaleFunserverRareRespawnDelay(uint32_t seconds)
{
    return std::max<uint32_t>(60u, std::min<uint32_t>(seconds / 60u, 1440u));
}

#endif
