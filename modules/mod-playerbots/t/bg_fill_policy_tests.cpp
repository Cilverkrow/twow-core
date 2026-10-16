#include "BgFillPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
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

// Owner 16.10.: a real player's queue entry of size k (his group with his own bots). k = 1 gives exactly the stage-b
// results, checked over the whole range against the stage-b formulas (follow-up review: but no release where the core
// invites him already, which the AV differential allows even while his side is ahead).
void GroupEntry()
{
    using namespace ai::bgfill;
    for (uint32_t maxPerTeam : { 0u, 1u, 10u, 15u, 40u })
    {
        for (uint32_t s = 0; s <= maxPerTeam; ++s)
        {
            for (uint32_t o = 0; o <= maxPerTeam; ++o)
            {
                for (uint32_t d : { 0u, DifferentialAV })
                {
                    uint32_t const diff = s > o ? s - o : o - s;
                    Require(PairPossible(s, o, maxPerTeam, 1) == (s < maxPerTeam && o < maxPerTeam), "k = 1: pair as stage b");
                    Require(PairPossible(s, o, maxPerTeam, 1) == PairPossible(s, o, maxPerTeam), "k = 1 is the default: pair");
                    Require(ReleaseAllowed(s, o, 1) == (s > o), "k = 1: release above the minimum as stage b");
                    Require(ReleaseAllowed(s, o, 1) == ReleaseAllowed(s, o), "k = 1 is the default: minimum");
                    Require(ReleaseInvitesPlayer(s, o, d, 1) == (diff <= d), "k = 1: balance as stage b");
                    Require(ReleaseInvitesPlayer(s, o, d, 1) == ReleaseInvitesPlayer(s, o, d), "k = 1 is the default: balance");
                    Require(TopUpNeeded(s, o, maxPerTeam, d, 1) == (s >= maxPerTeam && o < maxPerTeam && diff > d), "k = 1: top-up as stage b");
                    Require(TopUpNeeded(s, o, maxPerTeam, d, 1) == TopUpNeeded(s, o, maxPerTeam, d), "k = 1 is the default: top-up");
                    Require(ReleaseCount(s, o, maxPerTeam, d, 1) ==
                        (s >= 1 && diff <= d && !CoreInvitesEntry(s, o, maxPerTeam, d, 1) ? 1u : 0u),
                        "k = 1: one release exactly when balanced and the core does not invite him already");
                    Require(PairCount(s, o, d, 1) == 1 + (s > o + d ? s - o - d : 0), "k = 1: one pair bot plus the lead beyond the differential");
                    Require(ReleaseCount(s, o, maxPerTeam, d, 1) == ReleaseCount(s, o, maxPerTeam, d), "k = 1 is the default: count");
                }
            }
        }
    }

    // The core's FillPlayersToBG for one entry alone (InvitationType 1).
    Require(!CoreInvitesEntry(10, 10, 10, 0, 1), "WSG full: no slot");
    Require(!CoreInvitesEntry(9, 9, 10, 0, 1), "WSG equal counts block a lone invite");
    Require(CoreInvitesEntry(9, 10, 10, 0, 1), "WSG his side behind: invited");
    Require(CoreInvitesEntry(8, 10, 10, 0, 2), "WSG pair of two, his side two behind: invited");
    Require(!CoreInvitesEntry(8, 9, 10, 0, 2), "WSG pair of two, his side one behind: kicked");
    Require(!CoreInvitesEntry(9, 10, 10, 0, 2), "WSG one free slot for a group of two: not selected");
    Require(CoreInvitesEntry(38, 37, 40, DifferentialAV, 1), "AV his side ahead within the differential");
    Require(!CoreInvitesEntry(0, 0, 10, 0, 0), "an empty entry is nobody");

    // Pair: k free slots on both sides.
    Require(PairPossible(8, 8, 10, 2), "group of two, two free on both sides");
    Require(!PairPossible(9, 8, 10, 2), "group of two, alliance one free");
    Require(!PairPossible(8, 9, 10, 2), "group of two, horde one free");
    Require(PairPossible(0, 0, 10, 10), "a full side's group into an empty battleground");
    Require(!PairPossible(0, 0, 10, 11), "group larger than a side");

    // Minimum: the side stays at or above it after all releases of the decision.
    Require(ReleaseAllowed(7, 5, 2), "two releases from 7, minimum 5");
    Require(!ReleaseAllowed(6, 5, 2), "two releases from 6, minimum 5");
    Require(ReleaseAllowed(10, 5, 5), "five releases from a full side");
    Require(!ReleaseAllowed(5, 5, 0), "never from a side at the minimum");

    // Releases for one decision: the fewest that get the entry in, 0 = none does.
    Require(ReleaseCount(10, 10, 10, 0, 2) == 2, "WSG full, group of two: two releases");
    Require(ReleaseCount(9, 9, 10, 0, 2) == 2, "WSG 9v9, group of two: two releases");
    Require(ReleaseCount(9, 10, 10, 0, 2) == 1, "WSG his side one behind, group of two: one release");
    Require(ReleaseCount(10, 9, 10, 0, 2) == 0, "WSG his side ahead: no release (top-up first)");
    Require(ReleaseCount(8, 10, 10, 0, 2) == 0, "WSG the core invites the group without a release");
    Require(ReleaseCount(5, 5, 10, 0, 3) == 3, "WSG 5v5, group of three: three releases");
    Require(ReleaseCount(1, 1, 10, 0, 2) == 0, "fewer bots on his side than releases needed");
    Require(ReleaseCount(10, 10, 10, 0, 11) == 0, "group larger than a side");
    Require(ReleaseCount(10, 10, 10, 0, 0) == 0, "empty entry");
    // Follow-up review: AV Horde 40, Alliance 38 - the core invites the Alliance player without a release.
    Require(CoreInvitesEntry(38, 40, 40, DifferentialAV, 1), "AV his side two behind: invited");
    Require(ReleaseCount(38, 40, 40, DifferentialAV, 1) == 0, "AV no release where the core invites without one");
    Require(ReleaseCount(40, 38, 40, DifferentialAV, 1) == 1, "AV his side full and ahead within the differential: one release");
    Require(ReleaseCount(39, 36, 40, DifferentialAV, 1) == 1, "AV his side three ahead: one release");
    // The count is the fewest, at most k, and the core then invites the entry; whenever the balance allows it, his side
    // has k bots and the core does not invite the entry already, some count does. Both differentials.
    for (uint32_t d : { 0u, DifferentialAV })
    {
        uint32_t const maxPerTeam = d ? 40 : 10;
        for (uint32_t k = 1; k <= 6; ++k)
        {
            for (uint32_t s = 0; s <= maxPerTeam; ++s)
            {
                for (uint32_t o = 0; o <= maxPerTeam; ++o)
                {
                    uint32_t const r = ReleaseCount(s, o, maxPerTeam, d, k);
                    Require(r <= k, "never more releases than members");
                    if (r)
                    {
                        Require(CoreInvitesEntry(s - r, o, maxPerTeam, d, k), "the core invites the entry after the releases");
                        Require(r == 1 || !CoreInvitesEntry(s - r + 1, o, maxPerTeam, d, k), "the fewest releases");
                        Require(!CoreInvitesEntry(s, o, maxPerTeam, d, k), "no release where the core invites without one");
                    }
                    else if (ReleaseInvitesPlayer(s, o, d, k) && s >= k && !CoreInvitesEntry(s, o, maxPerTeam, d, k))
                        Require(false, "a balanced side with k bots gets a release count");
                }
            }
        }
    }

    // Follow-up review: pair bots for an entry of size k - the lead of his side beyond the differential goes along, so
    // the free slots are even after the invite (the core's balance loop then keeps the entry and every bot) and the bots
    // still fit (PairPossible).
    Require(PairCount(6, 6, 0, 3) == 3, "WSG even counts: k bots");
    Require(PairCount(7, 6, 0, 3) == 4, "WSG 7v6, group of three: four bots");
    Require(PairCount(11, 9, 0, 4) == 6, "AB 11v9, group of four: six bots");
    Require(PairCount(6, 7, 0, 3) == 3, "WSG his side behind: k bots");
    Require(PairCount(30, 27, DifferentialAV, 1) == 1, "AV lead within the differential: one bot");
    Require(PairCount(31, 27, DifferentialAV, 1) == 2, "AV lead one beyond the differential: two bots");
    for (uint32_t d : { 0u, DifferentialAV })
    {
        uint32_t const maxPerTeam = d ? 40 : 15;
        for (uint32_t k = 1; k <= 6; ++k)
        {
            for (uint32_t s = 0; s <= maxPerTeam; ++s)
            {
                for (uint32_t o = 0; o <= maxPerTeam; ++o)
                {
                    if (!PairPossible(s, o, maxPerTeam, k))
                        continue;
                    uint32_t const count = PairCount(s, o, d, k);
                    Require(count >= k, "at least one bot per member");
                    Require(o + count <= maxPerTeam, "the pair bots fit on the other side");
                    // Free slots after the selection: his side max - s - k, the other side max - o - count.
                    int64_t const freeSide = int64_t(maxPerTeam) - s - k;
                    int64_t const freeOther = int64_t(maxPerTeam) - o - count;
                    int64_t const gap = freeSide > freeOther ? freeSide - freeOther : freeOther - freeSide;
                    Require(s < o || gap <= int64_t(d), "his side ahead: the free slots within the differential after the pair");
                }
            }
        }
    }

    // Top-up: his side lacks k free slots, the other side is short and releases alone would not get the entry in.
    Require(TopUpNeeded(9, 7, 10, 0, 2), "group of two, his side one free and ahead: top-up");
    Require(!TopUpNeeded(9, 9, 10, 0, 2), "group of two, 9v9: the releases apply");
    Require(!TopUpNeeded(8, 8, 10, 0, 2), "group of two, two free on both sides: the pair option applies");
    Require(!TopUpNeeded(9, 10, 10, 0, 2), "other side full");
}

