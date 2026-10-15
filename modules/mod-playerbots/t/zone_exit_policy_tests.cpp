// twow-repo#587 (ZONE-01): rescue continent, stranded counter, battleground quest sorts.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "ZoneExitPolicy.h"

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
    using namespace ai::zone_exit;

    // Z3: Warsong Gulch, Arathi Basin, Alterac Valley quests ("Fight for Warsong Gulch" 8372/8386 = sort 3277).
    Require(IsBattlegroundQuestSort(3277) && IsBattlegroundQuestSort(3358) && IsBattlegroundQuestSort(2597), "BG sorts");
    Require(!IsBattlegroundQuestSort(40) && !IsBattlegroundQuestSort(85) && !IsBattlegroundQuestSort(-162), "negative: zones and sorts");

    // Z1a: a human (home 0) in Teldrassil (1) is rescued home; on its home continent it stays there.
    Require(RescueContinent(1, 0) == 0, "off home: back to the home continent");
    Require(RescueContinent(0, 0) == 0 && RescueContinent(1, 1) == 1, "at home: same continent");

    // Z1b: Gwendolyn (4068) - only cross_map rejections, search after search.
    Require(OnlyOtherContinent(7, 0), "only the continent rule emptied the search");
    Require(!OnlyOtherContinent(7, 2), "negative: another reason (level, danger, hub) took candidates too");
    Require(!OnlyOtherContinent(0, 0), "negative: nothing offered at all");
    std::uint32_t count = 0;
    for (int i = 0; i < 3; ++i)
        count = NextStrandedCount(count, true);
    Require(count == 3 && StrandedRescueDue(count, 3), "third such search in a row: rescue");
    Require(NextStrandedCount(2, false) == 0, "any other outcome resets the count");
    Require(!StrandedRescueDue(2, 3), "two are not enough");
    Require(!StrandedRescueDue(100, 0), "threshold 0: off");

    std::cout << "zone_exit_policy_tests passed\n";
    return 0;
}
