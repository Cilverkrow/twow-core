#include "RidingStagesBotPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

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
    using namespace ai::riding_stages;
    using FunserverRiding::MountedSpeedPct;

    // twow-repo#295: training 50 s / 5 g / 50 g / 500 g at level 10/20/40/60 comes from
    // the trainer ("train cost"); mount 1 costs 1 g, mount 2 100 g.
    Require(MountBudgetCopper(9, 0, false, false) == 0, "below level 10: nothing");
    Require(MountBudgetCopper(10, 5000, false, false) == 15000, "level 10: 50 s rank 1 + 1 g mount 1");
    Require(MountBudgetCopper(10, 0, true, false) == 0, "level 10 with a mount, rank 1 trained: nothing");
    Require(MountBudgetCopper(20, 50000, true, false) == 50000, "level 20: 5 g rank 2, mount 1 owned");
    Require(MountBudgetCopper(39, 0, true, false) == 0, "level 39: mount 2 not yet");
    Require(MountBudgetCopper(40, 500000, true, false) == 1500000, "level 40: 50 g rank 3 + 100 g mount 2");
    Require(MountBudgetCopper(40, 500000, false, false) == 1510000, "level 40 without any mount: both mounts");
    Require(MountBudgetCopper(45, 0, true, true) == 0, "swift mount owned, rank 3 trained: nothing");
    Require(MountBudgetCopper(60, 5000000, true, true) == 5000000, "level 60: 500 g rank 4");
    Require(MountBudgetCopper(60, 0xFFFFFFFFu, false, false) == 0xFFFFFFFFu, "no overflow");

    // The vendor trip: the item requirement (riding 75 / 225) and the stage level.
    Require(!MayBuyMount(9, 75, false, false), "level 9: no mount even with riding");
    Require(!MayBuyMount(10, 0, false, false), "level 10 without riding: train first");
    Require(!MayBuyMount(10, 74, false, false), "riding 74 is below rank 1");
    Require(MayBuyMount(10, 75, false, false), "level 10 with riding 75: mount 1");
    Require(!MayBuyMount(30, 150, true, false), "mount 1 owned below level 40: nothing to buy");
    Require(!MayBuyMount(40, 150, true, false), "level 40 with riding 150: train rank 3 first");
    Require(!MayBuyMount(40, 224, true, false), "riding 224 is below rank 3");
    Require(MayBuyMount(40, 225, true, false), "level 40 with riding 225: mount 2");
    Require(!MayBuyMount(39, 225, true, false), "mount 2 not before level 40");
    Require(MayBuyMount(40, 225, false, false), "no mount at all at stage 40: buy");
    Require(!MayBuyMount(60, 300, true, true), "swift mount owned: nothing");

    // An offered mount against the bot's own mounts, by effective speed.
    Require(ClassifyMountOffer(60, {}) == MountOffer::Upgrade, "first mount: buy");
    Require(ClassifyMountOffer(0, {}) == MountOffer::NotNeeded, "a mount without speed is no upgrade");
    Require(ClassifyMountOffer(100, { 100 }) == MountOffer::Keep, "as fast as an owned one: keep, do not buy");
    Require(ClassifyMountOffer(140, { 100 }) == MountOffer::Upgrade, "swift at rank 3 beats the slow mount");
    Require(ClassifyMountOffer(100, { 140 }) == MountOffer::NotNeeded, "slower than an owned mount");
    Require(ClassifyMountOffer(180, { 100, 180 }) == MountOffer::Keep, "the bagged item itself is kept");

    // The ranking the bot mounts by (core rule, stages on; aura-32 values 60 slow, 100
    // swift): the families tie at ranks 1/2 - no pointless remount - and the swift mount
    // wins at ranks 3/4, so a bot changes to it once it trains rank 3.
    for (std::uint32_t const skill : { 75u, 150u })
        Require(MountedSpeedPct(true, skill, 30, 60, false) == MountedSpeedPct(true, skill, 30, 100, false), "ranks 1/2: families tie");
    Require(MountedSpeedPct(true, 225, 40, 100, false) == 140 && MountedSpeedPct(true, 225, 40, 60, false) == 100, "rank 3: 140 vs 100");
    Require(MountedSpeedPct(true, 300, 60, 100, false) == 180 && MountedSpeedPct(true, 300, 60, 60, false) == 100, "rank 4: 180 vs 100");
    Require(MountedSpeedPct(true, 75, 30, 60, true) == 100, "SPEED_100 mounts keep 100 at rank 1");
    // A doubled form (Ghost Wolf / Travel Form +80 %) beats a rank-1 mount and loses to rank 2,
    // but it does not replace a mount in the budget (MayBuyMount knows only mounts).
    Require(80 > MountedSpeedPct(true, 75, 20, 60, false) && 80 < MountedSpeedPct(true, 150, 20, 60, false), "form vs mount");

    // The travel time budget.
    Require(TravelBudgetRunSpeed(false, 19.6f, 7.0f) == 19.6f, "switch off: the current speed as before");
    Require(TravelBudgetRunSpeed(true, 19.6f, 7.0f) == 7.0f, "mounted: the unmounted run speed");
    Require(TravelBudgetRunSpeed(true, 12.6f, 7.0f) == 7.0f, "ghost wolf: the run speed");
    Require(TravelBudgetRunSpeed(true, 3.5f, 7.0f) == 3.5f, "slowed: the slower speed");
    Require(TravelBudgetRunSpeed(true, 0.0f, 7.0f) == 7.0f, "no speed: the run speed, no division by zero");

    return 0;
}
