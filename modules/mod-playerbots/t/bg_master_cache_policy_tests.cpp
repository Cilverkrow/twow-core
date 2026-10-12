#include "BgMasterCachePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <vector>

// twow-repo#541 (audit A09): the const-reference lookup gives the same entries, order and decisions as the
// old by-value copy with operator[], and never inserts into the shared cache.

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Stand-ins with the core's shape (Team / BattleGroundTypeId are plain enums).
enum Team { TEAM_NONE = 0, TEAM_BOTH_ALLOWED = 1, HORDE = 67, ALLIANCE = 469 };
enum BgType { BG_NONE = 0, BG_AV = 1, BG_WS = 2, BG_AB = 3, BG_C4 = 4, BG_C5 = 5, BG_C6 = 6 };
using Cache = std::map<Team, std::map<BgType, std::list<std::uint32_t>>>;

Cache Fixture()
{
    Cache c;
    c[TEAM_BOTH_ALLOWED][BG_WS] = { 900, 901 };
    c[ALLIANCE][BG_AV] = { 100, 101, 102 };
    c[ALLIANCE][BG_WS] = { 110 };
    c[ALLIANCE][BG_AB] = { 120, 121 };
    c[HORDE][BG_AV] = { 200 };
    c[HORDE][BG_AB] = { 220, 221, 222 };
    c[HORDE][BG_C4] = { };            // present but empty
    return c;
}

// Old BgMastersValue path, verbatim shape: copy, then operator[] on the copy.
std::vector<std::uint32_t> OldEntries(Cache const& shared, BgType bg)
{
    Cache copy = shared;
    std::vector<std::uint32_t> entries;
    entries.insert(entries.end(), copy[TEAM_BOTH_ALLOWED][bg].begin(), copy[TEAM_BOTH_ALLOWED][bg].end());
    entries.insert(entries.end(), copy[ALLIANCE][bg].begin(), copy[ALLIANCE][bg].end());
    entries.insert(entries.end(), copy[HORDE][bg].begin(), copy[HORDE][bg].end());
    return entries;
}

std::vector<std::uint32_t> NewEntries(Cache const& shared, BgType bg)
{
    std::vector<std::uint32_t> entries;
    ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(shared, TEAM_BOTH_ALLOWED, bg));
    ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(shared, ALLIANCE, bg));
    ai::bgmaster::Append(entries, ai::bgmaster::FindEntries(shared, HORDE, bg));
    return entries;
}

// Old RpgBgTypeValue inner block: copy per queue type, neutral list first, then the bot's team.
BgType OldPick(Cache const& shared, Team botTeam, std::uint32_t target, std::vector<BgType> const& queueTypes)
{
    for (BgType bg : queueTypes)
    {
        Cache copy = shared;
        for (auto& entry : copy[TEAM_BOTH_ALLOWED][bg])
            if (entry == target)
                return bg;
        for (auto& entry : copy[botTeam][bg])
            if (entry == target)
                return bg;
    }
    return BG_NONE;
}

BgType NewPick(Cache const& shared, Team botTeam, std::uint32_t target, std::vector<BgType> const& queueTypes)
{
    Cache const* ref = &shared;
    for (BgType bg : queueTypes)
    {
        if (ai::bgmaster::Contains(ai::bgmaster::FindEntries(*ref, TEAM_BOTH_ALLOWED, bg), target))
            return bg;
        if (ai::bgmaster::Contains(ai::bgmaster::FindEntries(*ref, botTeam, bg), target))
            return bg;
    }
    return BG_NONE;
}
}

int main()
{
    Cache const shared = Fixture();
    Cache const snapshot = shared;
    std::vector<BgType> const allBg = { BG_NONE, BG_AV, BG_WS, BG_AB, BG_C4, BG_C5, BG_C6 };
    std::vector<Team> const allTeams = { TEAM_NONE, TEAM_BOTH_ALLOWED, HORDE, ALLIANCE };

    // FindEntries: absent team, absent bg type and empty list.
    Require(ai::bgmaster::FindEntries(shared, TEAM_NONE, BG_AV) == nullptr, "absent team -> nullptr");
    Require(ai::bgmaster::FindEntries(shared, HORDE, BG_WS) == nullptr, "absent bg type -> nullptr");
    Require(ai::bgmaster::FindEntries(shared, HORDE, BG_C4) != nullptr && ai::bgmaster::FindEntries(shared, HORDE, BG_C4)->empty(), "present empty list");
    Require(ai::bgmaster::FindEntries(shared, ALLIANCE, BG_AV) == &shared.at(ALLIANCE).at(BG_AV), "points into the shared map, no copy");
    Require(!ai::bgmaster::Contains<std::list<std::uint32_t>>(nullptr, 100u), "nullptr contains nothing");

    // BgMastersValue: same entries in the same order for every bg type.
    for (BgType bg : allBg)
        Require(OldEntries(shared, bg) == NewEntries(shared, bg), "bg masters entries equal the copy path");

    // RpgBgTypeValue: same decision for every team, target and queue order.
    std::vector<std::uint32_t> targets = { 0, 1, 100, 101, 102, 110, 120, 121, 200, 220, 221, 222, 900, 901, 999 };
    std::vector<std::vector<BgType>> orders = { { BG_AV, BG_WS, BG_AB, BG_C4, BG_C5 }, { BG_C5, BG_C4, BG_AB, BG_WS, BG_AV }, { BG_WS }, { } };
    for (Team team : allTeams)
        for (std::uint32_t target : targets)
            for (auto const& order : orders)
                Require(OldPick(shared, team, target, order) == NewPick(shared, team, target, order), "rpg bg type decision equals the copy path");

    // Spot checks: neutral first, the other faction's master never matches.
    Require(NewPick(shared, HORDE, 901, allBg) == BG_WS, "neutral master matches for horde");
    Require(NewPick(shared, HORDE, 100, allBg) == BG_NONE, "alliance master does not match for horde");
    Require(NewPick(shared, ALLIANCE, 120, allBg) == BG_AB, "own master matches");

    // Nothing was inserted into the shared cache.
    Require(shared == snapshot, "shared cache unchanged");
    Require(shared.size() == 3 && shared.at(HORDE).size() == 3, "no new keys");

    // Empty cache (RandomBotJoinBG = 0: never loaded).
    Cache const empty;
    for (BgType bg : allBg)
        Require(NewEntries(empty, bg).empty() && OldEntries(empty, bg).empty(), "empty cache -> no entries");
    Require(NewPick(empty, ALLIANCE, 100, allBg) == BG_NONE, "empty cache -> none");
    Require(empty.empty(), "empty cache stays empty");

    std::cout << "bg_master_cache_policy_tests passed\n";
    return 0;
}