// Follow-up D: drains a bot-only battleground as LeaveBotOnlyBg does - the side from BotOnlyDrainSide, a leave only where
// BotOnlyLeaveAllowed holds - and returns the counts where it stops. Every leave is checked: the core never invites a
// waiting entry of the leaving side afterwards, unless the pause is over.
std::pair<uint32_t, uint32_t> Drain(uint32_t a, uint32_t h, uint32_t maxPerTeam, uint32_t d, uint32_t waitA, uint32_t waitH,
    bool pauseOver)
{
    using namespace ai::bgfill;
    for (uint32_t step = 0; step <= 2 * maxPerTeam; ++step)
    {
        bool const leaveA = BotOnlyLeaveAllowed(a, h, maxPerTeam, d, waitA, pauseOver);
        bool const leaveH = BotOnlyLeaveAllowed(h, a, maxPerTeam, d, waitH, pauseOver);
        uint32_t const side = BotOnlyDrainSide(a, h, leaveA, leaveH);
        if (side == NoDrainSide)
            break;
        Require(side <= 1, "a drain side is alliance or horde");
        if (side == 0)
        {
            Require(a > 0, "no leave from an empty alliance side");
            --a;
            Require(pauseOver || !waitA || !CoreInvitesEntry(a, h, maxPerTeam, d, waitA), "no alliance invite after the leave");
        }
        else
        {
            Require(h > 0, "no leave from an empty horde side");
            --h;
            Require(pauseOver || !waitH || !CoreInvitesEntry(h, a, maxPerTeam, d, waitH), "no horde invite after the leave");
        }
    }
    return { a, h };
}

