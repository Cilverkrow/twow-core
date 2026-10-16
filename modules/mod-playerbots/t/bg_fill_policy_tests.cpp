#include "BgFillPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// twow-repo#517 stage a and b: the pure rules of the manager's battleground fill (BgFillPolicy.h).

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

void QueueTypes()
{
    using namespace ai::bgfill;
    Require(IsFillQueueType(QueueAV), "AV is filled");
    Require(IsFillQueueType(QueueWS), "WSG is filled");
    Require(IsFillQueueType(QueueAB), "AB is filled");
    Require(!IsFillQueueType(QueueBR), "Blood Ring 3v3 is excluded (OB-00 D4)");
    Require(!IsFillQueueType(QueueSV), "Sunnyglade Valley is excluded in stage a");
    Require(!IsFillQueueType(0), "no queue type");
    Require(!IsFillQueueType(6), "beyond MAX_BATTLEGROUND_QUEUE_TYPES");
    Require(QueueAV == 1 && QueueWS == 2 && QueueAB == 3 && QueueBR == 4 && QueueSV == 5, "core queue type values");
}

void Need()
{
    using ai::bgfill::NeedPerSide;
    // One real player on his side, nobody queued yet: his side needs min-1, the other side min.
    Require(NeedPerSide(5, 1, 0) == 4, "player side min-1");
    Require(NeedPerSide(5, 0, 0) == 5, "other side min");
    // Bots already queued for the side count.
    Require(NeedPerSide(5, 1, 4) == 0, "player side complete");
    Require(NeedPerSide(5, 0, 3) == 2, "other side partly queued");
    // Never below zero, also with more real players than the minimum.
    Require(NeedPerSide(5, 7, 0) == 0, "more real players than min");
    Require(NeedPerSide(5, 3, 9) == 0, "more pending than needed");
    Require(NeedPerSide(0, 0, 0) == 0, "template minimum 0");
    // AV-sized minimum.
    Require(NeedPerSide(20, 1, 0) == 19, "AV player side");
    Require(NeedPerSide(20, 0, 0) == 20, "AV other side");

    using ai::bgfill::QueueMinBlocksFill;
    // Alterac.MinPlayersInQueue 0 (default, and every other battleground): the fill runs.
    Require(!QueueMinBlocksFill(20, 0), "no queue minimum");
    Require(!QueueMinBlocksFill(20, 10), "queue minimum below the template");
    Require(!QueueMinBlocksFill(20, 20), "queue minimum equal to the template");
    // Above the template: the core shuffles more entries than it takes, the real player could be left out.
    Require(QueueMinBlocksFill(20, 21), "queue minimum above the template");
    Require(QueueMinBlocksFill(0, 1), "queue minimum above a zero template minimum");
}

void Cap()
{
    using ai::bgfill::CapAllows;
    using ai::bgfill::RunningForBracket;
    // Fail closed: 0 = no bot battleground at all.
    Require(!CapAllows(0, 0), "MaxConcurrent 0 allows nothing");
    Require(CapAllows(0, 1), "first of one");
    Require(!CapAllows(1, 1), "one running of one");
    Require(CapAllows(1, 2), "second of two");
    Require(!CapAllows(3, 2), "over the cap");

    Require(RunningForBracket(0, 0, false) == 0, "nothing running");
    Require(RunningForBracket(1, 0, false) == 1, "one started bot battleground");
    Require(RunningForBracket(0, 1, false) == 1, "another bracket forming");
    Require(RunningForBracket(0, 1, true) == 0, "own forming bracket does not block its top-up");
    Require(RunningForBracket(1, 2, true) == 2, "own forming excluded, the rest counted");
    Require(RunningForBracket(2, 0, true) == 2, "selfForming without a forming bracket changes nothing");
    // With MaxConcurrent 1 a forming bracket may refill itself, a second bracket may not start.
    Require(CapAllows(RunningForBracket(0, 1, true), 1), "own top-up under cap 1");
    Require(!CapAllows(RunningForBracket(0, 1, false), 1), "second bracket blocked under cap 1");
}

