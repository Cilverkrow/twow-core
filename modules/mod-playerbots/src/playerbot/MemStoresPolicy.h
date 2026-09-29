#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::mem_stores
{
// twow-repo#416 / #319 (hotfix 7.3, 2026-09-29): mangosd grows by ~300 MB/h,
// the same on v12, v13 and v14. Suspect: GridUnload = 0 keeps every grid a bot
// ever entered loaded. One [MemStores] line per hour puts the resident memory
// next to the loaded grids and objects per map and the bounded bot stores, so
// the growth can be correlated. Diagnostic only.

constexpr uint32_t IntervalSeconds = 3600;

inline bool Due(uint32_t lastReport, uint32_t now)
{
    return !lastReport || now - lastReport >= IntervalSeconds;
}

// VmRSS in kB from the text of /proc/self/status; 0 when it is missing.
inline uint64_t ParseVmRssKb(std::string const& status)
{
    std::string::size_type pos = status.find("VmRSS:");
    if (pos == std::string::npos)
        return 0;
    pos += 6;
    while (pos < status.size() && (status[pos] == ' ' || status[pos] == '\t'))
        ++pos;

    uint64_t value = 0;
    while (pos < status.size() && status[pos] >= '0' && status[pos] <= '9')
        value = value * 10 + uint64_t(status[pos++] - '0');
    return value;
}

struct MapStat
{
    uint32_t mapId = 0;
    uint32_t instanceId = 0;
    uint32_t grids = 0;
    uint32_t creatures = 0;
    uint32_t gameobjects = 0;
    uint32_t players = 0;
};

// "map[/instance]:grids/creatures/gameobjects,..." for the maps with the most grids.
inline std::string TopMaps(std::vector<MapStat> stats, size_t count)
{
    std::sort(stats.begin(), stats.end(), [](MapStat const& a, MapStat const& b)
        {
            return a.grids != b.grids ? a.grids > b.grids : a.mapId < b.mapId;
        });

    std::string out;
    for (size_t i = 0; i < stats.size() && i < count; ++i)
    {
        MapStat const& s = stats[i];
        if (!out.empty())
            out += ',';
        out += std::to_string(s.mapId);
        if (s.instanceId)
            out += '/' + std::to_string(s.instanceId);
        out += ':' + std::to_string(s.grids) + '/' + std::to_string(s.creatures) + '/' + std::to_string(s.gameobjects);
    }
    return out;
}
}