void BotOnlyDrain()
{
    using namespace ai::bgfill;
    Require(BotOnlyLeavesPerUpdate >= 1 && BotOnlyLeavesPerUpdate <= 4, "a small fixed number of leaves per update");
    Require(NoDrainSide > 1, "no side is neither alliance nor horde");

    // Leave rule: nobody waits, an empty side, or the pause is over - the bot may leave.
    Require(BotOnlyLeaveAllowed(5, 5, 10, 0, 0, false), "nobody waits");
    Require(BotOnlyLeaveAllowed(0, 5, 10, 0, 1, false), "nothing on the side");
    Require(BotOnlyLeaveAllowed(5, 5, 10, 0, 1, true), "pause over");
    // WSG/AB: a player waiting on that side is invited once his side falls behind.
    Require(!BotOnlyLeaveAllowed(5, 5, 10, 0, 1, false), "WSG equal counts: the leave would put him in");
    Require(BotOnlyLeaveAllowed(6, 5, 10, 0, 1, false), "WSG his side ahead: the leave evens it");
    Require(BotOnlyLeaveAllowed(5, 5, 10, 0, 2, false), "WSG a group of two needs two slots behind");
    Require(!BotOnlyLeaveAllowed(5, 6, 10, 0, 2, false), "WSG group of two, his side one behind already");
    // AV: the differential lets him in while his side is up to three ahead.
    Require(!BotOnlyLeaveAllowed(20, 18, 40, DifferentialAV, 1, false), "AV his side two ahead");
    Require(BotOnlyLeaveAllowed(22, 18, 40, DifferentialAV, 1, false), "AV his side four ahead");

    // Side: the larger side first, alliance on a tie, only sides that may leave and hold a bot.
    Require(BotOnlyDrainSide(5, 5, true, true) == 0, "tie: alliance");
    Require(BotOnlyDrainSide(4, 5, true, true) == 1, "horde larger");
    Require(BotOnlyDrainSide(6, 5, true, true) == 0, "alliance larger");
    Require(BotOnlyDrainSide(6, 5, false, true) == 1, "alliance may not leave");
    Require(BotOnlyDrainSide(0, 5, true, false) == NoDrainSide, "alliance empty, horde may not leave");
    Require(BotOnlyDrainSide(0, 0, true, true) == NoDrainSide, "empty battleground");
    Require(BotOnlyDrainSide(0, 3, true, true) == 1, "one side empty: the other goes on");

    // Nobody waits: every start drains to empty.
    for (uint32_t a = 0; a <= 15; ++a)
        for (uint32_t h = 0; h <= 15; ++h)
            Require(Drain(a, h, 15, 0, 0, 0, false) == std::make_pair(0u, 0u), "nobody waits: drained to empty");
    // WSG/AB with a player (or group up to a side) waiting on one side only: drained to empty without inviting him.
    for (uint32_t k = 1; k <= 10; ++k)
    {
        for (uint32_t a = 0; a <= 10; ++a)
        {
            for (uint32_t h = 0; h <= 10; ++h)
            {
                Require(Drain(a, h, 10, 0, k, 0, false) == std::make_pair(0u, 0u), "WSG waiting on alliance: drained to empty");
                Require(Drain(a, h, 10, 0, 0, k, false) == std::make_pair(0u, 0u), "WSG waiting on horde: drained to empty");
            }
        }
    }
    // Players waiting on both sides: even counts hold at once (the bounded pause), the pause over drains it.
    Require(Drain(10, 10, 10, 0, 1, 1, false) == std::make_pair(10u, 10u), "WSG waiting on both sides: held");
    Require(Drain(10, 10, 10, 0, 1, 1, true) == std::make_pair(0u, 0u), "WSG waiting on both sides, pause over: empty");
    // AV with a player waiting on alliance: horde drains freely, alliance only while four ahead - three stay (held).
    Require(Drain(20, 20, 40, DifferentialAV, 1, 0, false) == std::make_pair(3u, 0u), "AV waiting on alliance: three held");
    Require(Drain(20, 20, 40, DifferentialAV, 1, 0, true) == std::make_pair(0u, 0u), "AV pause over: empty");
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
    GroupEntry();
    BotOnlyDrain();
    Victim();
    std::cout << "bg fill policy tests passed\n";
    return 0;
}