void Interval()
{
    using namespace ai::bgfill;
    Require(CheckIntervalMs(0) == 30000, "0 = 30 s internal");
    Require(CheckIntervalMs(1) == 10000, "minimum 10 s");
    Require(CheckIntervalMs(9) == 10000, "below the minimum");
    Require(CheckIntervalMs(10) == 10000, "exactly the minimum");
    Require(CheckIntervalMs(45) == 45000, "configured value");
    Require(CheckIntervalMs(4000000) == MaxCheckIntervalSeconds * 1000, "no overflow");

    Require(IntervalDue(5, 0, 30000), "never run: due at once");
    Require(!IntervalDue(1000 + 29999, 1000, 30000), "not yet due");
    Require(IntervalDue(1000 + 30000, 1000, 30000), "due");
    // Millisecond clock wrapped between the two checks.
    Require(IntervalDue(20000u, 0xFFFFFFFFu - 9999u, 30000), "due across the wrap");
    Require(!IntervalDue(10000u, 0xFFFFFFFFu - 9999u, 30000), "not due across the wrap");
}

void Pending()
{
    using namespace ai::bgfill;
    uint32_t const maxMs = PendingMaxSeconds * 1000;
    Require(PendingMaxSeconds >= 60 && PendingMaxSeconds <= 600, "pending bound of a few minutes");
    Require(!PendingExpired(1000 + maxMs - 1, 1000, maxMs), "not expired yet");
    Require(PendingExpired(1000 + maxMs, 1000, maxMs), "expired");
    // Millisecond clock wrapped while the bot waited.
    Require(!PendingExpired(1000u, 0xFFFFFFFFu - 999u, maxMs), "not expired across the wrap");
    Require(PendingExpired(maxMs, 0xFFFFFFFFu - 999u, maxMs), "expired across the wrap");
}

void Candidates()
{
    using namespace ai::bgfill;
    Require(CandidateLimit(0) == 0, "no need, no candidates");
    Require(CandidateLimit(4) == 4 * CandidateFactor, "need times the factor");
    Require(CandidateFactor >= 1 && CandidateFactor <= 3, "small factor");
    Require(MinLoggedInSeconds == 30, "same login wait as BGJoinAction");
}

// Stage b (AiPlayerbot.BG.SlotRelease).
void StageBTimes()
{
    using namespace ai::bgfill;
    Require(SlotReleaseWaitMs(0) == 60000, "wait 0 = 60 s internal");
    Require(SlotReleaseWaitMs(1) == 15000, "wait minimum 15 s");
    Require(SlotReleaseWaitMs(14) == 15000, "wait below the minimum");
    Require(SlotReleaseWaitMs(15) == 15000, "wait exactly the minimum");
    Require(SlotReleaseWaitMs(90) == 90000, "wait configured");
    Require(SlotReleaseWaitMs(4000000) == MaxStageBSeconds * 1000, "wait no overflow");

    Require(BotOnlyGraceMs(0) == 120000, "grace 0 = 120 s internal");
    Require(BotOnlyGraceMs(30) == 60000, "grace minimum 60 s");
    Require(BotOnlyGraceMs(60) == 60000, "grace exactly the minimum");
    Require(BotOnlyGraceMs(300) == 300000, "grace configured");
    Require(BotOnlyGraceMs(4000000) == MaxStageBSeconds * 1000, "grace no overflow");

    Require(!DeadlinePassed(1000, 2000), "deadline ahead");
    Require(DeadlinePassed(2000, 2000), "deadline reached");
    Require(DeadlinePassed(2001, 2000), "deadline passed");
    // Millisecond clock wrapped between the reservation and the check.
    Require(!DeadlinePassed(0xFFFFFFFFu - 10u, 5000u), "deadline ahead across the wrap");
    Require(DeadlinePassed(6000u, 0xFFFFFFFFu - 10u), "deadline passed across the wrap");
}

