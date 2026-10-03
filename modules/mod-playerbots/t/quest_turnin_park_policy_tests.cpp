// twow-repo#485: turn-ins that keep failing are parked (QuestTurnInParkPolicy.h).
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "QuestTurnInParkPolicy.h"
#include "QuestSearchPolicy.h"

#include <cstdint>
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

bool Has(ai::turnin_park::Book const& book, std::uint32_t questId)
{
    for (ai::turnin_park::Entry const& entry : book.entries)
        if (entry.questId == questId)
            return true;
    return false;
}

// The no_route inputs as ChooseTravelTargetAction::Execute sums up the reject counts of the
// choice (quest_turnin_park_source_contract pins the same sums at both call sites).
bool NoRoute(ai::turnin_park::RouteOutcome outcome, ai::quest_search::RejectCounts const& rejects, bool routeDangerCounts)
{
    return ai::turnin_park::CountsAsNoRoute(outcome, false,
        rejects.crossMap + rejects.zoneLevel + rejects.dangerMap, rejects.turnInSuppressed,
        rejects.movedAway + rejects.rangeSkip + rejects.hubFilter + rejects.resumeSkipped, routeDangerCounts);
}

// Owner profile proposal: 3 failures within 3600 s park for 3600 s.
constexpr std::uint32_t Window = 3600 * 1000;
constexpr std::uint32_t Park = 3600 * 1000;
}

