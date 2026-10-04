#ifndef TW_FUNSERVER_LOOT_RULES_H
#define TW_FUNSERVER_LOOT_RULES_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// twow-repo#482, owner rule table v5 (2026-10-02, train 9): raid loot by instance profile,
// dungeon loot by level band, the trim rule and the 16 client slots. Pure rules without
// server types, so the policy test covers them; LootMgr applies them.
//
// Raid profile per instance (Funserver.Loot.Raid.Maps):
//   set pieces  - Mix: by the class mix of the members present, only for classes where at
//                 least one member would get an upgrade (item level, +5 from 2 set pieces
//                 worn), at most SetMax; the fixed set drops of the original table count.
//               - Tokens: TokenMin sure, then one 33 % roll per further slot up to TokenMax.
//   own items   - OwnMin sure, then one 33 % roll per further slot up to OwnMax; duplicates
//                 keep 25 % of their selection weight. No BoE pool in raids.
// Dungeon band per instance (Funserver.Loot.Dungeon.Maps): Min from the own table (the 60+
// band at least blue), then one 33 % roll per further slot up to Max: own table first, the
// BoE pool once the own table has no distinct item left, at most BoeMax BoE items. Minimums
// apply as far as the own table and BoeMax allow.
namespace FunserverLootRules
{
    // The 1.12 client keeps loot slots in a fixed array of 16 (test 2026-10-02).
    uint32_t constexpr CLIENT_SLOTS = 16;
    float constexpr EXTRA_CHANCE = 0.33f;
    float constexpr DUPLICATE_FACTOR = 0.25f;
    uint32_t constexpr SET_UPGRADE_BONUS_ILVL = 5;
    uint32_t constexpr SET_UPGRADE_MIN_PIECES = 2;

    enum class SetMode : uint8_t
    {
        None   = 0,
        Mix    = 1,
        Tokens = 2,
    };

    struct RaidProfile
    {
        char const* name;
        SetMode setMode;
        uint32_t setMax;        // Mix
        uint32_t tokenMin;      // Tokens
        uint32_t tokenMax;
        uint32_t ownMin;
        uint32_t ownMax;
    };

    // Owner rule table v5 (#482 issuecomment-5959416171).
    RaidProfile constexpr RAID_PROFILES[] = {
        { "T1",    SetMode::Mix,    18, 0, 0, 6, 12 },   // Molten Core
        { "T2",    SetMode::Mix,    12, 0, 0, 4, 10 },   // Onyxia, Blackwing Lair
        { "T2ES",  SetMode::None,    0, 0, 0, 4, 10 },   // Emerald Sanctum: T2, no set pieces
        { "LK10",  SetMode::None,    0, 0, 0, 3,  6 },   // Lower Karazhan Halls (10 players)
        { "T25",   SetMode::Tokens,  0, 1, 9, 3,  9 },   // Ahn'Qiraj Temple
        { "T3",    SetMode::Tokens,  0, 1, 6, 3,  8 },   // Naxxramas
        { "T35",   SetMode::Tokens,  0, 1, 3, 2,  6 },   // Tower of Karazhan
        { "TOK20", SetMode::Tokens,  0, 1, 8, 2,  6 },   // Timbermaw Hold, Zul'Gurub, AQ20
    };

    constexpr char const* DEFAULT_RAID_MAPS =
        "409:T1,249:T2,469:T2,807:T2ES,532:LK10,531:T25,533:T3,814:T35,819:TOK20,309:TOK20,509:TOK20";

    struct DungeonBand
    {
        char const* name;
        uint32_t min;
        uint32_t max;
        uint32_t boeMax;
        uint32_t minQuality;    // quality of the Min items (3 = blue), 0 = any
    };

    DungeonBand constexpr DUNGEON_BANDS[] = {
        { "10-20", 1, 2, 1, 0 },
        { "20-30", 1, 3, 1, 0 },
        { "30-40", 2, 4, 1, 0 },
        { "40-50", 2, 5, 2, 0 },
        { "50-60", 3, 6, 2, 0 },
        { "60+",   4, 8, 2, 3 },
    };

    // Wiki rule with the endgame rule (#482 v3/v4, owner: Uldaman 40-50, BRS 60+).
    constexpr char const* DEFAULT_DUNGEON_MAPS =
        "389:10-20,822:10-20,"
        "36:20-30,43:20-30,33:20-30,34:20-30,48:20-30,820:20-30,816:20-30,"
        "90:30-40,47:30-40,802:30-40,818:30-40,189:30-40,"
        "129:40-50,70:40-50,815:40-50,209:40-50,"
        "349:50-60,109:50-60,808:50-60,230:50-60,"
        "289:60+,329:60+,429:60+,229:60+,800:60+,269:60+";