void PairAndRelease()
{
    using namespace ai::bgfill;
    // Pair option: a free slot on both sides (counts are the core's invited counts).
    Require(PairPossible(5, 5, 10), "both sides free");
    Require(PairPossible(9, 9, 10), "one slot left on both sides");
    Require(!PairPossible(10, 9, 10), "alliance full");
    Require(!PairPossible(9, 10, 10), "horde full");
    Require(!PairPossible(10, 10, 10), "both full");
    Require(!PairPossible(0, 0, 0), "no slots at all");

    // Release only from a side above the minimum (G2: the side stays at or above it, no PrematureFinish).
    Require(!ReleaseAllowed(5, 5), "side at the minimum");
    Require(!ReleaseAllowed(4, 5), "side below the minimum");
    Require(ReleaseAllowed(6, 5), "side one above the minimum");
    Require(ReleaseAllowed(10, 5), "full side");
    Require(!ReleaseAllowed(0, 0), "empty side, minimum 0");

    // InvitationType 1: after the release the lone player is invited only if the invited counts were balanced.
    Require(ReleaseInvitesPlayer(10, 10, 0), "WSG both full");
    Require(!ReleaseInvitesPlayer(10, 9, 0), "WSG player side ahead");
    Require(!ReleaseInvitesPlayer(9, 10, 0), "WSG player side behind (he would be invited without a release)");
    Require(ReleaseInvitesPlayer(40, 37, DifferentialAV), "AV within the differential");
    Require(!ReleaseInvitesPlayer(40, 36, DifferentialAV), "AV beyond the differential");
    Require(ReleaseInvitesPlayer(36, 39, DifferentialAV), "AV other side ahead within the differential");

    // Top-up: his side full, the other side short and a release alone would not invite him.
    Require(TopUpNeeded(10, 9, 10, 0), "WSG player side full, other side one short");
    Require(TopUpNeeded(10, 7, 10, 0), "WSG player side full, other side three short");
    Require(!TopUpNeeded(10, 10, 10, 0), "WSG both full: the release applies");
    Require(!TopUpNeeded(9, 9, 10, 0), "WSG player side not full: the pair option applies");
    Require(!TopUpNeeded(9, 10, 10, 0), "WSG other side full");
    Require(!TopUpNeeded(40, 38, 40, DifferentialAV), "AV within the differential: the release applies");
    Require(TopUpNeeded(40, 36, 40, DifferentialAV), "AV beyond the differential");
    Require(!TopUpNeeded(0, 0, 0, 0), "no slots at all");

    // Target: the most fill bots on the player's side first, then the lowest instance id.
    Require(PreferTarget(3, 9, 2, 1), "more fill bots wins");
    Require(!PreferTarget(2, 1, 3, 9), "fewer fill bots loses");
    Require(PreferTarget(3, 4, 3, 7), "tie: lower instance wins");
    Require(!PreferTarget(3, 7, 3, 4), "tie: higher instance loses");
    Require(!PreferTarget(3, 4, 3, 4), "same instance is not better");
}

ai::bgfill::ReleaseCand Cand(uint32_t guid, uint32_t joinMs, bool flag, bool combat, bool alive)
{
    ai::bgfill::ReleaseCand c;
    c.guid = guid;
    c.joinMs = joinMs;
    c.carriesFlag = flag;
    c.inCombat = combat;
    c.alive = alive;
    return c;
}

void Victim()
{
    using ai::bgfill::ChooseReleaseVictim;
    uint32_t const now = 100000;
    Require(ChooseReleaseVictim({}, now) == 0, "no candidates, no victim");
    // Never a flag carrier, never a dead one.
    Require(ChooseReleaseVictim({ Cand(7, 90000, true, false, true) }, now) == 0, "flag carrier only");
    Require(ChooseReleaseVictim({ Cand(7, 90000, false, false, false) }, now) == 0, "dead only");
    Require(ChooseReleaseVictim({ Cand(7, 90000, true, false, true), Cand(8, 10000, false, true, true) }, now) == 8,
        "flag carrier skipped for an older bot in combat");
    // Out of combat before in combat, even if joined earlier.
    Require(ChooseReleaseVictim({ Cand(7, 90000, false, true, true), Cand(8, 10000, false, false, true) }, now) == 8,
        "out of combat preferred");
    // The latest joined first.
    Require(ChooseReleaseVictim({ Cand(7, 50000, false, false, true), Cand(8, 90000, false, false, true),
        Cand(9, 70000, false, false, true) }, now) == 8, "latest joined");
    // Ties: lowest guid, in any order.
    Require(ChooseReleaseVictim({ Cand(9, 90000, false, false, true), Cand(4, 90000, false, false, true) }, now) == 4,
        "tie: lowest guid");
    Require(ChooseReleaseVictim({ Cand(4, 90000, false, false, true), Cand(9, 90000, false, false, true) }, now) == 4,
        "tie: lowest guid, order independent");
    // Joined just before the millisecond clock wrapped vs just after it: the one after is the latest.
    Require(ChooseReleaseVictim({ Cand(5, 0xFFFFFF00u, false, false, true), Cand(6, 0x100u, false, false, true) }, 0x200u) == 6,
        "latest joined across the wrap");
    Require(ChooseReleaseVictim({ Cand(0, 90000, false, false, true) }, now) == 0, "guid 0 is no victim");
}
}

int main()
{
    QueueTypes();
    Need();
    Cap();
    Interval();
    Pending();
    Candidates();
    StageBTimes();
    PairAndRelease();
    Victim();
    std::cout << "bg fill policy tests passed\n";
    return 0;
}
