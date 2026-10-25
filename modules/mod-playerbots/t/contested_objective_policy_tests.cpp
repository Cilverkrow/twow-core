#include "ContestedObjectivePolicy.h"
#include "DestinationDeathPolicy.h"

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
    using namespace ai::contested_objective;

    // F5b: a GO objective with >= MinBots other roster bots nearby is contested; switch off or no objective never.
    Facts go;
    go.on = true; go.objective = true; go.goObjective = true; go.minBots = 2; go.otherBotsNear = 2;
    Require(Contested(go), "GO objective with 2 bots near (MinBots 2) is contested");
    go.otherBotsNear = 1;
    Require(!Contested(go), "one other bot is below MinBots 2");
    go.otherBotsNear = 5; go.on = false;
    Require(!Contested(go), "switch off for this bot: never");
    go.on = true; go.objective = false;
    Require(!Contested(go), "not a quest objective: never");

    // Kill objectives only with Kill and never in a bot-only group (ad-hoc / S7 share the kill credit there).
    Facts kill;
    kill.on = true; kill.objective = true; kill.goObjective = false; kill.minBots = 2; kill.otherBotsNear = 6;
    Require(!Contested(kill), "kill objective without Kill: as before");
    kill.killToo = true;
    Require(Contested(kill), "kill objective with Kill and a crowd: contested");
    kill.inBotOnlyGroup = true;
    Require(!Contested(kill), "kill objective in a bot-only group: left to the group managers");
    Facts goInGroup = go; goInGroup.objective = true; goInGroup.inBotOnlyGroup = true; goInGroup.otherBotsNear = 3;
    Require(Contested(goInGroup), "a GO objective stays contested in a group (loot is per looter)");

    // MinBots 0 acts like 1.
    Facts zero = go; zero.objective = true; zero.minBots = 0; zero.otherBotsNear = 1;
    Require(Contested(zero), "MinBots 0 acts like 1");

    // F5 diagnostic: the death_suppressed subsets, appended only when there is one.
    SourceCounts none{};
    Require(FormatSources(none).empty(), "no suppression skips: nothing appended");
    SourceCounts some{};
    some[0] = 2; some[1] = 5; some[SourceGather] = 1;
    Require(FormatSources(some) == " death_suppressed_by=death:2,work:5,escape:0,park:0,contested:0,gather:1", "subsets in source order");

    // A death suppression is tagged as a death (the default for every untagged entry, too).
    ai::destination_death::Record record;
    record.source = ai::destination_death::SourceWork;
    record.suppressUntil = 100;
    Require(!ai::destination_death::RecordDeath(record, 200, 2, 1000), "first death after the expiry: counted, not suppressed");
    Require(record.source == ai::destination_death::SourceDeath, "the expired entry starts over as a death record");
    Require(ai::destination_death::RecordDeath(record, 300, 2, 1000) && record.source == ai::destination_death::SourceDeath,
        "the suppressing death tags the entry as a death");

    std::cout << "contested_objective tests passed\n";
    return 0;
}
