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
    // 7.1 = 14, 7.3 = 21 at 60; train 9 (#484): Charged Stormstrike has 4 ranks, 7.3 = 24.
    Require(ReservedPoints(7, Enhancement, 60) == 14, "7.1 pays 14 points at 60");
    Require(ReservedPoints(7, ShamanTank, 60) == 24, "7.3 pays 24 points at 60");
    Require(ReservedPoints(7, ShamanTank, 9) == 0, "nothing before level 10");
    Require(ReservedPoints(7, ShamanTank, 10) == 2, "L10: attack speed 1 + defense 1");
    Require(ReservedPoints(7, Enhancement, 10) == 1, "L10: attack speed 1");
    Require(ReservedPoints(7, 0, 60) == 0, "no path, no reserve");

    // Highest due rank only; a higher rank replaces the lower one.
    std::vector<std::uint32_t> const tank12 = WantedAuras(7, ShamanTank, 12);
    Require(tank12.size() == 2 && Has(tank12, 61103) && Has(tank12, 61108), "L12 tank: attack speed 3, defense 3");
    Require(!Has(tank12, 61101) && !Has(tank12, 61102), "lower ranks are not wanted");
    Require(WantedAuras(7, ShamanTank, 9).empty(), "nothing before level 10");

    Require(!Has(AllAuras(7), 61111), "no Ghost Wolf aura (Improved Ghost Wolf 2/2 for everyone)");
    Require(!Has(WantedAuras(7, Enhancement, 60), 61106 + 4), "defense is tank only");
    Require(!Has(WantedAuras(7, ShamanTank, 60), 61123), "storm wisdom is 7.1 only");

    std::vector<std::uint32_t> const tank60 = WantedAuras(7, ShamanTank, 60);
    Require(tank60.size() == 7, "7.3 has seven aura talents at 60");
    Require(Has(tank60, 61105) && Has(tank60, 61110) && Has(tank60, 61114) &&
            Has(tank60, 61117) && Has(tank60, 61225) && Has(tank60, 61129) && Has(tank60, 61130), "7.3 top ranks at 60");
    Require(!Has(tank60, 61118), "Charged Stormstrike rank 4 replaces rank 1");
    Require(Has(WantedAuras(7, ShamanTank, 30), 61118) && Has(WantedAuras(7, ShamanTank, 31), 61223) &&
            Has(WantedAuras(7, ShamanTank, 33), 61225), "Charged Stormstrike ranks 1-4 at 30-33");
    std::vector<std::uint32_t> const enh60 = WantedAuras(7, Enhancement, 60);
    Require(enh60.size() == 4 && Has(enh60, 61105) && Has(enh60, 61114) &&
            Has(enh60, 61123) && Has(enh60, 61125), "7.1 top ranks at 60");

    // Every granted spell lies in the shaman block 61101-61130 (O-14) or is a Charged
    // Stormstrike rank 2-4 (61223-61225, train 9).
    std::vector<std::uint32_t> const all = AllAuras(7);
    Require(all.size() == 30, "30 aura ranks in the table");
    for (std::uint32_t id : all)
        Require((id >= 61101 && id <= 61130) || (id >= 61223 && id <= 61225), "aura ids in 61101-61130 or 61223-61225");

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
        Require(id >= 61151 && id <= 61191, "rogue aura ids in 61151-61191 (61192-61194 are script helpers)");
    Require(Has(WantedAuras(4, RogueTank, 14), 61164) && Has(WantedAuras(4, RogueTank, 14), 61169), "L14 tank: agility 5, defense 5");
    Require(!Has(WantedAuras(4, RogueCombat, 60), 61169), "defense is tank only");

    // #367 kit for 4.3 (free): Spit at 12, Shadow Dance I/II/III at 20/40/60.
    Require(WantedKit(4, RogueTank, 11).empty(), "no kit before 12");
    Require(WantedKit(4, RogueTank, 12).size() == 1 && WantedKit(4, RogueTank, 12)[0] == 61141, "Spit at 12");
    std::vector<std::uint32_t> const kit45 = WantedKit(4, RogueTank, 45);
    Require(kit45.size() == 2 && Has(kit45, 61141) && Has(kit45, 61144) && !Has(kit45, 61143), "Shadow Dance II at 45");
    Require(Has(WantedKit(4, RogueTank, 60), 61145), "Shadow Dance III at 60");
    Require(WantedKit(4, RogueCombat, 60).empty(), "the kit is tank only");
    Require(ReservedPoints(4, RogueTank, 60) == 20, "the kit costs no talent points");

    // twow-repo#409 stage 2: a class in AiPlayerbot.SpecAura.TalentClasses has real talents
    // with the same IDs. SpecAura must neither grant nor remove them, and nothing is reserved.
    std::vector<std::uint32_t> const noClasses;
    std::vector<std::uint32_t> const rogueReal = { 4 };
    Require(!AuraTalentsAreReal(4, noClasses), "empty list: phase 1 for everyone");
    Require(AuraTalentsAreReal(4, rogueReal) && !AuraTalentsAreReal(7, rogueReal), "only the listed class");
    Require(ManagedWantedAuras(4, RogueTank, 60, true).empty(), "real talents: no aura grant");
    Require(ManagedAllAuras(4, true).empty(), "real talents: nothing to remove, a bought talent stays");
    Require(ManagedReservedPoints(4, RogueTank, 60, true) == 0, "real talents: the links spend the points");
    Require(ManagedWantedAuras(4, RogueTank, 60, false) == WantedAuras(4, RogueTank, 60) &&
            ManagedAllAuras(4, false) == AllAuras(4) &&
            ManagedReservedPoints(4, RogueTank, 60, false) == 20, "phase 1 unchanged");
    Require(WantedKit(4, RogueTank, 60).size() == 2, "the kit stays with real talents (no talent)");
    Require(ManagedAllKit(4, true).empty(), "P-1: every rogue may learn the kit at the trainer, none loses it");
    Require(ManagedAllKit(4, false) == AllKit(4), "phase 1: the kit is removed from other paths as before");

    // #357 stage 2 (core#217): the shaman uses the same switch ("7" in the list).
    std::vector<std::uint32_t> const bothReal = { 4, 7 };
    Require(AuraTalentsAreReal(7, bothReal) && AuraTalentsAreReal(4, bothReal), "shaman and rogue listed");
    Require(!AuraTalentsAreReal(7, rogueReal), "shaman not listed: phase 1");
    Require(ManagedAllAuras(7, true).empty() && ManagedReservedPoints(7, ShamanTank, 60, true) == 0,
            "real shaman talents: nothing removed, nothing reserved");

    std::cout << "spec_aura_policy_tests passed\n";
    return 0;
}
