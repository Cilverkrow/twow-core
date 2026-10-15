// twow-repo#308 LOOT-ROLE-01: the weight scale from the premade path, bear/cat fixed, the quest reward by gain.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "LootRolePolicy.h"

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
}

int main()
{
    using namespace ai::loot_role;

    // Below level 10 the talent tree gave every warrior prot and every paladin retribution; the path decides now.
    Require(ScaleForPath(1, "fury", 5) == "fury" && ScaleForPath(1, "protection", 5) == "prot", "warrior damage and tank");
    Require(ScaleForPath(2, "holy", 3) == "holy" && ScaleForPath(2, "protection", 3) == "prot" &&
        ScaleForPath(2, "retribution", 3) == "retrib", "paladin healer, tank, damage");
    Require(ScaleForPath(7, "restoration", 4) == "resto" && ScaleForPath(7, "shaman tank", 30) == "enhance", "shaman");
    Require(ScaleForPath(5, "discipline", 8) == "disc" && ScaleForPath(9, "affliction", 8) == "afflic" &&
        ScaleForPath(3, "beastmastery", 8) == "beast" && ScaleForPath(4, "rogue tank", 30) == "combat", "scale names");
    Require(ScaleForPath(8, "frost", 12) == "frost" && ScaleForPath(11, "balance", 12) == "balance", "mage, druid");
    // Druid: bear always tank; cat from level 20 (cat form), a bear form fighter before.
    Require(ScaleForPath(11, "bear", 40) == "feraltank" && ScaleForPath(11, "feral", 40) == "feraldps" &&
        ScaleForPath(11, "feral", 15) == "feraltank" && ScaleForPath(11, "restoration", 15) == "resto", "druid paths");
    // Negative: no or unknown path, or the path of another class - the old talent-tree rule applies.
    Require(ScaleForPath(1, "", 5).empty() && ScaleForPath(1, "holy", 5).empty() && ScaleForPath(6, "frost", 60).empty(),
        "unknown path: old rule");

    // FeralFixedSpec: bear and cat by the path, the same answer on every check.
    for (int i = 0; i < 20; ++i)
    {
        bool const coin = i % 2 == 0;
        Require(!FeralDps(true, "bear", 40, 1001, coin) && FeralDps(true, "feral", 40, 1001, coin), "fixed by the path");
        Require(FeralDps(true, "", 40, 1001, coin) && !FeralDps(true, "", 40, 1002, coin), "no path: by guid, stable");
    }
    Require(!FeralDps(true, "feral", 19, 1001, true) && !FeralDps(false, "feral", 19, 1001, true), "below 20: bear form");
    Require(FeralDps(false, "bear", 40, 1002, true) && !FeralDps(false, "bear", 40, 1002, false), "switch off: the old coin");

    // Rewards (Senir's Observations style): 0 = mail Str, 1 = cloth Int (usable), 2 = a ring not usable.
    std::vector<Choice> choices(3);
    for (std::uint8_t i = 0; i < 3; ++i)
        choices[i].index = i;
    choices[0] = Choice{ 0, true, 120, 2, 10 };
    choices[1] = Choice{ 1, true, 300, 2, 10 };   // the healer's scale: Int counts
    choices[2] = Choice{ 2, false, 900, 3, 12 };   // not equippable: never chosen
    Require(BestByGain(choices) == 1, "largest gain among usable");
    Require(Pick(true, 0, BestByGain(choices)) == 1, "RoleWeights on: the best");
    Require(Pick(false, 0, BestByGain(choices)) == 0, "negative: switch off keeps the first");
    choices[1].gain = 120;
    choices[1].quality = 3;
    Require(BestByGain(choices) == 1, "equal gain: quality");
    choices[1].quality = 2;
    Require(BestByGain(choices) == 0, "all equal: list order");
    choices[0].equip = choices[1].equip = false;
    Require(BestByGain(choices) == -1 && Pick(true, 2, -1) == 2, "nothing usable: the first stays");

    std::cout << "loot_role_policy_tests passed\n";
    return 0;
}
