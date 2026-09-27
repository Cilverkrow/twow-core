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
    Require(ReservedPoints(Enhancement, 60) == 14, "7.1 pays 14 points at 60");
    Require(ReservedPoints(ShamanTank, 60) == 21, "7.3 pays 21 points at 60");
    Require(ReservedPoints(ShamanTank, 9) == 0, "nothing before level 10");
    Require(ReservedPoints(ShamanTank, 10) == 2, "L10: attack speed 1 + defense 1");
    Require(ReservedPoints(Enhancement, 10) == 1, "L10: attack speed 1");
    Require(ReservedPoints(0, 60) == 0, "no path, no reserve");

    // Highest due rank only; a higher rank replaces the lower one.
    std::vector<std::uint32_t> const tank12 = WantedAuras(ShamanTank, 12);
    Require(tank12.size() == 2 && Has(tank12, 90102) && Has(tank12, 90107), "L12 tank: attack speed 3, defense 3");
    Require(!Has(tank12, 90100) && !Has(tank12, 90101), "lower ranks are not wanted");
    Require(WantedAuras(ShamanTank, 9).empty(), "nothing before level 10");

    Require(!Has(AllAuras(), 90110), "no Ghost Wolf aura (Improved Ghost Wolf 2/2 for everyone)");
    Require(!Has(WantedAuras(Enhancement, 60), 90105 + 4), "defense is tank only");
    Require(!Has(WantedAuras(ShamanTank, 60), 90122), "storm wisdom is 7.1 only");

    std::vector<std::uint32_t> const tank60 = WantedAuras(ShamanTank, 60);
    Require(tank60.size() == 7, "7.3 has seven aura talents at 60");
    Require(Has(tank60, 90104) && Has(tank60, 90109) && Has(tank60, 90113) &&
            Has(tank60, 90116) && Has(tank60, 90117) && Has(tank60, 90128) && Has(tank60, 90129), "7.3 top ranks at 60");
    std::vector<std::uint32_t> const enh60 = WantedAuras(Enhancement, 60);
    Require(enh60.size() == 4 && Has(enh60, 90104) && Has(enh60, 90113) &&
            Has(enh60, 90122) && Has(enh60, 90124), "7.1 top ranks at 60");

    // Every granted spell lies in the shaman block 90100-90129 (O-14).
    std::vector<std::uint32_t> const all = AllAuras();
    Require(all.size() == 27, "27 aura ranks in the table");
    for (std::uint32_t id : all)
        Require(id >= 90100 && id <= 90129, "aura ids in 90100-90129");

    std::cout << "spec_aura_policy_tests passed\n";
    return 0;
}
