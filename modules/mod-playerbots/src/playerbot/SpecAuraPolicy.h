#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ai::spec_aura
{
// #357 O-12 variant A (owner 2026-09-27): the reworked Enhancement talents are
// bot auras in phase 1 (route B, no client patch). A bot on premade path 7.1
// "enhancement" or 7.3 "shaman tank" gets them by level and pays for them with
// talent points: the premade links leave exactly ReservedPoints(path, level)
// unspent (tools/build_premade_specs.py, SPEC_AURAS - keep both tables equal).
// IDs, grant levels and point values: OB-20 interface, twow-repo#357
// issuecomment-5857395120. The spell_template rows come from OB-20 (core#187).
// Ghost Wolf rank 3 (90110) is no aura: owner 2026-09-27, Improved Ghost Wolf 2/2
// is instant for everyone (#357 issuecomment-5857623399), so 7.1 = 14, 7.3 = 21.
enum PathMask : std::uint8_t
{
    Enhancement = 1,  // 7.1
    ShamanTank = 2,   // 7.3
    Both = Enhancement | ShamanTank,
};

struct AuraTalent
{
    char const* name;
    std::uint8_t paths;
    std::uint32_t firstLevel;          // rank k is granted at firstLevel + k - 1
    std::vector<std::uint32_t> ranks;  // spell ids, rank 1..n; a higher rank replaces the lower
};

inline std::vector<AuraTalent> const& ShamanAuras()
{
    static std::vector<AuraTalent> const auras = {
        { "attack speed",        Both,        10, { 90100, 90101, 90102, 90103, 90104 } },
        { "defense",             ShamanTank,  10, { 90105, 90106, 90107, 90108, 90109 } },
        { "imbue mastery",       Both,        25, { 90111, 90112, 90113 } },
        { "retaliation",         ShamanTank,  25, { 90114, 90115, 90116 } },
        { "stormstrike charges", ShamanTank,  30, { 90117 } },
        { "storm wisdom",        Enhancement, 35, { 90118, 90119, 90120, 90121, 90122 } },
        { "chain storm",         Enhancement, 40, { 90124 } },
        { "shield constitution", ShamanTank,  35, { 90126, 90127, 90128 } },
        { "shield ward",         ShamanTank,  40, { 90129 } },
    };
    return auras;
}

inline std::uint8_t PathFor(std::uint8_t cls, std::string const& pathName)
{
    if (cls != 7)
        return 0;
    if (pathName == "enhancement")
        return Enhancement;
    if (pathName == "shaman tank")
        return ShamanTank;
    return 0;
}

// Ranks of a talent a bot of this level has: 0 before firstLevel.
inline std::uint32_t RanksAt(AuraTalent const& aura, std::uint32_t level)
{
    if (level < aura.firstLevel)
        return 0;
    std::uint32_t const ranks = level - aura.firstLevel + 1;
    return ranks < aura.ranks.size() ? ranks : std::uint32_t(aura.ranks.size());
}

// The aura spells the bot should have: the highest due rank of every talent of its path.
inline std::vector<std::uint32_t> WantedAuras(std::uint8_t path, std::uint32_t level)
{
    std::vector<std::uint32_t> wanted;
    for (AuraTalent const& aura : ShamanAuras())
        if (aura.paths & path)
            if (std::uint32_t const ranks = RanksAt(aura, level))
                wanted.push_back(aura.ranks[ranks - 1]);
    return wanted;
}

// Every aura spell of the table (anything known but not wanted is removed).
inline std::vector<std::uint32_t> AllAuras()
{
    std::vector<std::uint32_t> all;
    for (AuraTalent const& aura : ShamanAuras())
        all.insert(all.end(), aura.ranks.begin(), aura.ranks.end());
    return all;
}

// Talent points the path pays for its auras at this level (one per granted rank).
inline std::uint32_t ReservedPoints(std::uint8_t path, std::uint32_t level)
{
    std::uint32_t points = 0;
    for (AuraTalent const& aura : ShamanAuras())
        if (aura.paths & path)
            points += RanksAt(aura, level);
    return points;
}
}