    // Token items of the token raids (Funserver.Loot.Raid.Tokens). Built from the live data
    // 2026-10-04 (#482): epic own-table items that a class quest requires (AQ40, Naxx, Tower of
    // Karazhan, ZG) plus the AQ20 drapes/rings/hilts and the Timbermaw "Ritualistic" items, whose
    // quests are open to every class. Heads, Atiesh parts and other quest items are no tokens.
    constexpr char const* DEFAULT_RAID_TOKENS =
        "531:20926,20928-20930,20932-20933;"
        "533:22352-22372,55581-55583;"
        "814:55482-55490;"
        "309:19716-19724;"
        "509:20884-20886,20888-20890;"
        "819:33335,33338-33340";

    // "531:1,3-5;533:7" -> {531: [1, 3, 4, 5], 533: [7]}; malformed parts are skipped,
    // a range spans at most 1000 ids.
    inline std::map<uint32_t, std::vector<uint32_t>> ParseTokenLists(std::string const& text)
    {
        std::map<uint32_t, std::vector<uint32_t>> out;
        std::stringstream maps(text);
        std::string mapPart;
        while (std::getline(maps, mapPart, ';'))
        {
            std::string::size_type const colon = mapPart.find(':');
            if (colon == std::string::npos || colon == 0)
                continue;
            std::string const id = mapPart.substr(0, colon);
            if (id.find_first_not_of("0123456789 ") != std::string::npos)
                continue;
            uint32_t const mapId = uint32_t(std::stoul(id));
            std::stringstream items(mapPart.substr(colon + 1));
            std::string item;
            while (std::getline(items, item, ','))
            {
                if (item.empty() || item.find_first_not_of("0123456789- ") != std::string::npos)
                    continue;
                std::string::size_type const dash = item.find('-');
                if (dash == std::string::npos)
                {
                    out[mapId].push_back(uint32_t(std::stoul(item)));
                    continue;
                }
                if (dash == 0 || dash + 1 >= item.size())
                    continue;
                uint32_t const lo = uint32_t(std::stoul(item.substr(0, dash)));
                uint32_t const hi = uint32_t(std::stoul(item.substr(dash + 1)));
                if (lo > hi || hi - lo > 1000)
                    continue;
                for (uint32_t v = lo; v <= hi; ++v)
                    out[mapId].push_back(v);
            }
        }
        return out;
    }

    inline RaidProfile const* FindRaidProfile(std::string const& name)
    {
        for (RaidProfile const& p : RAID_PROFILES)
            if (name == p.name)
                return &p;
        return nullptr;
    }

    inline DungeonBand const* FindDungeonBand(std::string const& name)
    {
        for (DungeonBand const& b : DUNGEON_BANDS)
            if (name == b.name)
                return &b;
        return nullptr;
    }

    // "409:T1,469:T2" -> {409: "T1", 469: "T2"}; malformed entries are skipped.
    inline std::map<uint32_t, std::string> ParseMapNames(std::string const& text)
    {
        std::map<uint32_t, std::string> out;
        std::stringstream entries(text);
        std::string entry;
        while (std::getline(entries, entry, ','))
        {
            std::string::size_type const colon = entry.find(':');
            if (colon == std::string::npos || colon == 0 || colon + 1 >= entry.size())
                continue;
            std::string const id = entry.substr(0, colon);
            if (id.find_first_not_of("0123456789 ") != std::string::npos)
                continue;
            std::string name = entry.substr(colon + 1);
            name.erase(0, name.find_first_not_of(' '));
            name.erase(name.find_last_not_of(' ') + 1);
            out[uint32_t(std::stoul(id))] = name;
        }
        return out;
    }

    // Min sure, then one roll per further slot up to Max. roll() returns [0, 1).
    template <class RollFn>
    uint32_t RollCount(uint32_t min, uint32_t max, float chance, RollFn roll)
    {
        uint32_t count = std::min(min, max);
        for (uint32_t slot = count; slot < max; ++slot)
            if (roll() < chance)
                ++count;
        return count;
    }

    struct DungeonPlan
    {
        uint32_t own = 0;   // items from the boss's own table (distinct while possible)
        uint32_t boe = 0;   // items from the BoE pool
    };

    // ownDistinct: distinct items the own table can still give (for the Min part: at the band's
    // minimum quality). The own table comes first; the BoE pool only fills what the own table
    // cannot, never beyond boeMax. Minimums hold as far as possible.
    template <class RollFn>
    DungeonPlan PlanDungeon(DungeonBand const& band, uint32_t ownDistinct, RollFn roll)
    {
        DungeonPlan plan;
        plan.own = std::min(band.min, ownDistinct);
        plan.boe = std::min(band.min - plan.own, band.boeMax);
        for (uint32_t slot = plan.own + plan.boe; slot < band.max; ++slot)
        {
            if (!(roll() < EXTRA_CHANCE))
                continue;
            if (plan.own < ownDistinct)
                ++plan.own;
            else if (plan.boe < band.boeMax)
                ++plan.boe;
            else
                break;                      // nothing left to give
        }
        return plan;
    }

