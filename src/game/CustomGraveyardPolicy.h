#pragma once

#include <cstdint>
#include <set>
#include <string>

// twow-repo#575 F9 / F-b V2 (owner 29.10.2026): a level-limited graveyard for a zone, read from custom_graveyards.
// ObjectMgr::GetClosestGraveYard returns such a graveyard instead of the zone's normal ones when the dead player's level is at
// or below the row's max_level (Blackstone Island, zone 5536: up to L10 at the Bizzlo plaza, from L11 graveyard 149).
// Only zones in Graveyard.Custom.Zones are loaded: the table also holds Turtle rows (other zones, dungeon maps) that nobody
// has reviewed. Pure (std only), so the unit test and the source contract pin the rule without game headers.
namespace custom_graveyard
{
    // Synthetic WorldSafeLocs ids, far above the DBC's (1..174): one per row and team, so facing and pointer identity work.
    constexpr uint32_t IdBase = 100000;
    constexpr uint32_t TeamAlliance = 469;   // Team ALLIANCE
    constexpr uint32_t TeamHorde = 67;       // Team HORDE

    inline uint32_t SyntheticId(uint32_t rowId, bool horde)
    {
        return IdBase + rowId * 2 + (horde ? 1 : 0);
    }

    // "5536" or "5536, 406" -> {5536, 406}; anything that is not a digit separates, 0 is ignored.
    inline std::set<uint32_t> ParseZones(std::string const& text)
    {
        std::set<uint32_t> zones;
        uint64_t value = 0;
        bool inNumber = false;
        for (char c : text)
        {
            if (c >= '0' && c <= '9')
            {
                value = value * 10 + uint64_t(c - '0');
                if (value > 0xFFFFFFFFull)
                    value = 0xFFFFFFFFull;
                inNumber = true;
                continue;
            }
            if (inNumber && value)
                zones.insert(uint32_t(value));
            value = 0;
            inNumber = false;
        }
        if (inNumber && value)
            zones.insert(uint32_t(value));
        return zones;
    }

    // area_id 0 or area_id == zone_id: the whole zone (as the Turtle rows, e.g. Venture Camp 406/406); else only that area.
    inline bool AreaFits(uint32_t rowZone, uint32_t rowArea, uint32_t area)
    {
        return rowArea == 0 || rowArea == rowZone || rowArea == area;
    }

    // team 0 = any (GM .neargrave); otherwise the row's entry for that team only.
    inline bool TeamFits(bool horde, uint32_t team)
    {
        return team == 0 || team == (horde ? TeamHorde : TeamAlliance);
    }

    // level 0 = the caller passes no level: the custom graveyards stay out (battlegrounds, LFG, GM commands, old callers).
    inline bool Applies(uint32_t rowMap, uint32_t rowZone, uint32_t rowArea, uint32_t rowMaxLevel,
                        uint32_t map, uint32_t zone, uint32_t area, uint32_t level)
    {
        return level != 0 && level <= rowMaxLevel && rowMap == map && rowZone == zone && AreaFits(rowZone, rowArea, area);
    }
}
