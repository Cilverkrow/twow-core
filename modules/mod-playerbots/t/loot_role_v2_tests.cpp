// twow-repo#308 LOOT-ROLE-01 v2: the owner stat table as weight scales - priorities, units, the owner's own example.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "LootRoleV2Weights.h"

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

ai::loot_v2::Row const* Find(std::uint8_t cls, char const* spec)
{
    for (ai::loot_v2::Row const& row : ai::loot_v2::Rows)
        if (row.cls == cls && std::string(row.spec) == spec)
            return &row;
    return nullptr;
}

// An item's score for one row: field value x weight, as RandomItemMgr::CalculateSingleStatWeight adds it up.
std::uint32_t Score(ai::loot_v2::Row const& row, std::map<std::string, std::uint32_t> const& item)
{
    std::uint32_t total = 0;
    for (auto const& [field, weight] : ai::loot_v2::RowWeights(row))
    {
        auto const it = item.find(field);
        if (it != item.end())
            total += it->second * weight;
    }
    return total;
}
}

int main()
{
    using namespace ai::loot_v2;

    Require(sizeof(Rows) / sizeof(Rows[0]) == 28, "28 specs (tank shaman / tank rogue stay out until the owner's OK)");

    // README owner example (paladin): 10 str / 3 sta against 3 str / 15 sta.
    Row const* ret = Find(2, "retribution");
    Row const* prot = Find(2, "protection");
    Require(ret && prot, "paladin rows");
    std::map<std::string, std::uint32_t> const strItem = { { "str", 10 }, { "sta", 3 } };
    std::map<std::string, std::uint32_t> const staItem = { { "str", 3 }, { "sta", 15 } };
    Require(Score(*ret, strItem) == 1120 && Score(*ret, staItem) == 900, "retribution 1120 : 900 - takes strength");
    Require(Score(*prot, strItem) == 320 && Score(*prot, staItem) == 660, "protection 320 : 660 - takes stamina");

    // Units: 1 % = 10 units, AP 2 points = 1 unit, 1 mp5 = 2 units, armour per 10 points.
    Row const* fury = Find(1, "fury");
    Require(Score(*fury, { { "critstrkrtng", 1 } }) == 80 * 10, "fury: 1 % crit = prio2 x 10");
    Row const* combat = Find(4, "combat");
    Require(Score(*combat, { { "atkpwr", 10 } }) == 10 * (20 / 2), "combat: 10 AP = 5 units at prio5");
    Row const* holyPriest = Find(5, "holy");
    Require(Score(*holyPriest, { { "manargn", 4 } }) == 4 * 40 * 2, "holy priest: 4 mp5 = 8 units at prio4");
    Row const* bear = Find(11, "bear");
    Require(bear && std::string(bear->scale) == "feraltank" && Score(*bear, { { "armor", 100 } }) == 100 * (60 / 10),
        "bear: armour per 10 points at prio3, on the bear scale");
    Row const* cat = Find(11, "feral");
    Require(cat && std::string(cat->scale) == "feraldps" && Score(*cat, { { "feratkpwr", 20 } }) == 20 * (60 / 2),
        "cat: feral attack power counts as AP");

    // Spell power: all schools plus the row's own school; healing only for healers.
    Row const* fire = Find(8, "fire");
    Require(Score(*fire, { { "splpwr", 10 } }) == 1000 && Score(*fire, { { "firsplpwr", 10 } }) == 1000 &&
        Score(*fire, { { "frosplpwr", 10 } }) == 0, "fire mage: generic and fire spell power, not frost");
    Require(Score(*holyPriest, { { "splheal", 10 } }) == 10 * 60, "holy priest: healing at prio3");
    // Negative: a stat not in the 5 priorities counts 0 (no blanket stamina, owner 26.09.).
    Require(Score(*fire, { { "sta", 20 } }) == 0 && Score(*holyPriest, { { "str", 20 } }) == 0, "outside the 5: 0");

    // Weapon DPS stays from the old scale; nothing else does.
    Require(KeepFromOldScale("mledps") && KeepFromOldScale("rgddps") && !KeepFromOldScale("str") && !KeepFromOldScale("sta"),
        "only weapon DPS kept");

    std::cout << "loot_role_v2_tests passed\n";
    return 0;
}
