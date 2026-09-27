#include "SpecAuraPolicy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

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

bool Has(std::vector<std::uint32_t> const& ids, std::uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}

int main()
{
    using namespace ai::spec_aura;

    // Paths: 7.1 "enhancement" and 7.3 "shaman tank" only.
    Require(PathFor(7, "enhancement") == Enhancement, "7.1 path");
    Require(PathFor(7, "shaman tank") == ShamanTank, "7.3 path");
    Require(PathFor(7, "elemental") == 0 && PathFor(7, "") == 0, "other shaman paths get nothing");
    Require(PathFor(4, "shaman tank") == 0, "only shamans");

    // O-12 point values (owner 2026-09-27; Ghost Wolf rank 3 is a talent for everyone):
    // 7.1 = 14, 7.3 = 21 at 60.
    Require(ReservedPoints(7, Enhancement, 60) == 14, "7.1 pays 14 points at 60");
    Require(ReservedPoints(7, ShamanTank, 60) == 21, "7.3 pays 21 points at 60");
    Require(ReservedPoints(7, ShamanTank, 9) == 0, "nothing before level 10");
    Require(ReservedPoints(7, ShamanTank, 10) == 2, "L10: attack speed 1 + defense 1");
    Require(ReservedPoints(7, Enhancement, 10) == 1, "L10: attack speed 1");
    Require(ReservedPoints(7, 0, 60) == 0, "no path, no reserve");

    // Highest due rank only; a higher rank replaces the lower one.
    std::vector<std::uint32_t> const tank12 = WantedAuras(7, ShamanTank, 12);
    Require(tank12.size() == 2 && Has(tank12, 90102) && Has(tank12, 90107), "L12 tank: attack speed 3, defense 3");
    Require(!Has(tank12, 90100) && !Has(tank12, 90101), "lower ranks are not wanted");
    Require(WantedAuras(7, ShamanTank, 9).empty(), "nothing before level 10");

    Require(!Has(AllAuras(7), 90110), "no Ghost Wolf aura (Improved Ghost Wolf 2/2 for everyone)");
    Require(!Has(WantedAuras(7, Enhancement, 60), 90105 + 4), "defense is tank only");
    Require(!Has(WantedAuras(7, ShamanTank, 60), 90122), "storm wisdom is 7.1 only");

    std::vector<std::uint32_t> const tank60 = WantedAuras(7, ShamanTank, 60);
    Require(tank60.size() == 7, "7.3 has seven aura talents at 60");
    Require(Has(tank60, 90104) && Has(tank60, 90109) && Has(tank60, 90113) &&
            Has(tank60, 90116) && Has(tank60, 90117) && Has(tank60, 90128) && Has(tank60, 90129), "7.3 top ranks at 60");
    std::vector<std::uint32_t> const enh60 = WantedAuras(7, Enhancement, 60);
    Require(enh60.size() == 4 && Has(enh60, 90104) && Has(enh60, 90113) &&
            Has(enh60, 90122) && Has(enh60, 90124), "7.1 top ranks at 60");

    // Every granted spell lies in the shaman block 90100-90129 (O-14).
    std::vector<std::uint32_t> const all = AllAuras(7);
    Require(all.size() == 27, "27 aura ranks in the table");
    for (std::uint32_t id : all)
        Require(id >= 90100 && id <= 90129, "aura ids in 90100-90129");

    // Classes without a table get nothing.
    Require(AllAuras(1).empty() && ReservedPoints(1, 1, 60) == 0, "other classes have no auras");
    Require(PathFor(4, "combat") == RogueCombat && PathFor(4, "rogue tank") == RogueTank, "rogue paths");

    // #367 rogue (OB-20 IDs, OB-10 path assignment): points at 60.
    Require(ReservedPoints(4, RogueCombat, 60) == 9, "4.0 combat pays 9");
    Require(ReservedPoints(4, RogueAssassination, 60) == 9, "4.1 assassination pays 9");
    Require(ReservedPoints(4, RogueSubtlety, 60) == 8, "4.2 subtlety pays 8");
    Require(ReservedPoints(4, RogueTank, 60) == 20, "4.3 rogue tank pays 20");
    std::vector<std::uint32_t> const rogueAll = AllAuras(4);
    Require(rogueAll.size() == 41, "41 rogue aura ranks");
    for (std::uint32_t id : rogueAll)
        Require(id >= 90150 && id <= 90190, "rogue aura ids in 90150-90190 (90191-90193 are script helpers)");
    Require(Has(WantedAuras(4, RogueTank, 14), 90163) && Has(WantedAuras(4, RogueTank, 14), 90168), "L14 tank: agility 5, defense 5");
    Require(!Has(WantedAuras(4, RogueCombat, 60), 90168), "defense is tank only");

    // #367 kit for 4.3 (free): Spit at 12, Shadow Dance I/II/III at 20/40/60.
    Require(WantedKit(4, RogueTank, 11).empty(), "no kit before 12");
    Require(WantedKit(4, RogueTank, 12).size() == 1 && WantedKit(4, RogueTank, 12)[0] == 90140, "Spit at 12");
    std::vector<std::uint32_t> const kit45 = WantedKit(4, RogueTank, 45);
    Require(kit45.size() == 2 && Has(kit45, 90140) && Has(kit45, 90143) && !Has(kit45, 90142), "Shadow Dance II at 45");
    Require(Has(WantedKit(4, RogueTank, 60), 90144), "Shadow Dance III at 60");
    Require(WantedKit(4, RogueCombat, 60).empty(), "the kit is tank only");
    Require(ReservedPoints(4, RogueTank, 60) == 20, "the kit costs no talent points");

    std::cout << "spec_aura_policy_tests passed\n";
    return 0;
}
