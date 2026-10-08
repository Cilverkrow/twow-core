#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include <climits>

namespace ai::area_level
{
// twow-repo#416 (hotfix 7.3, 2026-09-29): third hard stall, now proven by the
// #216 diagnostic - Latchigedap (Mulgore) 19.6 s in "refresh travel target",
// no path builds. TravelMgr::GetAreaLevel fell back to a scan over every
// creature of the world, with a terrain lookup for each, for an area that has
// no level of its own. That ran on the map thread, once per area and server run:
// LoadAreaLevels precomputes all areas at startup, but its loop used
// sAreaStore.GetNumRows() - the record count (1481) of the SQL store, not the
// highest id (5735) - so the 388 Turtle areas above id 3487 (117 without a level)
// were first computed live, by whichever bot touched them first.

// End of the area id range (exclusive): the SQL store is sparse, ids go past the
// record count.
inline uint32_t AreaIdEnd(uint32_t maxEntry, uint32_t recordCount)
{
    return std::max(maxEntry, recordCount);
}

// The creature levels are only used while the area levels load at startup;
// at runtime an uncached area never scans the world.
inline bool MayUseCreatureLevels(bool loadingAreaLevels)
{
    return loadingAreaLevels;
}

// Average max level of the hostile creatures per area, filled in one pass.
struct CreatureLevels
{
    bool loaded = false;
    std::unordered_map<uint32_t, std::pair<int64_t, uint32_t>> sums;

    void Add(uint32_t areaId, uint32_t level)
    {
        auto& sum = sums[areaId];
        sum.first += level;
        ++sum.second;
    }

    // 0 when no creature stands in the area; otherwise at least 1.
    int32_t Average(uint32_t areaId) const
    {
        auto const it = sums.find(areaId);
        if (it == sums.end() || !it->second.second)
            return 0;
        return std::max<int32_t>(1, int32_t(it->second.first / it->second.second));
    }

    uint32_t Size() const
    {
        return uint32_t(sums.size());
    }
};

// twow-repo#541 (deep dive C1): the area levels as a table by area id, frozen at the end of the startup
// load and read without a lock. Unset marks an id the load did not fill (it takes the locked path).
constexpr int32_t FrozenUnset = INT32_MIN;

inline std::vector<int32_t> FreezeLevels(std::vector<std::pair<uint32_t, int32_t>> const& levels, uint32_t areaIdEnd)
{
    std::vector<int32_t> table(areaIdEnd, FrozenUnset);
    for (auto const& [id, level] : levels)
        if (id < areaIdEnd)
            table[id] = level;
    return table;
}

// True with the level when the frozen table knows the area.
inline bool FrozenLookup(std::vector<int32_t> const& table, uint32_t areaId, int32_t& level)
{
    if (areaId >= table.size() || table[areaId] == FrozenUnset)
        return false;
    level = table[areaId];
    return true;
}
}
