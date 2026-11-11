// twow-repo#703 item 4 (UnreachableGiveUpPolicy.h, AiPlayerbot.Rotation.UnreachableGiveUp, default 0 until its A/B; design v2
// 85574eca, OB-10 OK 11.11.2026): the per-guid count of [Unreachable] results, the 1 h mark guard, the gate and the eviction check.
#include "UnreachableGiveUpPolicy.h"
#include "CohortPolicy.h"

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
    using namespace ai::unreachable_giveup;
    std::uint64_t const a = 0xF130000000000A01ull, b = 0xF130000000000B02ull, c = 0xF130000000000C03ull,
        d = 0xF130000000000D04ull, e = 0xF130000000000E05ull;

    // The constants of design v2.
    Require(K == 3 && WindowSeconds == 1800 && LongIgnoreSeconds == 1800 && MarkSeconds == 1800 && MarkGuardSeconds == 3600 &&
        RingSlots == 4 && MarkGuardSlots == 8, "design constants");

    // Mode 0 = no counting for any bot; the cohort and the gate must hold too.
    Require(!Active(0, ai::cohort::Applies(0, 1), true) && !Active(0, true, true), "mode 0: no count");
    Require(Active(1, ai::cohort::Applies(1, 2), true), "mode 1: every bot");
    Require(Active(2, ai::cohort::Applies(2, 1), true) && !Active(2, ai::cohort::Applies(2, 2), true), "mode 2: cohort B only");
    Require(Active(3, ai::cohort::Applies(3, 2), true) && !Active(3, ai::cohort::Applies(3, 1), true), "mode 3: cohort A only");
    Require(!Active(1, true, false), "the gate must hold");

    // The gate: a roster bot without a real player master, with no group or not its leader.
    Require(Counts(true, false, false, false), "roster bot, no group: counts");
    Require(Counts(true, false, true, false), "roster bot, group member: counts");
    Require(!Counts(true, false, true, true), "group leader: never (a leading tank keeps its pull)");
    Require(!Counts(true, true, false, false), "real player master: never");
    Require(!Counts(false, false, false, false), "not a roster bot: never");

    // A, B, A, B, A gives up on A (two mobs alternating).
    {
        Ring ring;
        Require(ring.Note(a, 100) == 1, "A 1");
        Require(ring.Note(b, 420) == 1, "B 1");
        Require(ring.Note(a, 740) == 2, "A 2");
        Require(ring.Note(b, 1060) == 2, "B 2");
        Require(ring.Note(a, 1380) == 3, "A 3: give up");
        Require(ring.CountOf(a) == 0, "A's slot cleared at K");
        Require(ring.CountOf(b) == 2, "B keeps its count");
        Require(ring.Note(a, 1700) == 1, "A counts from 1 again after a give-up");
    }

    // The window: a result 1800 s or more after the first starts the count again.
    {
        Ring ring;
        Require(ring.Note(a, 1000) == 1, "first");
        Require(ring.Note(a, 1500) == 2, "second within the window");
        Require(ring.Note(a, 2800) == 1, "third after the window: a new count");
        Require(ring.Note(a, 3000) == 2, "and on from there");
        Require(ring.Note(a, 3100) == 3, "K within the new window");
    }
    {
        Ring ring;
        Require(ring.Note(a, 1000) == 1 && ring.Note(a, 2799) == 2 && ring.Note(a, 2799) == 3, "1799 s after the first still counts");
    }

    // The slot eviction: four guids fill the ring, a fifth takes the oldest slot (the earliest first), the others keep theirs.
    {
        Ring ring;
        ring.Note(a, 100);
        ring.Note(b, 200);
        ring.Note(c, 300);
        ring.Note(d, 400);
        ring.Note(b, 500);
        Require(ring.Note(e, 600) == 1, "fifth guid counts");
        Require(ring.CountOf(a) == 0, "the oldest (A) left");
        Require(ring.CountOf(b) == 2 && ring.CountOf(c) == 1 && ring.CountOf(d) == 1 && ring.CountOf(e) == 1, "the others stay");
        Require(ring.Note(a, 700) == 1, "A starts again from 1");
        Require(ring.CountOf(b) == 0, "and took the next oldest (B, first 200)");
    }

    // A free slot (after a give-up) is taken before an occupied one.
    {
        Ring ring;
        ring.Note(a, 100);
        ring.Note(b, 200);
        ring.Note(c, 300);
        ring.Note(d, 400);
        ring.Note(d, 500);
        Require(ring.Note(d, 600) == 3, "D gives up, its slot is free");
        Require(ring.Note(e, 700) == 1, "E takes the free slot");
        Require(ring.CountOf(a) == 1 && ring.CountOf(b) == 1 && ring.CountOf(c) == 1, "nobody evicted");
    }

    // The 1 h mark guard per (quest, entry).
    {
        MarkGuard guard;
        Require(guard.Allows(33, 1501, 1000), "first mark allowed");
        guard.Note(33, 1501, 1000);
        Require(!guard.Allows(33, 1501, 1000 + 3599), "same camp within the hour: skip");
        Require(guard.Allows(33, 1501, 1000 + 3600), "after the hour: allowed");
        Require(guard.Allows(33, 1502, 1100) && guard.Allows(34, 1501, 1100), "another entry or quest: allowed");
        guard.Note(33, 1501, 5000);
        Require(!guard.Allows(33, 1501, 5100), "a new mark restarts the hour");
        // Eight camps fill the guard; a ninth takes a run-out or the oldest entry.
        MarkGuard full;
        for (std::uint32_t i = 0; i < MarkGuardSlots; ++i)
            full.Note(100 + i, 7, 10000 + i);
        full.Note(200, 7, 10100);
        Require(full.Allows(100, 7, 10101), "the oldest (quest 100) left");
        Require(!full.Allows(101, 7, 10101) && !full.Allows(200, 7, 10101), "the others stay");
    }

    // The eviction check matches quest_loop::MarkEmptyCamp's slot choice.
    {
        ai::quest_loop::EmptyCamps camps;
        Require(!MarkEvictsLive(camps, 33, 1501, 1000), "empty ring: no eviction");
        for (std::uint32_t i = 0; i < ai::quest_loop::EmptyCampSlots; ++i)
            ai::quest_loop::MarkEmptyCamp(camps, 10 + i, 5, 1000, 600000);
        Require(MarkEvictsLive(camps, 33, 1501, 2000), "full live ring: the mark evicts");
        Require(!MarkEvictsLive(camps, 10, 5, 2000), "its own slot: no eviction");
        Require(!MarkEvictsLive(camps, 33, 1501, 1000 + 600000), "a run-out mark: no eviction");
        ai::quest_loop::MarkEmptyCamp(camps, 33, 1501, 2000, MarkSeconds * 1000u);
        Require(ai::quest_loop::EmptyCampLeftMs(camps, 33, 1501, 2000) == MarkSeconds * 1000u, "the mark lasts MarkSeconds");
    }

    std::cout << "unreachable_giveup_policy_tests: OK\n";
    return 0;
}
