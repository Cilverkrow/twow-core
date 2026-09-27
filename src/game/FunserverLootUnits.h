#ifndef TW_FUNSERVER_LOOT_UNITS_H
#define TW_FUNSERVER_LOOT_UNITS_H

#include <cmath>
#include <cstdint>

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

#endif
