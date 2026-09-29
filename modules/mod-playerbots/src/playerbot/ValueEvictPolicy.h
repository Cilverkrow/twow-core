#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai::value_evict
{
// twow-repo#416 / #319 (hotfix 7.5, 2026-09-29): the per-bot value cache
// (AiObjectContext / NamedObjectContext::created) never drops an entry until
// logout. [MemStores] on v16: bot_values +46-63k per hour (largest bot 6000),
// led by "can use item on", whose qualifier carried the target's coordinates -
// every move of an rpg target made a new value. Upstream's ClearExpiredValues()
// only runs in ProcessBot's rare "update" event. Two costs: memory, and
// ClearValues(prefix), which copied every created name on each call (every
// travel choice) - O(n log n) growing with the cache (ticks/min 736 -> 574).

// F1: every EvictIntervalSeconds a bot drops the cached values of its own
// contexts that were not calculated for EvictIdleSeconds. Only calculated
// values can expire; manual (state-carrying) values never do, protected
// (memory / log) values are kept, shared contexts are not touched.
constexpr uint32_t EvictIntervalSeconds = 600;
constexpr uint32_t EvictIdleSeconds = 600;
constexpr uint32_t EvictLogSeconds = 3600;

inline bool Evictable(bool isProtected, bool expired)
{
    return !isProtected && expired;
}

// Staggered per bot (by guid), so 180 bots do not evict in the same tick.
struct EvictClock
{
    uint32_t next = 0;

    bool Due(uint32_t now, uint32_t stagger)
    {
        if (!next)
        {
            next = now + stagger % EvictIntervalSeconds;
            return false;
        }
        if (now < next)
            return false;
        next = now + EvictIntervalSeconds;
        return true;
    }
};

// F2: the created names starting with `prefix`, from a sorted map, via
// lower_bound - the same result as filtering every name with
// name.find(prefix) == 0, in O(log n + k). An empty prefix yields all names.
template <class SortedMap>
void AppendKeysWithPrefix(SortedMap const& map, std::string const& prefix, std::vector<std::string>& out)
{
    for (auto it = prefix.empty() ? map.begin() : map.lower_bound(prefix); it != map.end(); ++it)
    {
        if (it->first.compare(0, prefix.size(), prefix) != 0)
            break;
        out.push_back(it->first);
    }
}

// F3: "can use item on" keys its target by map and guid only; the coordinates
// (unused by CanUseItemOn::Calculate) made a new cached value on every move.
// Same format as GuidPosition::to_string(), which GuidPosition(std::string) parses.
inline std::string StableTargetQualifier(uint32_t mapId, uint64_t rawGuid)
{
    return std::to_string(mapId) + "|0|0|0|0|" + std::to_string(rawGuid);
}
}