    // Set-piece upgrade: the piece beats the equipped item of its slot by item level; with at
    // least SET_UPGRADE_MIN_PIECES of the set worn it counts SET_UPGRADE_BONUS_ILVL higher.
    // An empty slot (equippedItemLevel 0) is always an upgrade.
    inline bool IsSetUpgrade(uint32_t setItemLevel, uint32_t equippedItemLevel, uint32_t setPiecesWorn)
    {
        uint32_t const effective = setItemLevel + (setPiecesWorn >= SET_UPGRADE_MIN_PIECES ? SET_UPGRADE_BONUS_ILVL : 0);
        return effective > equippedItemLevel;
    }

    struct ClassNeed
    {
        uint32_t classId;
        uint32_t members;       // members of this class at reward distance (players and bots)
        uint32_t upgraders;     // of those: members with an upgrade from this boss's set piece
        uint32_t fixedDrops;    // set pieces of this class already dropped by the original table
    };

    // Set pieces per class on top of the fixed drops (Mix mode). The total, fixed drops
    // included, is at most setMax and never more than the members who would upgrade. Shares
    // follow the class mix of all members present (largest remainder); classes without an
    // upgrader get nothing.
    inline std::vector<uint32_t> AllocateMix(std::vector<ClassNeed> const& classes, uint32_t setMax)
    {
        std::vector<uint32_t> extra(classes.size(), 0);
        uint32_t fixedTotal = 0;
        uint32_t room = 0;          // pieces a class can still take
        uint32_t members = 0;
        for (ClassNeed const& c : classes)
        {
            fixedTotal += c.fixedDrops;
            if (c.upgraders > c.fixedDrops)
            {
                room += c.upgraders - c.fixedDrops;
                members += c.members;
            }
        }
        uint32_t remaining = setMax > fixedTotal ? std::min(setMax - fixedTotal, room) : 0;
        if (!remaining || !members)
            return extra;

        // Largest remainder over the eligible classes, capped by each class's room; whatever
        // a capped class cannot take goes round again.
        while (remaining)
        {
            uint32_t eligibleMembers = 0;
            for (size_t i = 0; i < classes.size(); ++i)
                if (classes[i].upgraders > classes[i].fixedDrops + extra[i])
                    eligibleMembers += classes[i].members;
            if (!eligibleMembers)
                break;

            uint32_t const round = remaining;
            std::vector<std::pair<uint64_t, size_t>> remainders;
            uint32_t given = 0;
            for (size_t i = 0; i < classes.size(); ++i)
            {
                ClassNeed const& c = classes[i];
                uint32_t const cap = c.upgraders > c.fixedDrops + extra[i] ? c.upgraders - c.fixedDrops - extra[i] : 0;
                if (!cap)
                    continue;
                uint64_t const share = uint64_t(round) * c.members;
                uint32_t const whole = std::min<uint32_t>(uint32_t(share / eligibleMembers), cap);
                extra[i] += whole;
                given += whole;
                if (whole < cap)
                    remainders.emplace_back(share % eligibleMembers, i);
            }
            std::stable_sort(remainders.begin(), remainders.end(),
                [](auto const& a, auto const& b) { return a.first > b.first; });
            for (auto const& r : remainders)
            {
                if (given >= round)
                    break;
                ++extra[r.second];
                ++given;
            }
            if (!given)
                break;
            remaining -= std::min(given, remaining);
        }
        return extra;
    }

    // Trim rule (owner v5): when the loot exceeds the limit, cut normal loot first, then set
    // pieces down to need, then the extra own rolls. Mandatory items, fixed original set drops
    // and own items up to the minimum are never cut.
    struct LootCounts
    {
        uint32_t mandatory = 0;     // quest items, head, keys
        uint32_t fixedSet = 0;      // set drops of the original table
        uint32_t ownMin = 0;        // own items up to the minimum
        uint32_t set = 0;           // set pieces or tokens on top of fixedSet
        uint32_t ownExtra = 0;      // own items above the minimum (33 % rolls)
        uint32_t normal = 0;        // blizzlike rest: gems, world drops, recipes, grey

        uint32_t Total() const { return mandatory + fixedSet + ownMin + set + ownExtra + normal; }
    };

    // setNeed: set pieces on top of fixedSet that cover one member per class with an upgrade
    // (Mix) or one token per token type (Tokens).
    inline LootCounts Trim(LootCounts in, uint32_t limit, uint32_t setNeed)
    {
        auto cut = [&in, limit](uint32_t& part, uint32_t floor)
        {
            uint32_t const total = in.Total();
            if (total <= limit || part <= floor)
                return;
            uint32_t const over = total - limit;
            part -= std::min(over, part - floor);
        };
        cut(in.normal, 0);
        cut(in.set, std::min(setNeed, in.set));
        cut(in.ownExtra, 0);
        return in;
    }
}

#endif
