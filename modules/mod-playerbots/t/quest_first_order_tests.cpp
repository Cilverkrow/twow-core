// twow-repo#601 decision order (AiPlayerbot.Grind.QuestFirstOrder, owner 20.10.2026): QuestFirstOrderPolicy.h.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "QuestFirstOrderPolicy.h"

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
    using namespace ai::quest_first_order;

    // Population: switch, quest-first roster bot, on its own, below the max level.
    Require(Applies(true, true, false, 1, 60), "L1 roster bot on its own: applies");
    Require(!Applies(false, true, false, 1, 60), "switch 0: never");
    Require(!Applies(true, false, false, 1, 60), "no quest-first roster bot: never");
    Require(!Applies(true, true, true, 1, 60), "a real player's bot: never");
    Require(!Applies(true, true, false, 60, 60), "max level: never");

    // Off: every candidate as before (also the L1-c wolf that is no quest mob and not in the way).
    Require(TargetAllowed(false, false, false), "off: plain grind candidate stays");
    // On: the L1-c case - a plain wolf off the path to the quest giver is skipped.
    Require(!TargetAllowed(true, false, false), "on: plain grind candidate skipped");
    Require(TargetAllowed(true, true, false), "on: quest objective stays");
    Require(TargetAllowed(true, false, true), "on: creature in the way stays");

    // In the way: hostile non-player, in front, inside the aggro distance, below bot level + 3.
    Require(BlocksPath(false, true, true, true, 3, 1), "L3 wolf ahead inside aggro range of an L1 bot: in the way");
    Require(!BlocksPath(false, true, false, true, 3, 1), "ahead but out of aggro range (isInFront ignores distance): not in the way");
    Require(!BlocksPath(false, false, true, true, 3, 1), "behind the bot: not in the way");
    Require(!BlocksPath(false, true, true, false, 3, 1), "not hostile: not in the way");
    Require(!BlocksPath(false, true, true, true, 4, 1), "bot level + 3: not in the way");
    Require(!BlocksPath(true, true, true, true, 1, 1), "a player: never");

    // Events and the minute line.
    Require(PickEvent(true) == Event::Objective && PickEvent(false) == Event::Blocker, "pick events");
    Require(EmptyEvent(true) == Event::QuestWork && EmptyEvent(false) == Event::NoQuest, "empty events");
    std::array<std::uint32_t, EventCount> counts = {7, 2, 1, 3, 4, 5};
    Require(FormatMinuteLine(counts) == "[QuestOrder] minute skipped=7 objective=2 blocker=1 quest_work=3 no_quest=4 grind_refused=5", "minute line");

    // Per-bot episode lines (OB-00 20.10.): kinds, band, end line.
    Require(EpisodeBit(Event::NoQuest) == 1 && EpisodeBit(Event::GrindRefused) == 2 && EpisodeBit(Event::Skipped) == 0 &&
        EpisodeBit(Event::QuestWork) == 0, "episode bits: only no_quest and grind_refused");
    Require(BandOf(1) == 0 && BandOf(5) == 1 && BandOf(14) == 2 && BandOf(59) == 11 && BandOf(60) == 12, "bands as [BotActivity]");
    Require(FormatEpisodeLine(Event::NoQuest, 5, 3, 12, 0) == "[QuestOrder] state=no_quest bot=5 level=3 band=0 zone=12 map=0",
        "no_quest line");
    Require(FormatEpisodeLine(Event::GrindRefused, 7, 11, 40, 0) ==
        "[QuestOrder] state=grind_refused bot=7 level=11 band=2 zone=40 map=0", "grind_refused line");
    Require(FormatEpisodeEndLine(5, 4, 12, 600, 3) ==
        "[QuestOrder] state=quest_found bot=5 level=4 band=0 zone=12 seconds=600 had=no_quest,grind_refused", "end line, both kinds");
    Require(FormatEpisodeEndLine(5, 4, 12, 60, 2) ==
        "[QuestOrder] state=quest_found bot=5 level=4 band=0 zone=12 seconds=60 had=grind_refused", "end line, one kind");

    std::cout << "quest_first_order_tests passed\n";
    return 0;
}
