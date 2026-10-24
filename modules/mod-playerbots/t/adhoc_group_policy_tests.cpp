#include "AdhocGroupPolicy.h"
#include "GroupAiRefresh.h"
#include "ScanSlicePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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
    using namespace ai::adhoc_group;

    // Who invites (design 3.1 point 4).
    Require(Decide(10, false, false, 20, false, false) == Invite::AInvitesB, "two ungrouped bots: the lower GUID invites");
    Require(Decide(30, false, false, 20, false, false) == Invite::BInvitesA, "two ungrouped bots: the lower GUID invites (other side)");
    Require(Decide(30, true, true, 20, false, false) == Invite::AInvitesB, "the ad-hoc leader invites");
    Require(Decide(10, false, false, 20, true, true) == Invite::BInvitesA, "a bot asks to join the neighbour's ad-hoc group");
    Require(Decide(10, true, false, 20, false, false) == Invite::None, "a member that does not lead never invites");
    Require(Decide(10, false, false, 20, true, false) == Invite::None, "a bot in a group it does not lead is no candidate");
    Require(Decide(10, true, true, 20, true, true) == Invite::None, "two ad-hoc groups are not merged");

    Require(LevelWindowOk(5, 8, 3) && !LevelWindowOk(5, 9, 3), "level window is max - min <= window");

    // Leave rules (design 3.2).
    LeaveFacts f;
    f.now = 1000;
    f.lastProgress = 990;
    Require(DecideLeave(f) == Leave::None, "working on the objective: stay");

    LeaveFacts done = f;
    done.objectiveDone = true;
    Require(DecideLeave(done) == Leave::ObjectiveDone, "own objective done: leave");

    LeaveFacts turnedIn = f;
    turnedIn.questInProgress = false;
    Require(DecideLeave(turnedIn) == Leave::QuestTurnedIn, "quest gone from the log: leave");

    LeaveFacts instance = f;
    instance.inInstance = true;
    Require(DecideLeave(instance) == Leave::Instance, "instance: leave");

    LeaveFacts far = f;
    far.outOfRangeSince = 1000 - OutOfRangeSeconds + 1;
    Require(DecideLeave(far) == Leave::None, "briefly out of range: stay");
    far.outOfRangeSince = 1000 - OutOfRangeSeconds;
    Require(DecideLeave(far) == Leave::OutOfRange, "out of range for 60 s: leave");

    LeaveFacts levels = f;
    levels.levelWindowSince = 1000 - LevelWindowSeconds;
    Require(DecideLeave(levels) == Leave::LevelWindow, "level window left for 5 min: leave");

    LeaveFacts idle = f;
    idle.lastProgress = 1000 - IdleSeconds;
    Require(DecideLeave(idle) == Leave::Idle, "no progress for 10 min: leave");

    Require(std::string(LeaveName(Leave::ObjectiveDone)) == "objective_done", "reason names for the log");

    // Pair cooldown: symmetric, time bound, bounded size.
    PairCooldownStore store;
    store.Block(7, 3, 700, 100);
    Require(store.IsBlocked(3, 7, 699) && store.IsBlocked(7, 3, 699), "the pair is blocked both ways");
    Require(!store.IsBlocked(3, 7, 700), "the cooldown ends");
    Require(!store.IsBlocked(3, 8, 100), "other pairs are free");

    PairCooldownStore full;
    for (std::uint32_t i = 0; i < PairCooldownStore::MaxPairs + 10; ++i)
        full.Block(i, i + 100000, 50, 100);  // all already expired at now = 100
    Require(full.Size() <= PairCooldownStore::MaxPairs, "the pair store stays bounded");

    // Concurrent access from several map threads.
    PairCooldownStore shared;
    std::vector<std::thread> threads;
    for (std::uint32_t t = 0; t < 4; ++t)
        threads.emplace_back([&shared, t]() {
            for (std::uint32_t i = 0; i < 20000; ++i)
            {
                shared.Block(t * 100000 + i, i, 1000000, i / 10);
                shared.IsBlocked(i, t * 100000 + i, i / 10);
            }
        });
    for (std::thread& thread : threads)
        thread.join();
    Require(shared.Size() <= PairCooldownStore::MaxPairs, "bounded under concurrent use");

    // Registry: which groups are ad-hoc, for which objective.
    Registry registry;
    ObjectiveKey key;
    key.questId = 4402;
    key.objective = 0;
    registry.Register(55, key, 100);
    Registry::Entry entry;
    Require(registry.Find(55, entry) && entry.key == key && entry.created == 100, "registered group found");
    Require(!registry.Find(56, entry), "other groups are not ad-hoc");
    registry.Forget(55);
    Require(!registry.Find(55, entry), "a forgotten group is ordinary again");

    // #628: entries of groups that no longer exist go, the others stay.
    registry.Register(60, key, 100);
    registry.Register(61, key, 100);
    registry.Register(62, key, 100);
    Require(registry.ForgetGone([](std::uint32_t id) { return id != 61; }) == 1, "one gone group forgotten");
    Require(registry.Find(60, entry) && !registry.Find(61, entry) && registry.Find(62, entry), "only the gone group left the registry");
    Require(registry.ForgetGone([](std::uint32_t) { return true; }) == 0, "nothing gone, nothing forgotten");

    // twow-repo#628 fix (GroupAiRefresh.h): the AI part of a world-thread group change, posted to the bot.
    {
        using namespace ai::group_ai_refresh;
        Require(IsWorldManagerSource("adhoc group") && IsWorldManagerSource("quest group"), "the two managers' accept sources");
        Require(!IsWorldManagerSource("group invite") && !IsWorldManagerSource(""), "any other accept runs inline");

        Require(Merge(0, JoinReset) == JoinReset, "a join alone");
        Require(Merge(JoinReset, Follower) == (JoinReset | Follower), "S7: join, then the follower set");
        Require(Merge(JoinReset | Follower, LeaveReset) == LeaveReset, "a leave drops the pending join and the set");
        Require(Merge(LeaveReset, JoinReset) == JoinReset, "a join drops the pending leave");
        Require(Merge(JoinReset | Follower, TurnIn) == (JoinReset | TurnIn), "turn-in replaces the follower set");
        Require(Merge(TurnIn, Follower) == Follower, "the sets exclude each other");
        Require(Merge(Follower, Follower) == Follower, "posting again changes nothing");

        Plan p = PlanFor(JoinReset | Follower);
        Require(p.reset && p.joinStrategies && p.follower && !p.turnIn, "join plan: reset with the accept strategies, then follower");
        p = PlanFor(LeaveReset);
        Require(p.reset && !p.joinStrategies && !p.follower && !p.turnIn, "leave plan: reset only");
        p = PlanFor(TurnIn);
        Require(!p.reset && p.turnIn && !p.follower, "turn-in plan: the set only");
        p = PlanFor(Follower | TurnIn);
        Require(p.turnIn && !p.follower, "never both sets");
        p = PlanFor(0);
        Require(!p.reset && !p.follower && !p.turnIn, "nothing posted, nothing done");
    }

    // twow-repo#628 ScanSlices (ScanSlicePolicy.h): a pass over all bots spread over several manager updates.
    {
        using ai::scan_slice::SliceEnd;
        Require(SliceEnd(540, 0, 0) == 540 && SliceEnd(540, 0, 1) == 540, "0 or 1 slices: all bots in one update");
        Require(SliceEnd(540, 0, 10) == 54 && SliceEnd(540, 486, 10) == 540, "10 slices of 540: 54 per update, the last ends at 540");
        std::size_t next = 0;
        std::uint32_t updates = 0;
        while (next < 541)
        {
            next = SliceEnd(541, next, 10);
            ++updates;
        }
        Require(updates == 10, "541 bots in 10 slices: still 10 updates (ceil)");
        Require(SliceEnd(3, 0, 10) == 1 && SliceEnd(3, 2, 10) == 3, "more slices than bots: one bot per update");
        Require(SliceEnd(0, 0, 10) == 0, "no bots: nothing to do, the pass ends at once");
        Require(SliceEnd(540, 540, 10) == 540, "a finished pass stays at its end");
    }

    std::cout << "adhoc_group_policy_tests passed\n";
    return 0;
}
