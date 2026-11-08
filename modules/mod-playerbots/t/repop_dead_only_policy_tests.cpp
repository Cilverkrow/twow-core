#include "RepopDeadOnlyPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// twow-repo#575 A3 step 4 S4-2 (AiPlayerbot.Rescue.RepopDeadOnly): the pure rules of RepopDeadOnlyPolicy.h - repop only when dead,
// QuestRescue or a reset for an alive bot without a usable hearthstone.
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
    using namespace ai::repop_dead_only;

    // 1. Config loader: an unknown mode is off.
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0-3 pass");
        Require(ClampMode(4) == 0 && ClampMode(-1) == 0 && ClampMode(99) == 0, "an unknown mode acts like 0");
    }

    // 2. The gate: alive bots on their own are blocked; the dead and bots led by a real player keep the repop.
    {
        Require(BlocksRepop(true, false), "an alive bot on its own: no repop");
        Require(!BlocksRepop(false, false), "a dead bot (ghost) repops");
        Require(!BlocksRepop(true, true), "a real player's bot keeps the legacy repop");
        Require(!BlocksRepop(false, true), "dead with a real player: legacy");
    }

    // 3. The alive step: QuestRescue when queued, else a reset (OB-00 07.11. decision 2).
    {
        Require(AliveStep(true) == Step::QuestRescue, "queued rescue: quest_rescue");
        Require(AliveStep(false) == Step::Reset, "per-bot limit: reset");
    }

    // 4. Names for the log lines.
    {
        Require(std::strcmp(SourceName(Source::MoveLong), "move_long") == 0, "move_long name");
        Require(std::strcmp(SourceName(Source::CombatLong), "combat_long") == 0, "combat_long name");
        Require(std::strcmp(SourceName(Source::Count), "unknown") == 0, "out of range source");
        Require(std::strcmp(StepName(Step::QuestRescue), "quest_rescue") == 0, "quest_rescue name");
        Require(std::strcmp(StepName(Step::Reset), "reset") == 0, "reset name");
        Require(std::strcmp(StepName(Step::Count), "unknown") == 0, "out of range step");
    }

    // 5. Counters: per source and step, read and reset; one minute line per minute; out of range ignored.
    {
        Counters counters;
        counters.Count(Source::MoveLong, Step::QuestRescue);
        counters.Count(Source::MoveLong, Step::QuestRescue);
        counters.Count(Source::CombatLong, Step::Reset);
        counters.Count(Source::Count, Step::Reset);
        counters.Count(Source::MoveLong, Step::Count);
        Require(counters.Take(Source::MoveLong, Step::QuestRescue) == 2, "two move_long rescues");
        Require(counters.Take(Source::MoveLong, Step::QuestRescue) == 0, "taken resets");
        Require(counters.Take(Source::MoveLong, Step::Reset) == 0, "no move_long reset");
        Require(counters.Take(Source::CombatLong, Step::Reset) == 1, "one combat_long reset");
        Require(counters.LogDue(1'800'000'020) && !counters.LogDue(1'800'000'040), "one summary per minute");
        Require(counters.LogDue(1'800'000'080), "the next minute logs again");
    }

    std::cout << "repop_dead_only_policy_tests: ok\n";
    return 0;
}
