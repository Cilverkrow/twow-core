#include "CheatItemPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

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
}  // namespace

int main()
{
    using namespace ai::cheat_item;

    // twow-repo#333 M2 shadow counter: fixed columns, in this order.
    Counts counts;
    Require(!counts.Any(), "a new window is empty");
    Require(Format(counts) == "use=0 use_had=0 cast_item=0 cast_item_had=0 food=0 drink=0 poison=0 ammo_refill=0 ammo_init=0 refresh=0 cheat_cd_set=0 self_blocked=0",
        "all sites in fixed order");

    // Missing vs. held item.
    Require(UseSite(0) == Site::Use, "use: no item in the bags = missing");
    Require(UseSite(3) == Site::UseHad, "use: item in the bags = had");
    Require(CastItemSite(0) == Site::CastItem, "cast item: missing");
    Require(CastItemSite(1) == Site::CastItemHad, "cast item: had");

    // Ammo top-up amount.
    Require(RefillAmount(150, 200) == 50, "refill adds the gap to the full stack");
    Require(RefillAmount(200, 200) == 0, "a full stack adds nothing");
    Require(RefillAmount(250, 200) == 0, "never negative");

    // Sums, saturating.
    counts.Add(Site::AmmoRefill, 50);
    counts.Add(Site::AmmoRefill, 3);
    counts.Add(Site::Food, 1);
    Require(counts.Any(), "a counted window is not empty");
    Require(Format(counts) == "use=0 use_had=0 cast_item=0 cast_item_had=0 food=1 drink=0 poison=0 ammo_refill=53 ammo_init=0 refresh=0 cheat_cd_set=0 self_blocked=0",
        "increments are summed per site");
    counts.Add(Site::Use, std::numeric_limits<std::uint32_t>::max());
    counts.Add(Site::Use, 5);
    Require(counts.n[static_cast<std::size_t>(Site::Use)] == std::numeric_limits<std::uint32_t>::max(), "saturates, no wrap");
    counts.Add(Site::Count, 1);
    Require(counts.n[static_cast<std::size_t>(Site::SelfBlocked)] == 0, "an out-of-range site is ignored");
    counts.Reset();
    Require(!counts.Any(), "reset empties the window");

    // Window.
    Require(kFlushSeconds == 600, "ten-minute window");
    Require(!ShouldFlush(1000, 0), "the first call only starts the window");
    Require(!ShouldFlush(1599, 1000), "inside the window");
    Require(ShouldFlush(1600, 1000), "window over");

    std::cout << "cheat_item_policy_tests passed\n";
    return 0;
}
