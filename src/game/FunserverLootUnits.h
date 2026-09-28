#ifndef TW_FUNSERVER_LOOT_UNITS_H
#define TW_FUNSERVER_LOOT_UNITS_H

#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

// Issue twow-repo#323: fixed loot units per kill for funserver content.
// Pure rules only, so they can be tested without a server.

enum FunserverLootContent : uint8_t
{
    FUNSERVER_LOOT_NONE    = 0,
    FUNSERVER_LOOT_RARE    = 1,   // open-world rare / rare elite
    FUNSERVER_LOOT_DUNGEON = 2,   // registered dungeon boss or boss chest
    FUNSERVER_LOOT_RAID    = 3,   // registered raid boss, boss chest or world boss
    FUNSERVER_LOOT_CONTENT_COUNT
};

// Item quality 0 (poor) .. 5 (legendary); artifact (6) uses the legendary weight.
uint32_t constexpr FUNSERVER_LOOT_QUALITY_COUNT = 6;

inline uint32_t FunserverLootQualityIndex(uint32_t quality)
{
    return quality < FUNSERVER_LOOT_QUALITY_COUNT ? quality : FUNSERVER_LOOT_QUALITY_COUNT - 1;
}

// Weight of one more copy of an own-table item: base chance x quality weight,
// reduced multiplicatively for every copy already selected in this kill.
inline float FunserverUnitWeight(float baseChance, float qualityWeight, uint32_t copiesSelected, float decay)
{
    if (baseChance <= 0.0f || qualityWeight <= 0.0f)
        return 0.0f;
    return baseChance * qualityWeight * std::pow(decay, float(copiesSelected));
}

// Units the BoE pool must supply: whatever the own table cannot cover with
// distinct items at the content's quality floor.
inline uint32_t FunserverBoePoolShortfall(uint32_t remainingUnits, uint32_t distinctFloorItems)
{
    return remainingUnits > distinctFloorItems ? remainingUnits - distinctFloorItems : 0;
}

// Pool items match when their required level is within +-window of the level.
inline bool FunserverBoeLevelMatch(uint32_t itemRequiredLevel, uint32_t level, uint32_t window)
{
    uint32_t const diff = itemRequiredLevel > level ? itemRequiredLevel - level : level - itemRequiredLevel;
    return diff <= window;
}

// Owner decision 2026-09-27 (e, d): unique own-table items and every instance or BoE
// pool item drop at most once per kill. maxCopies 0 = no cap (the decay alone applies).
uint32_t constexpr FUNSERVER_UNIQUE_MAX_COPIES = 1;
uint32_t constexpr FUNSERVER_POOL_MAX_COPIES   = 1;

inline float FunserverUnitWeightCapped(float baseChance, float qualityWeight, uint32_t copiesSelected, float decay, uint32_t maxCopies)
{
    if (maxCopies && copiesSelected >= maxCopies)
        return 0.0f;
    return FunserverUnitWeight(baseChance, qualityWeight, copiesSelected, decay);
}

// Owner decision 2026-09-27 (a): world pool items must reach the tier of the boss's
// own table, item level >= own maximum - margin. Without own floor items (max 0)
// the required-level window alone decides.
inline bool FunserverBoeItemLevelMatch(uint32_t itemLevel, uint32_t ownMaxItemLevel, uint32_t margin)
{
    return !ownMaxItemLevel || itemLevel + margin >= ownMaxItemLevel;
}

// Owner decision 2026-09-28 (twow-repo#429, hotfix 7.3): a five-player dungeon boss
// keeps its own drops and adds BoE pool items up to a random target (2..4), at least
// minBoe while there is room below maxTotal, never more than maxTotal in total.
// 1 own -> 1..3 BoE; 2 own -> 1..2; 3 own -> 1; 4 own -> 0.
inline uint32_t FunserverDungeonBoeUnits(uint32_t ownItems, uint32_t target, uint32_t maxTotal, uint32_t minBoe)
{
    if (ownItems >= maxTotal)
        return 0;
    uint32_t const room = maxTotal - ownItems;
    uint32_t want = target > ownItems ? target - ownItems : 0;
    if (want < minBoe)
        want = minBoe;
    return want < room ? want : room;
}

// Owner decision 2026-09-28 (twow-repo#429, hotfix 7.3), raids: normal (non-set,
// non-token) loot per kill is a random target per map; set pieces and tokens stay on
// top from the normal roll. The fill adds what the normal drops lack, and the corpse
// never exceeds hardCap items in total.
inline uint32_t FunserverRaidFillUnits(uint32_t ownNormal, uint32_t target, uint32_t corpseItems, uint32_t hardCap)
{
    uint32_t const want = target > ownNormal ? target - ownNormal : 0;
    uint32_t const room = hardCap > corpseItems ? hardCap - corpseItems : 0;
    return want < room ? want : room;
}

struct FunserverLootRange
{
    uint32_t min;
    uint32_t max;
};

// "409:6-8,469:4-6" -> {409: 6..8, 469: 4..6}. Malformed entries and min > max are skipped.
inline std::map<uint32_t, FunserverLootRange> FunserverParseMapRanges(std::string const& text)
{
    std::map<uint32_t, FunserverLootRange> out;
    std::stringstream entries(text);
    std::string entry;
    while (std::getline(entries, entry, ','))
    {
        unsigned long mapId = 0, lo = 0, hi = 0;
        char colon = 0, dash = 0;
        std::istringstream in(entry);
        if (!(in >> mapId >> colon >> lo >> dash >> hi) || colon != ':' || dash != '-' || lo > hi || hi > 16)
            continue;
        in >> std::ws;
        if (!in.eof())
            continue;
        out[uint32_t(mapId)] = { uint32_t(lo), uint32_t(hi) };
    }
    return out;
}

#endif