int main()
{
    using namespace ai::turnin_park;

    // 0 = off, the neutral default: nothing ever parks.
    {
        Book book;
        for (std::uint32_t i = 0; i < 10; ++i)
            Require(!RecordFailure(book, 310, 1000 + i, 0, Window, Park), "max 0 never parks");
        Require(!IsParked(book, 310, 2000) && ParkedCount(book, 2000) == 0, "max 0 leaves nothing parked");
        Require(!RecordFailure(book, 310, 1000, 1, Window, 0), "a park of 0 ms never parks");
        Require(!RecordFailure(book, 0, 1000, 1, Window, Park) && !IsParked(book, 0, 1000), "quest 0 is never parked");
    }

    // The third failure within the window parks, the second does not.
    {
        Book book;
        Require(!RecordFailure(book, 310, 1000, 3, Window, Park), "first failure");
        Require(!RecordFailure(book, 310, 2000, 3, Window, Park), "the second failure does not park");
        Require(!IsParked(book, 310, 2000), "not parked after two failures");
        Require(RecordFailure(book, 310, 3000, 3, Window, Park), "the third failure within the window parks");
        Require(IsParked(book, 310, 3001), "parked");
        Require(!IsParked(book, 5722, 3001), "other quests are not parked");

        // Parked until parkedUntil, then free; a failure while parked neither counts nor
        // extends the park.
        Require(!RecordFailure(book, 310, 4000, 3, Window, Park), "no new park while parked");
        Require(IsParked(book, 310, 3000 + Park - 1), "parked until parkedUntil");
        Require(!IsParked(book, 310, 3000 + Park), "free at parkedUntil, the park was not extended");

        // After the park the quest is tried again; a new cycle needs three new failures.
        std::uint32_t const later = 3000 + Park + 10;
        Require(!RecordFailure(book, 310, later, 3, Window, Park), "new cycle: first failure");
        Require(!RecordFailure(book, 310, later + 1, 3, Window, Park), "new cycle: second failure");
        Require(RecordFailure(book, 310, later + 2, 3, Window, Park), "new cycle: the third failure parks again");
    }

    // A window that ran out starts the count again.
    {
        Book book;
        Require(!RecordFailure(book, 5722, 0, 3, Window, Park), "window: first failure");
        Require(!RecordFailure(book, 5722, 1000, 3, Window, Park), "window: second failure");
        Require(!RecordFailure(book, 5722, Window + 1, 3, Window, Park), "an expired window restarts the count");
        Require(!RecordFailure(book, 5722, Window + 2, 3, Window, Park), "second failure of the new window");
        Require(RecordFailure(book, 5722, Window + 3, 3, Window, Park), "third failure of the new window parks");
    }

    // Timer overflow: a time before the window start counts as a new window ...
    {
        Book book;
        Require(!RecordFailure(book, 40273, 5000, 2, Window, Park), "overflow: first failure");
        Require(!RecordFailure(book, 40273, 4000, 2, Window, Park), "now before windowStart is a new window");
        Require(RecordFailure(book, 40273, 4001, 2, Window, Park), "the new window counts on");
    }

    // ... while a real wrap of the millisecond timer stays inside the window.
    {
        Book book;
        Require(!RecordFailure(book, 40273, 0xFFFFFF00u, 2, Window, Park), "wrap: first failure");
        Require(RecordFailure(book, 40273, 0x100u, 2, Window, Park), "wrap: 512 ms later is the same window");
        Require(IsParked(book, 40273, 0x200u) && ParkedCount(book, 0x200u) == 1, "wrap: parked after the wrap");
    }

    // An oversized park is clamped below 2^31 ms, so the signed comparison holds.
    {
        Book book;
        Require(RecordFailure(book, 310, 1000, 1, Window, 0xFFFFFFFFu), "an oversized park parks");
        Require(IsParked(book, 310, 1001), "an oversized park is active");
        Require(IsParked(book, 310, 1000 + MaxParkMs - 1) && !IsParked(book, 310, 1000 + MaxParkMs),
            "an oversized park is clamped to MaxParkMs");
    }

    // ParkedCount counts active parks only.
    {
        Book book;
        Require(RecordFailure(book, 310, 0, 1, Window, 1000), "short park");
        Require(RecordFailure(book, 5722, 500, 1, Window, Park), "long park");
        Require(!RecordFailure(book, 40273, 500, 3, Window, Park), "counted, not parked");
        Require(ParkedCount(book, 600) == 2, "two active parks");
        Require(ParkedCount(book, 1000) == 1, "the short park ended");
        Require(ParkedCount(book, 500 + Park) == 0, "both parks ended");
    }

    // Full book: the 17th quest replaces the oldest entry that is not parked ...
    {
        Book book;
        for (std::uint32_t t = 0; t < 3; ++t)
            RecordFailure(book, 1, 100 + t, 3, Window, Park);
        Require(IsParked(book, 1, 200), "quest 1 parked");
        for (std::uint32_t quest = 2; quest <= BookSize; ++quest)
            Require(!RecordFailure(book, quest, 200 + quest, 3, Window, Park), "one failure for quests 2-16");
        Require(Has(book, 2) && !Has(book, 17), "the book is full");
        Require(!RecordFailure(book, 17, 1000, 3, Window, Park), "the 17th quest is counted");
        Require(Has(book, 17) && !Has(book, 2), "the 17th quest replaced the oldest entry that is not parked (quest 2)");
        Require(Has(book, 1) && IsParked(book, 1, 1000), "the parked quest stays");
        Require(Has(book, 3) && Has(book, BookSize), "the younger entries stay");
    }

    // ... and in a book of parks, the park that ends first.
    {
        Book book;
        for (std::uint32_t quest = 1; quest <= BookSize; ++quest)
            Require(RecordFailure(book, quest, 1000 - quest, 1, Window, Park), "max 1 parks at the first failure");
        Require(ParkedCount(book, 1000) == BookSize, "16 parks");
        Require(!RecordFailure(book, 17, 1000, 3, Window, Park), "the 17th quest into a book of parks");
        Require(Has(book, 17) && !Has(book, BookSize), "it replaced the park that ends first (quest 16)");
        Require(ParkedCount(book, 1000) == BookSize - 1, "one park less");
    }

    // (3e) with critic B1.3: when a turn-in-only request counts as no_route. Arguments: outcome,
    // out of time, route danger deferrals (cross map + zone level + death cluster), suppressed
    // turn-in routes, takers not judged (moved away + range skip + hub filter + resume skip),
    // TurnInParkCountsRouteDanger.
    Require(!CountsAsNoRoute(RouteOutcome::Taker, false, 0, 0, 0, false), "a chosen taker is no failure");
    Require(!CountsAsNoRoute(RouteOutcome::Taker, false, 0, 0, 0, true), "a chosen taker is no failure, route danger counting on");
    Require(CountsAsNoRoute(RouteOutcome::Fallback, false, 0, 0, 0, false), "the quest giver fallback: no taker at all");
    Require(CountsAsNoRoute(RouteOutcome::Fallback, false, 1, 1, 1, false), "the fallback counts whatever the givers met or skipped");
    Require(CountsAsNoRoute(RouteOutcome::NoTarget, false, 0, 0, 0, false), "no target and no filter that lifts by itself");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, true, 0, 0, 0, false), "out of time: the next list resumes");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, false, 1, 0, 0, false), "a route danger deferral is no failure (critic B1.3)");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, false, 0, 1, 0, false), "a suppressed turn-in route counted already");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, false, 0, 0, 1, false),
        "a stale list or a random range skip past an acceptable taker is no failure");

    // TurnInParkCountsRouteDanger on (owner decision): a route danger deferral counts as well,
    // the quest is parked and tried again when the park ends; the other exceptions stay.
    Require(CountsAsNoRoute(RouteOutcome::NoTarget, false, 1, 0, 0, true), "route danger counting on: a deferral counts");
    Require(CountsAsNoRoute(RouteOutcome::NoTarget, false, 0, 0, 0, true), "route danger counting on: no target counts");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, true, 1, 0, 0, true), "route danger counting on: out of time does not count");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, false, 1, 1, 0, true), "route danger counting on: a suppressed route does not count");
    Require(!CountsAsNoRoute(RouteOutcome::NoTarget, false, 1, 0, 1, true), "route danger counting on: unjudged takers do not count");

    // The reject counts of a choice, summed as at the call sites.
    {
        using ai::quest_search::RejectCounts;

        // Review scenario: quest A's taker in the local hub lies behind a death cluster, quest B's
        // reachable taker farther away was skipped by the hub filter. B was never judged, so
        // nothing counts - with the route danger switch on as well.
        RejectCounts hub;
        hub.dangerMap = 1;
        hub.hubFilter = 1;
        Require(!NoRoute(RouteOutcome::NoTarget, hub, false), "hub filter: a taker outside the hub was not judged");
        Require(!NoRoute(RouteOutcome::NoTarget, hub, true), "hub filter: not judged, route danger counting on as well");

        // #416: a choice resumed after a time abort skips the candidates the aborted one checked;
        // their reasons are not known to this choice.
        RejectCounts resumed;
        resumed.resumeSkipped = 40;
        Require(!NoRoute(RouteOutcome::NoTarget, resumed, false), "resume skip: the skipped takers were not judged");
        Require(!NoRoute(RouteOutcome::NoTarget, resumed, true), "resume skip: not judged, route danger counting on as well");

        // A death cluster of the danger map ([QuestFirstRoute] reason=route_danger
        // detail=death_cluster) is a route danger deferral like cross map and zone level.
        RejectCounts cluster;
        cluster.dangerMap = 2;
        Require(!NoRoute(RouteOutcome::NoTarget, cluster, false), "a death cluster deferral is no failure with the switch off");
        Require(NoRoute(RouteOutcome::NoTarget, cluster, true), "a death cluster deferral counts with the switch on");
        RejectCounts deferred;
        deferred.crossMap = 1;
        deferred.zoneLevel = 1;
        deferred.dangerMap = 1;
        Require(!NoRoute(RouteOutcome::NoTarget, deferred, false), "all three route danger deferrals: no failure with the switch off");
        Require(NoRoute(RouteOutcome::NoTarget, deferred, true), "all three route danger deferrals count with the switch on");

        // Takers judged and turned down: no way to any of them.
        RejectCounts judged;
        judged.notActive = 3;
        judged.enemyZone = 1;
        Require(NoRoute(RouteOutcome::NoTarget, judged, false), "takers judged and turned down: no route");
        Require(NoRoute(RouteOutcome::NoTarget, RejectCounts(), false), "an empty list: no route");
    }

    std::cout << "quest_turnin_park_policy_tests passed\n";
    return 0;
}
