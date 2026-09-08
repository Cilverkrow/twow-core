#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace ai::profession
{
constexpr std::uint32_t kPlanVersion = 1;
constexpr char const* kEventName = "profession_pair";
constexpr char const* kEventData = "v1";

enum Pair : std::uint32_t
{
    None,
    HerbalismAlchemy,
    SkinningLeatherworking,
    MiningBlacksmithing,
    MiningEngineering,
    MiningJewelcrafting,
    TailoringEnchanting
};

struct Definition
{
    Pair pair;
    std::uint32_t first;
    std::uint32_t second;
};

constexpr std::uint32_t kAlchemy = 171;
constexpr std::uint32_t kBlacksmithing = 164;
constexpr std::uint32_t kEnchanting = 333;
constexpr std::uint32_t kEngineering = 202;
constexpr std::uint32_t kHerbalism = 182;
constexpr std::uint32_t kJewelcrafting = 755;
constexpr std::uint32_t kLeatherworking = 165;
constexpr std::uint32_t kMining = 186;
constexpr std::uint32_t kSkinning = 393;
constexpr std::uint32_t kTailoring = 197;

inline Definition const* Find(Pair pair)
{
    static Definition const definitions[] =
    {
        { HerbalismAlchemy, kHerbalism, kAlchemy },
        { SkinningLeatherworking, kSkinning, kLeatherworking },
        { MiningBlacksmithing, kMining, kBlacksmithing },
        { MiningEngineering, kMining, kEngineering },
        { MiningJewelcrafting, kMining, kJewelcrafting },
        { TailoringEnchanting, kTailoring, kEnchanting }
    };

    for (Definition const& definition : definitions)
    {
        if (definition.pair == pair)
            return &definition;
    }

    return nullptr;
}

inline bool IsValid(std::uint32_t value)
{
    return Find(static_cast<Pair>(value)) != nullptr;
}

inline bool Contains(Pair pair, std::uint32_t skill)
{
    if (Definition const* definition = Find(pair))
        return definition->first == skill || definition->second == skill;

    return false;
}

inline std::uint32_t Mix(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

inline std::uint32_t Weight(std::uint8_t classId, Pair pair)
{
    std::uint32_t herbalism = 2;
    std::uint32_t skinning = 1;
    std::uint32_t mining = 1;
    std::uint32_t tailoring = 3;

    switch (classId)
    {
        case 1: // warrior
        case 6: // death knight
            skinning = 2;
            mining = 3;
            tailoring = 1;
            break;
        case 2: // paladin
            mining = 3;
            tailoring = 2;
            break;
        case 3: // hunter
            skinning = 3;
            mining = 3;
            tailoring = 1;
            break;
        case 4: // rogue
            herbalism = 1;
            skinning = 3;
            mining = 2;
            tailoring = 1;
            break;
        case 5: // priest
        case 8: // mage
        case 9: // warlock
            break;
        case 7: // shaman
            herbalism = 3;
            skinning = 3;
            mining = 2;
            tailoring = 1;
            break;
        case 11: // druid
            herbalism = 3;
            skinning = 3;
            tailoring = 1;
            break;
        default:
            break;
    }

    switch (pair)
    {
        case HerbalismAlchemy:
            return herbalism * 7;
        case SkinningLeatherworking:
            return skinning * 7;
        case MiningBlacksmithing:
            return mining * 3;
        case MiningEngineering:
        case MiningJewelcrafting:
            return mining * 2;
        case TailoringEnchanting:
            return tailoring * 7;
        case None:
            return 0;
    }

    return 0;
}

inline Pair Select(std::uint32_t guid, std::uint8_t classId, std::uint32_t requiredSkill = 0)
{
    Pair const pairs[] =
    {
        HerbalismAlchemy,
        SkinningLeatherworking,
        MiningBlacksmithing,
        MiningEngineering,
        MiningJewelcrafting,
        TailoringEnchanting
    };

    std::uint32_t total = 0;
    for (Pair pair : pairs)
    {
        if (!requiredSkill || Contains(pair, requiredSkill))
            total += Weight(classId, pair);
    }

    if (!total)
        return None;

    std::uint32_t ticket = Mix(guid ^ (kPlanVersion * 0x9e3779b9u)) % total;
    for (Pair pair : pairs)
    {
        if (requiredSkill && !Contains(pair, requiredSkill))
            continue;

        std::uint32_t const weight = Weight(classId, pair);
        if (ticket < weight)
            return pair;

        ticket -= weight;
    }

    return None;
}

// Resolve the non-destructive "grandfathering" case before choosing a new
// plan. Two existing professions must name one unambiguous allowed pair. A
// single existing profession is retained by selecting only from pairs that
// contain it. More than two, duplicate, or incompatible professions yield no
// plan: the caller must retain the character state without changing it.
inline Pair SelectExisting(std::uint32_t guid, std::uint8_t classId,
    std::uint32_t const* skills, std::size_t count)
{
    if (count == 0)
        return Select(guid, classId);

    if (!skills || count > 2)
        return None;

    if (count == 1)
        return Select(guid, classId, skills[0]);

    if (skills[0] == skills[1])
        return None;

    Pair match = None;
    for (Pair candidate : { HerbalismAlchemy, SkinningLeatherworking,
            MiningBlacksmithing, MiningEngineering, MiningJewelcrafting,
            TailoringEnchanting })
    {
        if (!Contains(candidate, skills[0]) || !Contains(candidate, skills[1]))
            continue;

        if (match != None)
            return None;

        match = candidate;
    }

    return match;
}
}
