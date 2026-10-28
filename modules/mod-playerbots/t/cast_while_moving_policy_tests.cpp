// twow-repo#333 P1-w (CastWhileMovingPolicy.h, OB-00 27.10.2026): a loot cast of a moving bot casts after StopMoving instead of
// cancelling, and a started loot cast holds the AI for its cast time. twow-repo#602 (Rotation.CastStopFirst, OB-00 27.10.2026,
// default 1): every other cast of a moving bot casts after the stop too; 0 keeps the old cancel.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "CastWhileMovingPolicy.h"

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
}

int main()
{
    using namespace ai::cast_moving;

    // 1) The effect numbers of SpellDefines.h.
    Require(kSpellEffectOpenLock == 33 && kSpellEffectSkinning == 95, "OPEN_LOCK 33, SKINNING 95");

    // 2) A loot cast: OPEN_LOCK or SKINNING, only with the loot target found and without an item target.
    {
        Require(IsLootCast(kSpellEffectOpenLock, true, false), "opening / mining / herbs with a target");
        Require(IsLootCast(kSpellEffectSkinning, true, false), "skinning with a target");
        Require(!IsLootCast(kSpellEffectOpenLock, false, false) && !IsLootCast(kSpellEffectSkinning, false, false),
            "no loot target: not a loot cast");
        Require(!IsLootCast(kSpellEffectOpenLock, true, true),
            "Pick Lock on a lockbox (item target) with a stale loot target: not a loot cast");
        Require(!IsLootCast(2, true, false) && !IsLootCast(6, true, false) && !IsLootCast(0, true, false),
            "school damage, apply aura, no effect: not a loot cast");
    }

    // 3) After the stop: a loot cast casts (run 4: 12 of 12 skinning tries cancelled); with an active real player master as
    //    before; every other cast of a bot on its own cancels only without Rotation.CastStopFirst (switch value 0).
    {
        Require(!CancelAfterStop(false, true, false), "bot on its own, loot cast: cast");
        Require(!CancelAfterStop(true, true, false), "player master, loot cast: cast");
        Require(!CancelAfterStop(true, false, false), "player master, other cast: cast (unchanged)");
        Require(CancelAfterStop(false, false, false), "bot on its own, other cast, switch off: cancel (rollback)");
        Require(!CancelAfterStop(false, false, true), "bot on its own, other cast, switch on: cast (hunt2: Mind Blast 26k cancels)");
        Require(!CancelAfterStop(false, true, true) && !CancelAfterStop(true, false, true) && !CancelAfterStop(true, true, true),
            "switch on: nothing cancels");
    }

    // 4) Rotation.CastStopFirst modes: 0 off, 1 every bot, 2 cohort B (odd low guid); unknown values never reach the policy
    //    (PlayerbotAIConfig maps them to 1).
    {
        Require(!CastStopFirstApplies(0, 10) && !CastStopFirstApplies(0, 11), "mode 0: no bot");
        Require(CastStopFirstApplies(1, 10) && CastStopFirstApplies(1, 11), "mode 1: every bot");
        Require(!CastStopFirstApplies(2, 10) && CastStopFirstApplies(2, 11), "mode 2: cohort B only");
    }

    // 5) The [CastStopFirst] cast_after_stop counter: only the casts the old rule cancelled and the switch lets through.
    {
        Require(CastByStopFirst(false, false, true), "bot on its own, other cast, switch on: counted");
        Require(!CastByStopFirst(false, false, false), "switch off: not counted (it cancels)");
        Require(!CastByStopFirst(false, true, true), "loot cast: casts anyway, not counted");
        Require(!CastByStopFirst(true, false, true), "player master: casts anyway, not counted");
    }

    // 6) The open loot action waits for its cast: the cast time when longer, else its own duration.
    {
        Require(LootActionDuration(100, 0) == 100, "no cast (corpse loot without cast, cleared loot): react delay");
        Require(LootActionDuration(750, 0) == 750, "corpse loot: the loot delay stays");
        Require(LootActionDuration(100, 5100) == 5100, "mining (5 s cast + react delay): the cast time");
        Require(LootActionDuration(100, 100) == 100, "equal: unchanged");
        Require(LootActionDuration(2000, 1600) == 2000, "shorter cast than the action: the action's duration");
    }

    std::cout << "cast_while_moving_policy_tests passed\n";
    return 0;
}
