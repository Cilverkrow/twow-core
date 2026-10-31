// twow-repo#581 (QUEST-LOOP-01): QL-1 failure memory per bot x quest with a window and a cooldown, QL-2 minimum cooldown.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "QuestLoopPolicy.h"
#include "QuestSkillLockPolicy.h"   // twow-repo#601 SkipSkillLockedObjectives (no own test target: the rule is a few lines)
#include "StaleObjectivePolicy.h"   // twow-repo#601 cand4 (i) StaleObjectiveFilter (same: no own test target)

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
    using namespace ai::quest_loop;
    std::uint32_t const minute = 60000, window = 30 * minute, cooldown = 30 * minute;

    // SkipSkillLockedObjectives (twow-repo#601, rules agreed with CLI-Berufe): a lock opens when ANY slot fits.
    {
        using ai::quest_skill_lock::Slot;
        using ai::quest_skill_lock::Openable;
        using ai::quest_skill_lock::ProfessionSlotMet;
        Slot const mining[] = { { true, ProfessionSlotMet(false, 0, 1, false) } };
        Require(!Openable(mining, 1), "Copper Vein without Mining: not openable");
        Slot const miningNoPick[] = { { true, ProfessionSlotMet(true, 70, 1, false) } };
        Require(!Openable(miningNoPick, 1), "Mining but no pick: not openable");
        Slot const miner[] = { { true, ProfessionSlotMet(true, 70, 1, true) } };
        Require(Openable(miner, 1), "Mining with the pick: the vein stays a target");
        Require(!ProfessionSlotMet(true, 0, 0, true), "a skill value 0 never fits (max(1, lock value))");
        Require(!ProfessionSlotMet(true, 74, 75, true) && ProfessionSlotMet(true, 75, 75, true), "the lock value counts");
        Slot const herbKneel[] = { { true, false }, { false, true } };
        Require(Openable(herbKneel, 2), "lock 259 (Herbalism + open kneeling): every bot can open it");
        Slot const key[] = { { false, true } };
        Require(Openable(key, 1), "a key item slot counts as openable");
        Require(Openable(nullptr, 0), "a lock without slots: openable");
        Slot const twoProfessions[] = { { true, false }, { true, true } };
        Require(Openable(twoProfessions, 2), "one of two profession slots fits: openable");
    }

    // Ysmara-like loop: an inactive drop about every 3 minutes - the 3rd suppresses for 30 min.
    Record r;
    std::uint32_t const t = 1000000;
    Require(!RecordFailure(r, t, 3, window, cooldown) && !RecordFailure(r, t + 3 * minute, 3, window, cooldown),
        "two failures: still allowed");
    Require(RecordFailure(r, t + 6 * minute, 3, window, cooldown), "third failure in the window: suppressed");
    Require(IsSuppressed(r, t + 6 * minute + 1) && IsSuppressed(r, t + 35 * minute), "skipped for 30 minutes");
    Require(!IsSuppressed(r, t + 37 * minute), "back after the cooldown");
    Require(!RecordFailure(r, t + 10 * minute, 3, window, cooldown), "failures during the cooldown do not extend it");
    Require(IsSuppressed(r, t + 35 * minute), "cooldown unchanged by them");

    // After the cooldown the count starts again.
    Require(!RecordFailure(r, t + 40 * minute, 3, window, cooldown) && !RecordFailure(r, t + 41 * minute, 3, window, cooldown),
        "fresh count after the cooldown");

    // Negative: failures far apart (normal contention) never suppress.
    Record slow;
    bool suppressed = false;
    for (std::uint32_t i = 0; i < 10; ++i)
        suppressed = RecordFailure(slow, t + i * 20 * minute, 3, window, cooldown) || suppressed;
    Require(!suppressed, "one failure every 20 minutes: never suppressed");

    // Off.
    Record off;
    for (std::uint32_t i = 0; i < 10; ++i)
        Require(!RecordFailure(off, t + i, 0, window, cooldown), "MaxFailures 0: off");
    Require(!IsSuppressed(off, t), "off: never suppressed");

    // Clock wrap: a suppression across the 32-bit wrap still holds.
    Record wrap;
    std::uint32_t const nearWrap = 0xFFFFFFFFu - minute;
    Require(RecordFailure(wrap, nearWrap, 1, window, cooldown), "one failure with MaxFailures 1");
    Require(IsSuppressed(wrap, nearWrap + 2 * minute), "suppressed across the clock wrap");

    // QL-2: at least the configured seconds, never shorter than the destination's own cooldown.
    Require(InactiveCooldownMs(60000, 180) == 180000, "60 s -> 180 s");
    Require(InactiveCooldownMs(300000, 180) == 300000, "a longer destination cooldown stays");
    Require(InactiveCooldownMs(60000, 0) == 60000, "0 = as before");

    // GiverTakerDone (twow-repo#601): only a taken (giver) or turned-in (taker) quest ends the trip as a success.
    Require(GiverTakerTripDone(0, true, false), "giver: the quest is now in the log - taken");
    Require(GiverTakerTripDone(0, false, true), "giver: the quest is done (rewarded) - nothing to take");
    Require(!GiverTakerTripDone(0, false, false), "giver: not taken (no room, cannot accept) - the COOLDOWN as before");
    Require(GiverTakerTripDone(1, false, true), "taker: turned in");
    Require(GiverTakerTripDone(1, false, false), "taker: no longer in the log (abandoned) - nothing to turn in");
    Require(!GiverTakerTripDone(1, true, false), "taker: still in the log (an item lost, not complete) - the COOLDOWN as before");

    // InactiveAfterProgress (twow-repo#601): only a grown count is progress; no count never is.
    Require(InactiveAfterProgress(0, 1), "0 -> 1 kill: progress");
    Require(InactiveAfterProgress(4, 7), "4 -> 7 items: progress");
    Require(!InactiveAfterProgress(3, 3), "no new credit: an emptied camp as before");
    Require(!InactiveAfterProgress(5, 2), "a smaller count (items handed in or destroyed): no progress");
    Require(!InactiveAfterProgress(-1, 4), "no count at the start: rule as before");
    Require(!InactiveAfterProgress(2, -1), "quest gone from the log: rule as before");
    Require(ProgressCooldownMs < InactiveCooldownMs(60000, 0), "the progress cooldown is shorter than the destination's");

    // InactiveReselect (twow-repo#601): emptied camps per bot in a fixed ring, (quest, entry) for the cooldown.
    {
        EmptyCamps camps;
        Require(!EmptyCampLeftMs(camps, 872, 3269, t), "nothing marked at the start");
        MarkEmptyCamp(camps, 872, 3269, t, 180000);
        Require(EmptyCampLeftMs(camps, 872, 3269, t + 1000) == 179000, "marked: time left");
        Require(!EmptyCampLeftMs(camps, 872, 3270, t + 1000) && !EmptyCampLeftMs(camps, 873, 3269, t + 1000),
            "another entry of the quest, or another quest at the entry: not marked");
        Require(!EmptyCampLeftMs(camps, 872, 3269, t + 180000), "the mark ends with the cooldown");
        MarkEmptyCamp(camps, 872, -3767, t, 60000);
        Require(EmptyCampLeftMs(camps, 872, -3767, t) == 60000, "a game object entry (negative) is a key too");

        // The same camp again: its own slot, renewed - no second slot.
        EmptyCamps again;
        MarkEmptyCamp(again, 1, 10, t, 1000);
        MarkEmptyCamp(again, 1, 10, t + 500, 180000);
        Require(EmptyCampLeftMs(again, 1, 10, t + 1500) == 179000, "renewed in its own slot");
        int used = 0;
        for (EmptyCamp const& camp : again.slots)
            used += camp.untilMs ? 1 : 0;
        Require(used == 1, "one slot per camp");

        // Full ring: a run-out slot is reused first, else the one that runs out first gives way.
        EmptyCamps ring;
        for (std::uint32_t i = 0; i < EmptyCampSlots; ++i)
            MarkEmptyCamp(ring, 100 + i, 1, t, 100000 + i * 1000);
        MarkEmptyCamp(ring, 200, 1, t + 1000, 50000);
        Require(EmptyCampLeftMs(ring, 200, 1, t + 1000) == 50000, "5th camp marked");
        Require(!EmptyCampLeftMs(ring, 100, 1, t + 1000), "the one running out first gave way");
        for (std::uint32_t i = 1; i < EmptyCampSlots; ++i)
            Require(EmptyCampLeftMs(ring, 100 + i, 1, t + 1000) > 0, "the others stay");
        // At t + 101500 camps 200 (slot 0) and 101 have run out, 102 and 103 not: a run-out slot takes the new camp, no live one.
        MarkEmptyCamp(ring, 300, 1, t + 101500, 1000);
        Require(EmptyCampLeftMs(ring, 300, 1, t + 101500) == 1000 && EmptyCampLeftMs(ring, 102, 1, t + 101500) == 500 &&
            EmptyCampLeftMs(ring, 103, 1, t + 101500) == 1500, "a run-out slot is reused first");

        // Full ring across the clock wrap: the end before the wrap runs out first and gives way (not the small number after it).
        EmptyCamps across;
        MarkEmptyCamp(across, 1, 1, nearWrap, 30000);            // ends before the wrap
        MarkEmptyCamp(across, 2, 1, nearWrap, 2 * minute);       // ends after the wrap (small number)
        MarkEmptyCamp(across, 3, 1, nearWrap, 3 * minute);
        MarkEmptyCamp(across, 4, 1, nearWrap, 4 * minute);
        MarkEmptyCamp(across, 5, 1, nearWrap + 1000, minute);
        Require(!EmptyCampLeftMs(across, 1, 1, nearWrap + 1000) && EmptyCampLeftMs(across, 2, 1, nearWrap + 1000) > 0,
            "eviction across the wrap takes the end that comes first");
        // Equal ends: the lowest slot gives way.
        EmptyCamps tie;
        for (std::uint32_t i = 0; i < EmptyCampSlots; ++i)
            MarkEmptyCamp(tie, 10 + i, 1, t, minute);
        MarkEmptyCamp(tie, 20, 1, t, minute);
        Require(tie.slots[0].quest == 20 && EmptyCampLeftMs(tie, 11, 1, t) > 0, "equal ends: slot 0 gives way");

        // Clock wrap: a mark across the 32-bit wrap still holds; an end on 0 is no free slot.
        EmptyCamps wrapped;
        MarkEmptyCamp(wrapped, 5, 5, nearWrap, 2 * minute);
        Require(EmptyCampLeftMs(wrapped, 5, 5, nearWrap + minute) == minute, "marked across the clock wrap");
        EmptyCamps zero;
        MarkEmptyCamp(zero, 6, 6, 0xFFFFFFFFu - 999u, 1000);
        Require(zero.slots[0].untilMs == 1, "an end on 0 is kept as 1");
    }

    // twow-repo#601 EmptyCampHold: the mark lasts the respawn, at least the base mark, at most 900 s; the fallback COOLDOWN stays
    // the base mark.
    {
        Require(EmptyCampHoldMs(180000, 300) == 300000, "5 min respawn: held 5 min");
        Require(EmptyCampHoldMs(180000, 900) == 900000 && EmptyCampHoldMs(180000, 901) == 900000, "cap 900 s");
        Require(EmptyCampHoldMs(180000, 604800) == 900000, "a week's respawn: the cap (no overflow)");
        Require(EmptyCampHoldMs(180000, 0xFFFFFFFFu) == 900000, "largest respawn: the cap (no overflow)");
        Require(EmptyCampHoldMs(180000, 120) == 180000 && EmptyCampHoldMs(180000, 180) == 180000, "respawn within the base: the base");
        Require(EmptyCampHoldMs(180000, 0) == 180000, "no spawn data: the base");
        Require(EmptyCampHoldMs(1200000, 300) == 1200000 && EmptyCampHoldMs(1200000, 0xFFFFFFFFu) == 1200000, "a base above the cap stays");
        Require(FallbackCooldownMs(895000, 180000) == 180000, "fallback right after a held mark: the base cooldown");
        Require(FallbackCooldownMs(120000, 180000) == 120000 && FallbackCooldownMs(0, 180000) == 0, "fallback within the base: the time left");

        // cand3-C bot 53: 41746 / 62633 marked at the drop, held, the re-pick 4 min later is skipped.
        EmptyCamps camps;
        std::uint32_t const t = 1000000;
        MarkEmptyCamp(camps, 41746, 62633, t, 180000);
        MarkEmptyCamp(camps, 41746, 62633, t, EmptyCampHoldMs(180000, 300));
        Require(EmptyCampLeftMs(camps, 41746, 62633, t + 241000) == 59000, "re-pick at 4:01 skipped, 59 s left");
        Require(camps.slots[1].untilMs == 0, "the hold takes the camp's own slot again");
        Require(EmptyCampLeftMs(camps, 41746, 62633, t + 300000) == 0, "free again at the respawn");
    }

    // twow-repo#601 turn_in hold (QuestLoop.RelationWorkNoGrind / RelationArriveRpgTarget / TurnInWorkDeathNotRoute, CLI-Deep 28.10.).
    {
        Require(RelationWorkGrindSkipped(true, false), "working at an open taker: an elemental without quest need is skipped");
        Require(!RelationWorkGrindSkipped(true, true), "a quest objective stays a target");
        Require(!RelationWorkGrindSkipped(false, false), "off, travelling or the hand-in done: grinding as before");
        Require(std::string(RelationArriveRpgAction(1)) == "rpg end quest", "taker: the hand-in");
        Require(std::string(RelationArriveRpgAction(0)) == "rpg start quest", "giver: the accept");
        Require(WorkDeathIsTurnInFailure(true, true, true), "a death in WORK at the taker: a turn-in failure, no route death");
        Require(!WorkDeathIsTurnInFailure(true, false, true), "a death on the way (TRAVEL): the route rule as before");
        Require(!WorkDeathIsTurnInFailure(true, true, false), "parking off: nothing would bound the deaths - the route rule stays");
        Require(!WorkDeathIsTurnInFailure(false, true, true), "switch off or another cohort: as before");
    }

    // twow-repo#601 cand4 (i) StaleObjectiveFilter (CLI-Deep 29.10.): realm-wide failures without credit mark an objective.
    {
        namespace so = ai::stale_objective;
        so::Params const p;   // 4 failures, 3 bots, 1 h window, 1 h mark
        std::uint32_t const t = 1000000, h = 3600000;
        std::uint32_t f = 0, b = 0;
        so::Store s;
        so::Key const elem{ 41747, 62633 };
        Require(!so::RecordFailure(s, elem, 11, t, p, f, b) && !so::RecordFailure(s, elem, 13, t + 1000, p, f, b) &&
            !so::RecordFailure(s, elem, 15, t + 2000, p, f, b), "3 failures of 3 bots: not yet (4 needed)");
        Require(so::RecordFailure(s, elem, 11, t + 3000, p, f, b) && f == 4 && b == 3, "the 4th failure (bot 11 again, 3 bots): marked");
        Require(so::IsStale(s, elem, t + 4000) && !so::IsStale(s, so::Key{ 41747, 62634 }, t + 4000), "only that objective is marked");
        Require(!so::RecordFailure(s, elem, 17, t + 5000, p, f, b), "an existing mark is not renewed by more failures");
        Require(so::IsStale(s, elem, t + 3000 + h - 1) && !so::IsStale(s, elem, t + 3000 + h), "the mark lasts markMs");

        so::Store two;
        for (std::uint32_t i = 0; i < 6; ++i)
            Require(!so::RecordFailure(two, elem, 20 + (i % 2), t + i, p, f, b), "6 failures of only 2 bots: never marked");

        so::Store win;
        so::RecordFailure(win, elem, 1, t, p, f, b);
        so::RecordFailure(win, elem, 2, t + 1, p, f, b);
        so::RecordFailure(win, elem, 3, t + 2, p, f, b);
        Require(!so::RecordFailure(win, elem, 4, t + h + 10, p, f, b) && f == 1, "failures older than the window do not count");

        so::Store cr;
        for (std::uint32_t i = 0; i < 4; ++i)
            so::RecordFailure(cr, elem, 30 + i, t + i, p, f, b);
        so::RecordFailure(cr, so::Key{ 999, 5 }, 30, t, p, f, b);
        Require(so::IsStale(cr, elem, t + 10), "marked before the credit");
        Require(so::Credit(cr, 41747, t + 20) == 1 && !so::IsStale(cr, elem, t + 30), "a credit of the quest ends the mark");
        Require(cr.entries.count(so::Key{ 999, 5 }) == 1 && cr.entries.count(elem) == 0, "the credit drops only its own quest's keys");
        Require(!so::RecordFailure(cr, elem, 30, t + 40, p, f, b) && f == 1, "after the credit the count starts again");

        so::Store ring;
        for (std::uint32_t i = 0; i < 12; ++i)
            so::RecordFailure(ring, so::Key{ 7, 7 }, 40 + i, t + i, so::Params{ 9, 9, h, h }, f, b);
        Require(f == so::KeptFailures && b == so::KeptFailures, "at most KeptFailures failures are kept per objective");

        so::Store big;
        for (std::uint32_t i = 0; i < so::MaxKeys + 50; ++i)
            so::RecordFailure(big, so::Key{ 100000 + i, 1 }, 1, t + i, p, f, b);
        Require(big.entries.size() <= so::MaxKeys, "bounded: at most MaxKeys objectives");

        so::Store wrap;
        std::uint32_t const w = 0xFFFFFF00u;
        for (std::uint32_t i = 0; i < 4; ++i)
            so::RecordFailure(wrap, elem, 50 + i, w + i * 100, p, f, b);
        Require(so::IsStale(wrap, elem, w + 1000) && so::IsStale(wrap, elem, 5000), "the world timer wrap keeps the mark");
    }

    std::cout << "quest_loop_policy_tests passed\n";
    return 0;
}
