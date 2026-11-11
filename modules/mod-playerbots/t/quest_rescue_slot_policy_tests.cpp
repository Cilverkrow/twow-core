#include "QuestRescueSlotPolicy.h"
#include "QuestBandPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#703 item 1 (AiPlayerbot.QuestRescue.NoTargetFreeSlot): the pure rules of QuestRescueSlotPolicy.h - the key clamp,
// targets before the limiter only with the key, no slot for an empty list, the early list reused only for the peeked rung, the
// minute counters; plus the peek/take equality of the band rescue packing it relies on (QuestBandPolicy.h).
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
    using namespace ai::rescue_slot;

    // 1. The key: 0 / 1 pass, anything else is off (and flagged invalid for the log).
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1, "0 and 1 pass");
        Require(ClampMode(2) == 0 && ClampMode(-1) == 0 && ClampMode(4294967295LL) == 0, "other values act like 0");
        Require(ModeValid(0) && ModeValid(1) && !ModeValid(2) && !ModeValid(-1), "valid values");
    }

    // 2. The order: targets first only with the key; key 0 = the legacy order (limiter first).
    {
        Require(!TargetsFirst(0) && TargetsFirst(1), "targets first only with 1");
        Require(!TargetsFirst(2), "an unclamped value never turns it on");
    }

    // 3. The slot: only a rescue with somewhere to go asks the limiter.
    {
        Require(!NeedsSlot(true), "an empty list takes no slot");
        Require(NeedsSlot(false), "a list with targets asks the limiter");
    }

    // 4. Reuse of the early list: only with the key and only when the taken rung is the peeked one.
    {
        Require(ReuseEarlyTargets(true, 0, 0), "plain rescue, peeked 0, taken 0: reuse");
        Require(ReuseEarlyTargets(true, 3, 3), "band rung 2 (+1 = 3) peeked and taken: reuse");
        Require(!ReuseEarlyTargets(true, 3, 0), "a band rescue appeared after the peek: compute again (rung_mismatch)");
        Require(!ReuseEarlyTargets(true, 0, 3), "the band rescue was gone at the take: compute again (rung_mismatch)");
        Require(!ReuseEarlyTargets(false, 0, 0), "key 0: never reuse (the legacy computation)");
    }

    // 5. Peek and take read the same value at the same `now` (UnpackRescue is the one rule for both).
    {
        namespace qb = ai::quest_band;
        std::uint32_t const at = 1'800'000'000;
        std::uint64_t const packed = qb::PackRescue(4, at);
        bool dropped = false;
        std::uint32_t const peek = qb::UnpackRescue(packed, at + 10, dropped);
        Require(peek == 5 && !dropped, "fresh: rung + 1");
        std::uint32_t const take = qb::UnpackRescue(packed, at + 10, dropped);
        Require(take == peek, "the take after the limiter gives the peeked rung");
        Require(qb::UnpackRescue(packed, at + qb::RescueHandOverSeconds + 1, dropped) == 0 && dropped, "expired: 0 (a plain rescue)");
        Require(qb::UnpackRescue(0, at, dropped) == 0 && !dropped, "none: 0");
    }

    // 6. The minute counters: one line per minute.
    {
        Counters counters;
        counters.noTargetNoSlot.fetch_add(2);
        counters.rungMismatch.fetch_add(1);
        Require(counters.LogDue(1'800'000'000) && !counters.LogDue(1'800'000'059) && counters.LogDue(1'800'000'060), "one line per minute");
        Require(counters.noTargetNoSlot.exchange(0) == 2 && counters.noTargetNoSlot.load() == 0, "notarget_noslot taken");
        Require(counters.rungMismatch.exchange(0) == 1, "rung_mismatch taken");
    }

    std::cout << "quest_rescue_slot_policy_tests: ok\n";
    return 0;
}
