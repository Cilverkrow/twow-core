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

// Ghost Wolf rank 3 (61111) is no aura: owner 2026-09-27, Improved Ghost Wolf 2/2
// is instant for everyone (#357 issuecomment-5857623399), so 7.1 = 14, 7.3 = 21.
inline std::vector<AuraTalent> const& ShamanAuras()
{
    static std::vector<AuraTalent> const auras = {
        { "attack speed",        Both,        10, { 61101, 61102, 61103, 61104, 61105 } },
        { "defense",             ShamanTank,  10, { 61106, 61107, 61108, 61109, 61110 } },
        { "imbue mastery",       Both,        25, { 61112, 61113, 61114 } },
        { "retaliation",         ShamanTank,  25, { 61115, 61116, 61117 } },
        { "stormstrike charges", ShamanTank,  30, { 61118 } },
        { "storm wisdom",        Enhancement, 35, { 61119, 61120, 61121, 61122, 61123 } },
        { "chain storm",         Enhancement, 40, { 61125 } },
        { "shield constitution", ShamanTank,  35, { 61127, 61128, 61129 } },
        { "shield ward",         ShamanTank,  40, { 61130 } },
    };
    return auras;
}

// #367 owner talent line (issuecomment-5858564596), OB-20 IDs and grant levels
// (issuecomment-5858585355; 61192-61194 are script helpers and never granted).
// Path assignment (OB-10): 4.0 = 9, 4.1 = 9, 4.2 = 8, 4.3 = 20 points at 60.
inline std::vector<AuraTalent> const& RogueAuras()
{
    static std::vector<AuraTalent> const auras = {
        // Assassination (4.1)
        { "damage from behind",   RogueAssassination,       10, { 61151, 61152, 61153, 61154 } },
        { "snd cooldown crits",   RogueAssassination,       20, { 61155, 61156 } },
        { "cold blood damage",    RogueAssassination,       30, { 61157 } },
        { "vigor damage",         RogueAssassination,       40, { 61158 } },
        { "seal fate extra",      RogueAssassination,       40, { 61159 } },
        // Combat: C1 column for 4.0, agility and the C4 tank column for 4.3
        { "agility",              RogueCombat | RogueTank,  10, { 61160, 61161, 61162, 61163, 61164 } },
        { "defense",              RogueTank,                10, { 61165, 61166, 61167, 61168, 61169 } },
        { "riposte strikes",      RogueTank,                15, { 61170, 61171, 61172 } },
        { "evasive resistance",   RogueTank,                25, { 61173, 61174, 61175 } },
        { "execute strikes",      RogueCombat,              35, { 61176, 61177, 61178 } },
        { "frontal backstab",     RogueCombat,              40, { 61179 } },
        { "tank toughness",       RogueTank,                35, { 61180, 61181, 61182 } },
        { "ghostly magic dodge",  RogueTank,                40, { 61183 } },
        // Subtlety (4.2)
        { "stealth damage",       RogueSubtlety,            10, { 61184, 61185, 61186, 61187 } },
        { "hemorrhage stacks",    RogueSubtlety,            35, { 61188 } },
        { "shadow damage",        RogueSubtlety,            40, { 61189, 61190, 61191 } },
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
        { "spit",         RogueTank, { { 12, 61141 } } },
        { "shadow dance", RogueTank, { { 20, 61143 }, { 40, 61144 }, { 60, 61145 } } },
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

// #367 / twow-repo#409 stage 2: once a class's aura talents are real Talent.dbc talents
// (AiPlayerbot.SpecAura.TalentClasses), the rank spells keep their IDs but the talent
// system owns them. SpecAura must then neither grant nor remove them (a removal would
// unlearn a talent the bot bought), and the premade links spend those points on the
// talents instead of reserving them. The kit (KitFor) is no talent and stays granted.
template <class ClassList>
inline bool AuraTalentsAreReal(std::uint8_t cls, ClassList const& talentClasses)
{
    for (auto const talentClass : talentClasses)
        if (talentClass == cls)
            return true;
    return false;
}

inline std::vector<std::uint32_t> ManagedWantedAuras(std::uint8_t cls, std::uint8_t path, std::uint32_t level, bool auraTalentsAreReal)
{
    return auraTalentsAreReal ? std::vector<std::uint32_t>() : WantedAuras(cls, path, level);
}

inline std::vector<std::uint32_t> ManagedAllAuras(std::uint8_t cls, bool auraTalentsAreReal)
{
    return auraTalentsAreReal ? std::vector<std::uint32_t>() : AllAuras(cls);
}

// Owner decision P-1 (twow-repo#367, 2026-09-28): with the real talents every rogue can
// learn the kit at the trainer, so a bot of another path may know it. SpecAura still
// grants the kit to its path (4.3), but no longer takes it from anyone.
inline std::vector<std::uint32_t> ManagedAllKit(std::uint8_t cls, bool auraTalentsAreReal)
{
    return auraTalentsAreReal ? std::vector<std::uint32_t>() : AllKit(cls);
}

inline std::uint32_t ManagedReservedPoints(std::uint8_t cls, std::uint8_t path, std::uint32_t level, bool auraTalentsAreReal)
{
    return auraTalentsAreReal ? 0 : ReservedPoints(cls, path, level);
}
}
