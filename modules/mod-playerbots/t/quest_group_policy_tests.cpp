#include "QuestGroupPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
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

ai::quest_groups::Candidate C(std::uint32_t guid, std::uint32_t level, std::uint32_t open, float x, std::uint32_t guild = 0)
{
    ai::quest_groups::Candidate c;
    c.guid = guid;
    c.level = level;
    c.openObjectives = open;
    c.x = x;
    c.guild = guild;
    return c;
}
}

int main()
{
    using namespace ai::quest_groups;

    // Switch and cohorts: 0 off, 1 all, 2 B (odd), 3 A (even), anything else off.
    Require(!Applies(0, 7) && !Applies(0, 8), "0 = off");
    Require(Applies(1, 7) && Applies(1, 8), "1 = all");
    Require(Applies(2, 7) && !Applies(2, 8), "2 = cohort B (odd)");
    Require(!Applies(3, 7) && Applies(3, 8), "3 = cohort A (even)");
    Require(!Applies(4, 7) && !Applies(4, 8), "unknown = off");
    Require(Cohort(7) == 'B' && Cohort(8) == 'A', "cohort letter");

    // Quests: elite or (switch) suggested players, never dungeon/raid/pvp.
    Require(IsGroupHelpQuest(1, 0, true) && IsGroupHelpQuest(1, 0, false), "elite");
    Require(IsGroupHelpQuest(0, 3, true) && !IsGroupHelpQuest(0, 3, false), "suggested players only with the switch");
    Require(!IsGroupHelpQuest(0, 1, true), "a normal quest");
    Require(!IsGroupHelpQuest(81, 5, true) && !IsGroupHelpQuest(62, 10, true) && !IsGroupHelpQuest(41, 2, true), "dungeon/raid/pvp out");

    // Waiting: open, own objective open, not red, not abandoned, no class quest.
    QuestFacts f;
    f.incomplete = true; f.questLevel = 24; f.botLevel = 22; f.openObjectives = 2;
    Require(Waits(f), "open elite quest two levels above waits");
    f.questLevel = 27; Require(!Waits(f), "red quest (>= L+5) does not wait");
    f.questLevel = 24; f.openObjectives = 0; Require(!Waits(f), "own objectives done: nothing to wait for");
    f.openObjectives = 1; f.abandoned = true; Require(!Waits(f), "S1-abandoned quest never");
    f.abandoned = false; f.classQuest = true; Require(!Waits(f), "class quest never");

    // Forming: leader = most open objectives, radius, level window, guild first, max size, >= 2.
    FormLimits lim;
    lim.maxSize = 3; lim.levelWindow = 3; lim.recruitRadius = 100.0f;
    std::vector<Candidate> pool{ C(10, 20, 1, 0), C(11, 21, 3, 10), C(12, 22, 1, 20), C(13, 26, 1, 5), C(14, 20, 1, 500) };
    auto groups = Form(pool, lim);
    Require(groups.size() == 1, "one group (13 outside the window, 14 outside the radius)");
    Require(groups[0][0].guid == 11, "leader = most open objectives");
    Require(groups[0].size() == 3, "max size 3");
    for (auto const& c : groups[0])
        Require(c.guid != 13 && c.guid != 14, "window and radius respected");

    std::vector<Candidate> guildPool{ C(20, 20, 2, 0, 7), C(21, 20, 1, 5), C(22, 20, 1, 50, 7) };
    lim.maxSize = 2;
    groups = Form(guildPool, lim);
    Require(groups.size() == 1 && groups[0][1].guid == 22, "the guild mate first, before a nearer stranger");

    Require(Form({ C(30, 20, 1, 0) }, lim).empty(), "a single bot forms no group");
    lim.maxSize = 9;
    groups = Form({ C(40, 20, 1, 0), C(41, 20, 1, 1), C(42, 20, 1, 2), C(43, 20, 1, 3), C(44, 20, 1, 4), C(45, 20, 1, 5), C(46, 20, 1, 6) }, lim);
    Require(groups.size() == 2 && groups[0].size() == 5 && groups[1].size() == 2, "max size clamped to 5; the other two form their own group");

    // Life cycle.
    Timers t;
    GroupFacts g;
    g.members = 3; g.now = 1000; g.formedAt = 1000; g.phaseSince = 1000; g.lastProgress = 1000;
    Require(Decide(g, t).dissolve == Dissolve::None && Decide(g, t).next == Phase::Rendezvous, "rendezvous waits");
    g.gathered = true; Require(Decide(g, t).next == Phase::Goal, "gathered -> goal");
    g.gathered = false; g.now = 1000 + 10 * 60; Require(Decide(g, t).dissolve == Dissolve::RendezvousTimeout, "rendezvous timeout");

    g.phase = Phase::Goal; g.now = 2000; g.formedAt = 1000; g.phaseSince = 1500; g.lastProgress = 2000 - 14 * 60;
    Require(Decide(g, t).dissolve == Dissolve::None, "14 min without progress: still on");
    g.lastProgress = 2000 - 15 * 60; Require(Decide(g, t).dissolve == Dissolve::NoProgress, "15 min without progress: dissolve");
    g.lastProgress = 2000; g.allComplete = true; Require(Decide(g, t).next == Phase::TurnIn, "all complete -> turn in");

    g.phase = Phase::TurnIn; g.phaseSince = 2000; g.now = 2000 + 4 * 60; g.allComplete = true;
    Require(Decide(g, t).dissolve == Dissolve::None, "turn-in phase waits");
    g.now = 2000 + 5 * 60; Require(Decide(g, t).dissolve == Dissolve::TurnInWait, "turn-in wait");
    g.allTurnedIn = true; Require(Decide(g, t).dissolve == Dissolve::TurnedIn, "all turned in: the normal end");

    GroupFacts h;
    h.members = 3; h.phase = Phase::Goal; h.now = 1000 + 60 * 60; h.formedAt = 1000; h.phaseSince = 1000; h.lastProgress = h.now;
    Require(Decide(h, t).dissolve == Dissolve::MaxTime, "max quest time");
    h.now = 1100; h.realPlayer = true; Require(Decide(h, t).dissolve == Dissolve::RealPlayer, "a real player ends it first");
    h.realPlayer = false; h.members = 1; Require(Decide(h, t).dissolve == Dissolve::TooSmall, "one member left");
    h.members = 2; h.leaderStuck = true; Require(Decide(h, t).dissolve == Dissolve::LeaderStuck, "PosStuck moved the leader");
    Require(std::string(DissolveName(Dissolve::NoProgress)) == "no_progress", "reason names");

    // Queue store: age, attempts, cooldown, stale entries.
    QueueStore q;
    q.Seen(7, 100, 1000);
    q.Seen(7, 100, 1600);
    Require(q.QueuedAge(7, 100, 1600) == 600, "age from the first pass");
    q.Seen(7, 200, 1500);
    Require(q.OldestQueuedAge(7, 1600) == 600, "oldest wait over the bot's quests");
    Require(q.OldestQueuedAge(8, 1600) == 0, "another bot waits on nothing");
    q.Failed(7, 100, 1600, 1800);
    Require(q.Attempts(7, 100) == 1 && q.Blocked(7, 100, 1700) && !q.Blocked(7, 100, 3400), "attempt and cooldown");
    q.ForgetStale(5000, 600);
    Require(q.Size() == 0, "stale entries go");

    // The leader's approach (PosStuck does not see forced targets): 5 yd closer resets, 10 min without = stuck.
    ApproachWatch w;
    Require(!w.Observe(1, 500.0f, true, 1000), "first look starts the window");
    Require(!w.Observe(1, 497.0f, true, 1300), "3 yd closer: no reset, not yet stuck");
    Require(w.Observe(1, 497.0f, true, 1600), "10 min without 5 yd: stuck");
    Require(!w.Observe(1, 490.0f, true, 1700), "5 yd closer again: fresh window");
    Require(!w.Observe(1, 490.0f, true, 2299), "within the new window");
    Require(!w.Observe(1, 490.0f, false, 2400), "arrived (not travelling): fresh window");
    Require(!w.Observe(2, 900.0f, true, 2500) && !w.Observe(2, 900.0f, true, 3099), "new target: fresh window");
    Require(w.Observe(2, 899.0f, true, 3100), "new target stalls too");

    std::cout << "quest_group_policy_tests passed\n";
    return 0;
}
