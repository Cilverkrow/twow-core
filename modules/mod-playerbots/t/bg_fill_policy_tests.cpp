#include "BgFillPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

// twow-repo#517 stage a: the pure rules of the manager's battleground fill (BgFillPolicy.h).

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

    using ai::bgfill::FillMinPerTeam;
    // Alterac.MinPlayersInQueue 0 (default, and every other battleground): the template minimum.
    Require(FillMinPerTeam(20, 0) == 20, "no queue minimum");
    Require(FillMinPerTeam(20, 30) == 30, "queue minimum above the template");
    Require(FillMinPerTeam(20, 10) == 20, "queue minimum below the template");
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
}

int main()
{
    QueueTypes();
    Need();
    Cap();
    Interval();
    Pending();
    Candidates();
    std::cout << "bg fill policy tests passed\n";
    return 0;
}
