// twow-repo#703 item 4 (UnreachableGiveUpPolicy.h, AiPlayerbot.Rotation.UnreachableGiveUp, default 0 until its A/B; design v2
// 85574eca, OB-10 OK 11.11.2026): the per-guid count of [Unreachable] results, the 1 h mark guard, the gate and the eviction check;
// twow-repo#703 item 4b (Rotation.UnreachableLock): the locks, the gate, Find, expiry, slots, the self-defence table, the block line.
#include "UnreachableGiveUpPolicy.h"
#include "CohortPolicy.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
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

    // ---- twow-repo#703 item 4b (Rotation.UnreachableLock): the lock after a give-up ----------------------------------------
    // The constants of the design (20.11.2026, L1-L8).
    Require(LockSeconds == 1800 && LockSeconds == LongIgnoreSeconds && EntryRadiusYards == 100.0f && LockSlots == 4 &&
        LockLogSeconds == 60, "lock constants");

    // Modes 0-3 for an odd (cohort B) and an even (cohort A) guid; a bot in a group never (solo only, L3).
    for (std::uint32_t guidLow : { 1u, 2u })
    {
        bool const odd = guidLow % 2 == 1;
        Require(!LockActive(0, ai::cohort::Applies(0, guidLow), false), "lock mode 0: off for every bot");
        Require(LockActive(1, ai::cohort::Applies(1, guidLow), false), "lock mode 1: every bot");
        Require(LockActive(2, ai::cohort::Applies(2, guidLow), false) == odd, "lock mode 2: cohort B (odd) only");
        Require(LockActive(3, ai::cohort::Applies(3, guidLow), false) == !odd, "lock mode 3: cohort A (even) only");
        for (std::uint32_t mode = 0; mode <= 3; ++mode)
            Require(!LockActive(mode, ai::cohort::Applies(mode, guidLow), true), "lock: a bot in a group never");
    }
    Require(!LockActive(0, true, false), "lock mode 0 is off on its own (not only through the cohort)");

    // Find: the guid anywhere; a sibling of the same entry on the same map within 100 yd (2D) of the spot; Guid wins.
    {
        Locks locks;
        Require(!locks.Any(1000), "no lock: the fast path is off");
        Require(!locks.Arm(a, 580, 1, 100.0f, 200.0f, 1000), "first lock evicts nothing");
        Require(locks.Any(1000), "armed: the fast path is on");
        Require(locks.Find(a, 580, 1, 100.0f, 200.0f, 1000) == Match::Guid, "the guid at the spot");
        Require(locks.Find(a, 580, 0, -5000.0f, 9000.0f, 1000) == Match::Guid, "the guid anywhere, even on another map");
        Require(locks.Find(a, 999, 1, 100.0f, 200.0f, 1000) == Match::Guid, "the guid whatever entry is passed");
        Require(locks.Find(b, 580, 1, 100.0f + 99.0f, 200.0f, 1000) == Match::Entry, "a sibling at 99 yd");
        Require(locks.Find(b, 580, 1, 100.0f, 200.0f - 99.0f, 1000) == Match::Entry, "a sibling at 99 yd (y)");
        Require(locks.Find(b, 580, 1, 200.0f, 200.0f, 1000) == Match::Entry, "a sibling at exactly 100 yd: locked (<=)");
        Require(locks.Find(b, 580, 1, 100.0f, 300.0f, 1000) == Match::Entry, "a sibling at exactly 100 yd (y): locked (<=)");
        Require(locks.Find(b, 580, 1, 100.0f + 101.0f, 200.0f, 1000) == Match::None, "a sibling at 101 yd: free");
        Require(locks.Find(b, 580, 2, 100.0f, 200.0f, 1000) == Match::None, "a sibling on another map: free");
        Require(locks.Find(b, 581, 1, 100.0f, 200.0f, 1000) == Match::None, "another entry: free");
        Require(locks.Find(b, 0, 1, 100.0f, 200.0f, 1000) == Match::None, "entry 0 never matches");
        Require(locks.Find(0, 580, 1, 100.0f, 200.0f, 1000) == Match::Entry, "guid 0 is no guid match (entry still counts)");
        Require(locks.Find(0, 0, 1, 100.0f, 200.0f, 1000) == Match::None, "guid 0 and entry 0: nothing");
        Require(locks.LeftFor(Match::Guid, a, 580, 1, 100.0f, 200.0f, 1500) == 1300, "seconds left of the guid lock");
        Require(locks.LeftFor(Match::Entry, b, 580, 1, 100.0f, 200.0f, 1500) == 1300, "seconds left of the entry lock");
        Require(locks.LeftFor(Match::None, b, 580, 1, 100.0f, 200.0f, 1500) == 0, "no match, no seconds");

        // Guid wins over Entry: b's own lock elsewhere, a's sibling lock at the spot.
        locks.Arm(b, 580, 1, 5000.0f, 5000.0f, 1000);
        Require(locks.Find(b, 580, 1, 100.0f, 200.0f, 1000) == Match::Guid, "Guid wins over Entry");

        // A lock armed with guid 0 never matches by guid; one armed with entry 0 never by entry.
        Locks zero;
        zero.Arm(0, 0, 1, 0.0f, 0.0f, 1000);
        Require(zero.Find(0, 0, 1, 0.0f, 0.0f, 1000) == Match::None, "a zero lock matches nothing");
    }

    // Expiry: live until +1799, gone at +1800 (own clock, until > now).
    {
        Locks locks;
        locks.Arm(a, 580, 1, 0.0f, 0.0f, 1000);
        Require(locks.Find(a, 580, 1, 0.0f, 0.0f, 1000 + 1799) == Match::Guid, "guid live at +1799");
        Require(locks.Find(b, 580, 1, 10.0f, 0.0f, 1000 + 1799) == Match::Entry, "entry live at +1799");
        Require(locks.Any(1000 + 1799), "fast path live at +1799");
        Require(locks.Find(a, 580, 1, 0.0f, 0.0f, 1000 + 1800) == Match::None, "guid gone at +1800");
        Require(locks.Find(b, 580, 1, 10.0f, 0.0f, 1000 + 1800) == Match::None, "entry gone at +1800");
        Require(!locks.Any(1000 + 1800), "fast path off at +1800");
    }

    // Slots: its own guid again refreshes its slot; run-out slots are reused first; a 5th live lock evicts the one that runs out first.
    {
        Locks locks;
        Require(!locks.Arm(a, 1, 1, 0.0f, 0.0f, 100) && !locks.Arm(b, 2, 1, 0.0f, 0.0f, 200) &&
            !locks.Arm(c, 3, 1, 0.0f, 0.0f, 300) && !locks.Arm(d, 4, 1, 0.0f, 0.0f, 400), "four locks fill the slots");
        Require(!locks.Arm(a, 1, 1, 0.0f, 0.0f, 500), "the same guid again takes its own slot (no eviction)");
        Require(locks.LeftFor(Match::Guid, a, 1, 1, 0.0f, 0.0f, 500) == LockSeconds, "and restarts its span");
        Require(locks.Arm(e, 5, 1, 0.0f, 0.0f, 600), "a fifth live lock evicts");
        Require(locks.Find(b, 2, 1, 0.0f, 0.0f, 600) == Match::None, "the one that runs out first (B, armed 200) left");
        Require(locks.Find(a, 1, 1, 0.0f, 0.0f, 600) == Match::Guid && locks.Find(c, 3, 1, 0.0f, 0.0f, 600) == Match::Guid &&
            locks.Find(d, 4, 1, 0.0f, 0.0f, 600) == Match::Guid && locks.Find(e, 5, 1, 0.0f, 0.0f, 600) == Match::Guid, "the others stay");
        // At 300 + 1800 C has run out: the next lock takes that slot and evicts nothing.
        Require(!locks.Arm(b, 2, 1, 0.0f, 0.0f, 2100), "a run-out slot is reused first (no eviction)");
        Require(locks.Find(d, 4, 1, 0.0f, 0.0f, 2100) == Match::Guid && locks.Find(e, 5, 1, 0.0f, 0.0f, 2100) == Match::Guid &&
            locks.Find(a, 1, 1, 0.0f, 0.0f, 2100) == Match::Guid, "the live locks stay");
    }

    // The self-defence table per match type (L2).
    Require(MayDefend(Match::Entry, true, false) && MayDefend(Match::Entry, true, true), "sibling hitting the bot or pet: defend");
    Require(!MayDefend(Match::Entry, false, true) && !MayDefend(Match::Entry, false, false), "sibling not hitting: locked");
    Require(MayDefend(Match::Guid, true, true), "given-up unit hitting and hittable now: defend");
    Require(!MayDefend(Match::Guid, true, false), "given-up unit hitting but not hittable: locked");
    Require(!MayDefend(Match::Guid, false, true) && !MayDefend(Match::Guid, false, false), "given-up unit not hitting: locked");
    Require(MayDefend(Match::None, false, false), "no lock: pickable");
    Require(std::string(MatchName(Match::Guid)) == "guid" && std::string(MatchName(Match::Entry)) == "entry", "match names");
    Require(std::string(PathName(Path::GrindAttacker)) == "grind_att" && std::string(PathName(Path::GrindCandidate)) == "grind_cand" &&
        std::string(PathName(Path::Assist)) == "assist" && std::string(PathName(Path::Attack)) == "attack" &&
        std::string(PathName(Path::Pull)) == "pull", "path names");

    // The block line budget: the first block is due, then one per LockLogSeconds; counts per path add up; defended never due.
    {
        BlockLog log;
        Require(!log.Note(Path::Assist, true, 10), "a defended verdict is never due");
        Require(log.defended == 1, "and counts as defended");
        Require(log.Note(Path::Assist, false, 10), "the first block is due");
        log.Reset(10);
        Require(log.defended == 0 && log.Blocked(Path::Assist) == 0, "the line resets the counts");
        Require(!log.Note(Path::GrindCandidate, false, 11), "a second block within the minute: not due");
        Require(!log.Note(Path::GrindCandidate, false, 30), "still not due");
        Require(!log.Note(Path::Pull, false, 69), "59 s after the line: not due");
        Require(!log.Note(Path::Attack, true, 70), "defended at 60 s: not due");
        Require(log.Note(Path::Attack, false, 70), "60 s after the line: due");
        Require(log.Blocked(Path::GrindCandidate) == 2 && log.Blocked(Path::Pull) == 1 && log.Blocked(Path::Attack) == 1 &&
            log.Blocked(Path::Assist) == 0 && log.Blocked(Path::GrindAttacker) == 0 && log.defended == 1, "the counts per path");
    }

    std::cout << "unreachable_giveup_policy_tests: OK\n";
    return 0;
}
