// A3-HARM-UPSTREAM H6 (T40) (StaleTargetPolicy.h, AiPlayerbot.Rotation.ReleaseStaleTarget, default 0): a bot out of combat that keeps a
// valid current target and stands still drops only that current target. Modes and shadow, Reset, holds, the still / stopped clocks
// (gap, target, map, move, end of a hold), due, release, the once-per-episode counts and the re_same / moved_on follow-up.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "StaleTargetPolicy.h"

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
    using namespace ai::stale_target;

    // 1) Modes: 0 off, 1 every bot, 2 cohort B (odd low guid); the loader maps > 2 to 0, so 3 never reaches this rule.
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");
    Require(CohortIndex(10) == 0 && CohortIndex(11) == 1, "cohort index: A even, B odd");

    // 2) Shadow: only the control cohort of a mode that is on, and only with the shadow key 1.
    Require(ShadowApplies(2, 1, 10) && !ShadowApplies(2, 1, 11), "mode 2 + shadow: cohort A counts, B acts");
    Require(!ShadowApplies(1, 1, 10) && !ShadowApplies(1, 1, 11), "mode 1: nobody is control");
    Require(!ShadowApplies(0, 1, 10), "mode 0: no shadow even with the key");
    Require(!ShadowApplies(2, 0, 10), "shadow key 0: no shadow");

    // 3) Reset: any input makes the bot ineligible.
    Require(!IsReset(false, true, false, false, false, false, false), "out of combat, alive, free: eligible");
    Require(IsReset(true, true, false, false, false, false, false), "in combat");
    Require(IsReset(false, false, false, false, false, false, false), "dead");
    Require(IsReset(false, true, true, false, false, false, false), "real player master");
    Require(IsReset(false, true, false, true, false, false, false), "taxi");
    Require(IsReset(false, true, false, false, true, false, false), "transport");
    Require(IsReset(false, true, false, false, false, true, false), "duel");
    Require(IsReset(false, true, false, false, false, false, true), "battleground");

    // 4) Holds in order Pull, Wait, Loot, Cast, Rest.
    Require(HoldOf(false, false, false, false, false) == Hold::None, "no hold");
    Require(HoldOf(true, true, true, true, true) == Hold::Pull, "pull first");
    Require(HoldOf(false, true, true, true, true) == Hold::Wait, "wait second");
    Require(HoldOf(false, false, true, true, true) == Hold::Loot, "loot third");
    Require(HoldOf(false, false, false, true, true) == Hold::Cast, "cast fourth");
    Require(HoldOf(false, false, false, false, true) == Hold::Rest, "rest last");
    // Rest input (v1.2 F1): only a regen aura with AURA_INTERRUPT_FLAG_NOT_SEATED (food, drink) counts; Blessing of Wisdom and mp5 /
    // hp5 gear (no interrupt flag, or others only) are not Rest, so the bot without a sit stays releasable.
    Require(NotSeatedInterruptFlag == 0x00040000u, "NOT_SEATED as in the core SpellDefines.h");
    Require(IsEatOrDrinkAura(0x00040000u) && IsEatOrDrinkAura(0x00040000u | 0x2u), "food / drink aura: Rest");
    Require(!IsEatOrDrinkAura(0) && !IsEatOrDrinkAura(0x00000002u | 0x00001000u), "non-seated regen aura (Wisdom, mp5 gear): not Rest");
    Require(HoldOf(false, false, false, false, false || IsEatOrDrinkAura(0)) == Hold::None, "standing with a Wisdom / mp5 aura: no hold");

    // 5) Observe: the first observation opens an episode and anchors both clocks.
    {
        Clock c;
        Require(DueOf(c, 1000, 60, 5) == Due::None, "no episode: never due");
        Require(Observe(c, 1000, 77, 1, 0, 0, 0, true) == Observed::NewEpisode, "first observation: new episode");
        Require(c.running && c.stillRunning && c.stillSinceMs == 1000 && c.stoppedRunning && c.stoppedSinceMs == 1000, "both clocks anchored");
        Require(Observe(c, 2000, 77, 1, 4.9f, 0, 0, true) == Observed::Continued, "a move below MoveYards keeps the clock");
        Require(c.stillSinceMs == 1000 && c.stoppedSinceMs == 1000, "the clocks keep their start");
        Require(DueOf(c, 5999, 60, 5) == Due::None, "4999 ms stopped: not due");
        Observe(c, 4000, 77, 1, 0, 0, 0, true);
        Observe(c, 6000, 77, 1, 0, 0, 0, true);
        Require(DueOf(c, 6000, 60, 5) == Due::Stopped, "5000 ms stopped: due");
        Require(DueOf(c, 6000, 60, 0) == Due::None, "StoppedSeconds 0: that clause off");
    }
    {
        Clock c;
        Observe(c, 1000, 77, 1, 0, 0, 0, true);
        Require(Observe(c, 2000, 77, 1, 5.0f, 0, 0, true) == Observed::Restarted, "a move of MoveYards restarts");
        Require(c.stillSinceMs == 2000 && c.stoppedSinceMs == 2000 && c.x == 5.0f, "both clocks restart, anchor = here");
        Require(Observe(c, 3000, 77, 1, 5.0f, 0, 3.0f, true) == Observed::Continued, "3-D distance: 3 yd up keeps the clock");
        Require(Observe(c, 4000, 77, 2, 5.0f, 0, 3.0f, true) == Observed::Restarted && c.mapId == 2, "a map change restarts");
        Require(Observe(c, 9001, 77, 2, 5.0f, 0, 3.0f, true) == Observed::Restarted && c.stillSinceMs == 9001, "a gap > GapMs restarts");
        Require(Observe(c, 14001, 77, 2, 5.0f, 0, 3.0f, true) == Observed::Continued, "a gap of exactly GapMs keeps the clock");
        c.heldMask = 0xFF;
        c.shadowCounted = true;
        Require(Observe(c, 15000, 78, 2, 5.0f, 0, 3.0f, true) == Observed::NewEpisode, "a target change: new episode");
        Require(c.heldMask == 0 && !c.shadowCounted && c.stillSinceMs == 15000, "a new episode clears the once-per-episode marks");
    }
    {
        // The stopped clock runs only while stopped; the still clock does not care.
        Clock c;
        Observe(c, 1000, 77, 1, 0, 0, 0, false);
        Require(c.stillRunning && !c.stoppedRunning, "moving: still clock only");
        Observe(c, 2000, 77, 1, 0, 0, 0, true);
        Require(c.stoppedRunning && c.stoppedSinceMs == 2000, "first stopped pass starts the stopped clock");
        Observe(c, 3000, 77, 1, 0, 0, 0, true);
        Require(c.stoppedSinceMs == 2000, "stopped keeps its start");
        Observe(c, 4000, 77, 1, 0, 0, 0, false);
        Require(!c.stoppedRunning && c.stillSinceMs == 1000, "a moving pass stops the stopped clock, not the still clock");
        for (uint32_t t = 5000; t <= 61000; t += 1000)
            Observe(c, t, 77, 1, 0, 0, 0, false);
        Require(DueOf(c, 60999, 60, 5) == Due::None, "59999 ms still: not due");
        Require(DueOf(c, 61000, 60, 5) == Due::Still, "60000 ms still: due");
        Require(DueOf(c, 61000, 0, 5) == Due::None, "StillSeconds 0: that clause off (and not stopped)");
        Observe(c, 62000, 77, 1, 0, 0, 0, true);
        Observe(c, 67000, 77, 1, 0, 0, 0, true);
        Require(DueOf(c, 67000, 60, 5) == Due::Still, "Still wins when both are due");
        Require(DueOf(c, 67000, 0, 5) == Due::Stopped, "Stopped alone");
    }
    {
        // An episode anchored at nowMs 0 (v1.2 F3): the running flags, not a "sinceMs != 0" sentinel, make it due.
        Clock c;
        Require(Observe(c, 0, 77, 1, 0, 0, 0, true) == Observed::NewEpisode, "nowMs 0: new episode");
        Require(c.running && c.stillRunning && c.stillSinceMs == 0 && c.stoppedRunning && c.stoppedSinceMs == 0, "nowMs 0: both clocks run");
        for (uint32_t t = 1000; t <= 5000; t += 1000)
            Observe(c, t, 77, 1, 0, 0, 0, true);
        Require(DueOf(c, 4999, 60, 5) == Due::None && DueOf(c, 5000, 60, 5) == Due::Stopped, "nowMs 0: Stopped due at 5000");
        for (uint32_t t = 6000; t <= 60000; t += 1000)
            Observe(c, t, 77, 1, 0, 0, 0, false);
        Require(DueOf(c, 59999, 60, 5) == Due::None && DueOf(c, 60000, 60, 5) == Due::Still, "nowMs 0: Still due at 60000");
    }
    {
        // Wrap: WorldTimer ms across the uint32 wrap.
        Clock c;
        uint32_t const start = 0xFFFFFFFFu - 1000u;
        Observe(c, start, 77, 1, 0, 0, 0, true);
        for (uint32_t i = 1; i <= 5; ++i)
            Observe(c, start + i * 1000u, 77, 1, 0, 0, 0, true);
        Require(Observe(c, start + 6000u, 77, 1, 0, 0, 0, true) == Observed::Continued, "no false gap across the wrap");
        Require(DueOf(c, start + 4999u, 60, 5) == Due::None && DueOf(c, start + 5000u, 60, 5) == Due::Stopped, "due across the wrap");
    }

    // 6) Holds: once per episode and kind; the end of a hold restarts both clocks; a restart ends a hold.
    {
        Clock c;
        Observe(c, 1000, 77, 1, 0, 0, 0, true);
        Require(NoteHold(c, Hold::Loot) && c.holding, "first loot hold: counted");
        Require(!NoteHold(c, Hold::Loot), "the same hold again: not counted");
        Require(NoteHold(c, Hold::Cast), "another hold kind: counted");
        Require(!NoteHold(c, Hold::None), "None is never counted");
        Require(EndHold(c, 9000, 1, 1, 0, true) && !c.holding, "end of a hold");
        Require(c.stillSinceMs == 9000 && c.stoppedSinceMs == 9000 && c.x == 1.0f, "the end of a hold restarts both clocks");
        Require(!EndHold(c, 9500, 1, 1, 0, true), "no hold: nothing to end");
        Require(!NoteHold(c, Hold::Loot), "still the same episode: loot not counted again");
        c.holding = true;
        Observe(c, 20000, 77, 1, 1, 1, 0, true);   // gap > GapMs
        Require(!c.holding, "a clock restart ends the hold");
        Require(NoteShadow(c) && !NoteShadow(c), "shadow: once per episode");
        Clear(c);
        Require(!c.running && c.heldMask == 0 && !c.shadowCounted, "Clear (Reset, old rule) clears everything");
    }

    // 7) elig_bot_min: once per bot and minute, kept across episodes.
    {
        MinuteMark m;
        Require(NoteEligibleMinute(m, 60000) && !NoteEligibleMinute(m, 119999), "once in a minute");
        Require(NoteEligibleMinute(m, 120000), "next minute: again");
    }

    // 8) Release: only an acting bot, a due clause and no hold.
    Require(Release(true, Due::Still, Hold::None) && Release(true, Due::Stopped, Hold::None), "release");
    Require(!Release(false, Due::Still, Hold::None), "not applies: no release");
    Require(!Release(true, Due::None, Hold::None), "not due: no release");
    Require(!Release(true, Due::Still, Hold::Pull) && !Release(true, Due::Still, Hold::Rest), "a hold: no release");

    // 9) Follow-up: re_same / moved_on within 30 s, else it lapses without a count.
    {
        FollowUp f;
        Require(FollowOf(f, 0, 0, 1, 0, 0, 0) == Follow::Lapsed, "nothing open");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 2000, 0, 1, 1, 0, 0) == Follow::Open && f.open, "empty target, no move: stays open");
        Require(FollowOf(f, 3000, 77, 1, 0, 0, 0) == Follow::ReSame && !f.open, "the same guid again: re_same, closed");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 3000, 78, 1, 0, 0, 0) == Follow::MovedOn, "a different target: moved_on");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 3000, 0, 1, 5.0f, 0, 0) == Follow::MovedOn, "moved MoveYards: moved_on");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 3000, 0, 2, 0, 0, 0) == Follow::MovedOn, "another map: moved_on");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 31000, 77, 1, 0, 0, 0) == Follow::ReSame, "exactly 30 s: still inside the window");
        OpenFollowUp(f, 77, 1000, 1, 0, 0, 0);
        Require(FollowOf(f, 31001, 77, 1, 0, 0, 0) == Follow::Lapsed && !f.open, "after 30 s: lapsed, no count");
    }

    // 10) Named constants pinned (design note v1.1 D3).
    Require(DefaultStillSeconds == 60 && DefaultStoppedSeconds == 5 && GapMs == 5000 && MoveYards == 5.0f && FollowWindowMs == 30000,
        "the named constants");

    std::cout << "stale_target_policy_tests passed\n";
    return 0;
}
