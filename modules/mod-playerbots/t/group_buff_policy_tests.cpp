#include "GroupBuffPolicy.h"

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
    using namespace ai::group_buff;

    // #420 (a): a bot in a group with a real player buffs its group only.
    Require(!MayBuffOutOfGroup(true), "group bots of a real player buff no strangers");
    Require(MayBuffOutOfGroup(false), "free random bots may still buff strangers");

    // #420 (b): no drink stop while the master walks, unless below medium mana.
    Require(DrinkWhileMasterMoves(39, 40), "below 40 % the bot may drink");
    Require(!DrinkWhileMasterMoves(40, 40), "at 40 % the bot keeps following");
    Require(!DrinkWhileMasterMoves(64, 40), "64 % (old drink trigger) keeps following");

    // #420 (c): [GroupBuff] window.
    BuffWindow window;
    Require(!window.Due(1000), "an empty window is never reported");

    window.Record(1000, 7, false);
    window.Record(1000, 8, true);
    window.Record(1030, 7, false);
    Require(window.casts == 3, "three casts counted");
    Require(window.outOfGroup == 1, "one cast on a stranger");
    Require(window.sameTarget == 1, "target 7 again after 30 s");
    Require(!window.Due(1059), "window still open after 59 s");
    Require(window.Due(1060), "window reported after 60 s");

    window.Reset(1060);
    Require(window.casts == 0 && window.outOfGroup == 0 && window.sameTarget == 0, "reset clears the counters");
    Require(window.lastCastAt.count(8) == 0, "target 8 (60 s ago) is pruned");
    Require(window.lastCastAt.count(7) == 1, "target 7 (30 s ago) is kept");

    window.Record(1070, 7, false);
    Require(window.sameTarget == 1, "repeat across the window border still counts");
    Require(window.start == 1070, "a new window starts at its first cast");
    window.Record(1200, 7, false);
    Require(window.sameTarget == 1, "after 60 s it is no repeat");

    std::cout << "group_buff_policy_tests passed\n";
    return 0;
}
