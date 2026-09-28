#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ai::spec_aura
{
// #357 / #367 O-12 variant A (owner 2026-09-27): reworked talents are bot auras in
// phase 1 (route B, no client patch). A bot on a premade path that gets them
// receives them by level and pays for them with talent points: the premade links
// leave exactly ReservedPoints(cls, path, level) unspent
// (tools/build_premade_specs.py, SPEC_AURAS - keep both tables equal).
// IDs, grant levels and point values come from OB-20 (shaman: twow-repo#357
// issuecomment-5857395120 / -5857623399, core#187; rogue: twow-repo#367).
constexpr std::uint8_t ClassRogue = 4;
constexpr std::uint8_t ClassShaman = 7;

// Path bits, per class.
enum PathMask : std::uint8_t
{
    // Shaman
    Enhancement = 1,    // 7.1
    ShamanTank = 2,     // 7.3
    Both = Enhancement | ShamanTank,

    // Rogue
    RogueCombat = 1,         // 4.0
    RogueAssassination = 2,  // 4.1
    RogueSubtlety = 4,       // 4.2
    RogueTank = 8,           // 4.3
};

struct AuraTalent
{
    char const* name;
    std::uint8_t paths;
    std::uint32_t firstLevel;          // rank k is granted at firstLevel + k - 1
    std::vector<std::uint32_t> ranks;  // spell ids, rank 1..n; a higher rank replaces the lower
};

// Ghost Wolf rank 3 (90110) is no aura: owner 2026-09-27, Improved Ghost Wolf 2/2
// is instant for everyone (#357 issuecomment-5857623399), so 7.1 = 14, 7.3 = 21.
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

// #367 owner talent line (issuecomment-5858564596), OB-20 IDs and grant levels
// (issuecomment-5858585355; 90191-90193 are script helpers and never granted).
// Path assignment (OB-10): 4.0 = 9, 4.1 = 9, 4.2 = 8, 4.3 = 20 points at 60.
inline std::vector<AuraTalent> const& RogueAuras()
{
    static std::vector<AuraTalent> const auras = {
        // Assassination (4.1)
        { "damage from behind",   RogueAssassination,       10, { 90150, 90151, 90152, 90153 } },
        { "snd cooldown crits",   RogueAssassination,       20, { 90154, 90155 } },
        { "cold blood damage",    RogueAssassination,       30, { 90156 } },
        { "vigor damage",         RogueAssassination,       40, { 90157 } },
        { "seal fate extra",      RogueAssassination,       40, { 90158 } },
        // Combat: C1 column for 4.0, agility and the C4 tank column for 4.3
        { "agility",              RogueCombat | RogueTank,  10, { 90159, 90160, 90161, 90162, 90163 } },
        { "defense",              RogueTank,                10, { 90164, 90165, 90166, 90167, 90168 } },
        { "riposte strikes",      RogueTank,                15, { 90169, 90170, 90171 } },
        { "evasive resistance",   RogueTank,                25, { 90172, 90173, 90174 } },
        { "execute strikes",      RogueCombat,              35, { 90175, 90176, 90177 } },
        { "frontal backstab",     RogueCombat,              40, { 90178 } },
        { "tank toughness",       RogueTank,                35, { 90179, 90180, 90181 } },
        { "ghostly magic dodge",  RogueTank,                40, { 90182 } },
        // Subtlety (4.2)
        { "stealth damage",       RogueSubtlety,            10, { 90183, 90184, 90185, 90186 } },
        { "hemorrhage stacks",    RogueSubtlety,            35, { 90187 } },
        { "shadow damage",        RogueSubtlety,            40, { 90188, 90189, 90190 } },
    };
    return auras;
}

inline std::vector<AuraTalent> const& AurasFor(std::uint8_t cls)
{
    static std::vector<AuraTalent> const none;
    if (cls == ClassShaman)
        return ShamanAuras();
    if (cls == ClassRogue)
        return RogueAuras();
    return none;
}

inline std::uint8_t PathFor(std::uint8_t cls, std::string const& pathName)
{
    if (cls == ClassShaman)
    {
        if (pathName == "enhancement")
            return Enhancement;
        if (pathName == "shaman tank")
            return ShamanTank;
    }
    else if (cls == ClassRogue)
    {
        if (pathName == "combat")
            return RogueCombat;
        if (pathName == "assassination")
            return RogueAssassination;
        if (pathName == "subtlety")
            return RogueSubtlety;
        if (pathName == "rogue tank")
            return RogueTank;
    }
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
inline std::vector<std::uint32_t> WantedAuras(std::uint8_t cls, std::uint8_t path, std::uint32_t level)
{
    std::vector<std::uint32_t> wanted;
    for (AuraTalent const& aura : AurasFor(cls))
        if (aura.paths & path)
            if (std::uint32_t const ranks = RanksAt(aura, level))
                wanted.push_back(aura.ranks[ranks - 1]);
    return wanted;
}

// Every aura spell of the class table (anything known but not wanted is removed).
inline std::vector<std::uint32_t> AllAuras(std::uint8_t cls)
{
    std::vector<std::uint32_t> all;
    for (AuraTalent const& aura : AurasFor(cls))
        all.insert(all.end(), aura.ranks.begin(), aura.ranks.end());
    return all;
}

// #367 kit (core#188): class spells a path gets without paying talent points,
// as (level, spell) ranks; the highest rank the level reached replaces the lower.
struct KitSpell
{
    char const* name;
    std::uint8_t paths;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranks;
};

inline std::vector<KitSpell> const& KitFor(std::uint8_t cls)
{
    static std::vector<KitSpell> const none;
    static std::vector<KitSpell> const rogue = {
        { "spit",         RogueTank, { { 12, 90140 } } },
        { "shadow dance", RogueTank, { { 20, 90142 }, { 40, 90143 }, { 60, 90144 } } },
    };
    return cls == ClassRogue ? rogue : none;
}

inline std::vector<std::uint32_t> WantedKit(std::uint8_t cls, std::uint8_t path, std::uint32_t level)
{
    std::vector<std::uint32_t> wanted;
    for (KitSpell const& kit : KitFor(cls))
    {
        if (!(kit.paths & path))
            continue;
        std::uint32_t best = 0;
        for (auto const& rank : kit.ranks)
            if (level >= rank.first)
                best = rank.second;
        if (best)
            wanted.push_back(best);
    }
    return wanted;
}

inline std::vector<std::uint32_t> AllKit(std::uint8_t cls)
{
    std::vector<std::uint32_t> all;
    for (KitSpell const& kit : KitFor(cls))
        for (auto const& rank : kit.ranks)
            all.push_back(rank.second);
    return all;
}

// #357 stage 2 (twow-repo#409): once the client patch and the server Talent.dbc carry
// a class's reworked talents, the aura spell ids above are that class's real talent
// ranks (same ids). The grant must then neither teach nor remove them, and the premade
// path pays nothing extra: AiPlayerbot.SpecAura.TalentClasses lists such classes.
inline bool TalentBacked(std::uint8_t cls, std::vector<std::uint32_t> const& talentClasses)
{
    for (std::uint32_t talentClass : talentClasses)
        if (talentClass == cls)
            return true;
    return false;
}

// Talent points the path pays for its auras at this level (one per granted rank;
// the kit is free).
inline std::uint32_t ReservedPoints(std::uint8_t cls, std::uint8_t path, std::uint32_t level)
{
    std::uint32_t points = 0;
    for (AuraTalent const& aura : AurasFor(cls))
        if (aura.paths & path)
            points += RanksAt(aura, level);
    return points;
}
}
