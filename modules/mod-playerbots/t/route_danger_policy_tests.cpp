#include "RouteDangerPolicy.h"

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
    using namespace ai::route_danger;

    // Live: level 2-4 bots in Durotar sent to a turn-in in Undercity (other continent).
    Require(Classify(true, 3, 10, 0) == Reason::CrossMap, "level 3 defers a cross-continent turn-in");
    Require(Classify(true, 10, 10, 0) == Reason::None, "from the minimum level the continent switch is allowed");
    Require(Classify(true, 3, 0, 0) == Reason::None, "0 disables the cross-map rule");

    Require(Classify(false, 5, 10, 20) == Reason::TargetZoneLevel, "same-continent target in a level 20 zone waits");
    Require(Classify(false, 15, 10, 20) == Reason::None, "within the +5 margin the zone is fine");
    Require(Classify(false, 1, 10, 0) == Reason::None, "unknown zone level (custom zones) is never blocked");
    Require(Classify(false, 3, 10, 5) == Reason::None, "neighbouring starter zone stays open");

    // twow-repo#485: with CrossMapContinentsOnly only a route to the other
    // continent is deferred. The Deeprun Tram counts as the Eastern Kingdoms and
    // an instance as the continent of its entrance, so a profile minimum of 61
    // keeps routes on one continent open (v24: 17 choices tram -> Eastern
    // Kingdoms) and still defers tram -> Kalimdor and back (v24: 15 choices
    // Kalimdor -> tram).
    Require(ContinentOf(0, -1) == 0 && ContinentOf(1, -1) == 1, "a continent is itself");
    Require(ContinentOf(369, -1) == 0, "the Deeprun Tram is the Eastern Kingdoms");
    Require(ContinentOf(389, 1) == 1 && ContinentOf(36, 0) == 0, "an instance is the continent of its entrance");
    Require(ContinentOf(489, -1) == 489, "a battleground stays its own map");
    Require(IsContinentSwitch(1, 0) && IsContinentSwitch(0, 1), "Kalimdor <-> Eastern Kingdoms is a continent switch");
    Require(!IsContinentSwitch(0, 0) && !IsContinentSwitch(1, 1), "the same continent is no switch");
    Require(!IsContinentSwitch(489, 0) && !IsContinentSwitch(1, 489), "a map that is no continent is never a switch");
    Require(IsContinentSwitch(ContinentOf(369, -1), ContinentOf(1, -1)), "tram -> Kalimdor is a continent switch");
    Require(IsContinentSwitch(ContinentOf(1, -1), ContinentOf(369, -1)), "Kalimdor -> tram is a continent switch");
    Require(!IsContinentSwitch(ContinentOf(369, -1), ContinentOf(0, -1)), "tram -> Eastern Kingdoms stays on one continent");
    Require(IsContinentSwitch(ContinentOf(389, 1), ContinentOf(0, -1)), "Ragefire Chasm -> Eastern Kingdoms is a continent switch");
    Require(!IsContinentSwitch(ContinentOf(1, -1), ContinentOf(389, 1)), "Kalimdor -> Ragefire Chasm stays on one continent");
    Require(IsContinentSwitch(ContinentOf(1, -1), ContinentOf(36, 0)), "Kalimdor -> the Deadmines is a continent switch");
    Require(Classify(true, 30, 61, 0, 5, false, false) == Reason::CrossMap, "0 keeps every other map deferred (old behaviour)");
    Require(Classify(true, 30, 61, 0, 5, true, false) == Reason::None, "continents only: a route on one continent stays open");
    Require(Classify(true, 30, 61, 0, 5, true, true) == Reason::CrossMap, "continents only: a continent switch is still deferred");
    Require(Classify(true, 61, 61, 0, 5, true, true) == Reason::None, "continents only: from the minimum level the switch is allowed");
    Require(Classify(true, 5, 61, 20, 5, true, false) == Reason::TargetZoneLevel, "continents only: the zone level rule still applies");
    return 0;
}
