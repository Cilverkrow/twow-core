#include "ZoneEscapePolicy.h"

#include <cstdlib>
#include <cstring>
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

ai::zone_escape::Facts Stranded()
{
    // Live, train 4: level 11 bot in Eastern Plaguelands (area level 58).
    ai::zone_escape::Facts facts;
    facts.enabled = true;
    facts.rosterOnItsOwn = true;
    facts.alive = true;
    facts.due = true;
    facts.hearthUsable = true;
    facts.areaLevel = 58;
    facts.botLevel = 11;
    return facts;
}
}

int main()
{
    using namespace ai::zone_escape;

    Decision d = Decide(Stranded());
    Require(d.step == Step::Hearth && !std::strcmp(d.reason, "zone_above_level"), "stranded bot hearths home");

    Facts noHearth = Stranded();
    noHearth.hearthUsable = false;
    d = Decide(noHearth);
    Require(d.step == Step::Travel && !std::strcmp(d.reason, "no_hearthstone"), "without hearthstone: new travel target");

    Facts cooldown = Stranded();
    cooldown.due = false;
    Require(Decide(cooldown).step == Step::None, "cooldown stops loops");

    Facts fine = Stranded();
    fine.areaLevel = 16;
    Require(Decide(fine).step == Step::None, "area at most 5 above: stay");
    fine.areaLevel = 0;
    Require(Decide(fine).step == Step::None, "unknown area level is never too high");

    Facts off = Stranded();
    off.enabled = false;
    Require(Decide(off).step == Step::None, "switch off");
    Facts led = Stranded();
    led.rosterOnItsOwn = false;
    Require(Decide(led).step == Step::None, "bots led by a player or non-roster bots stay");
    Facts dead = Stranded();
    dead.alive = false;
    Require(Decide(dead).step == Step::None, "dead bots are handled by the corpse run");
    Facts city = Stranded();
    city.inRestArea = true;
    d = Decide(city);
    Require(d.step == Step::None && !std::strcmp(d.reason, "rest_area"), "cities and inns are never left (OB-40: L1 bots in Stormwind)");
    Facts home = Stranded();
    home.inHomeZone = true;
    d = Decide(home);
    Require(d.step == Step::None && !std::strcmp(d.reason, "home_zone"), "never inside the home-bind zone (start zone)");
    Facts dungeon = Stranded();
    dungeon.inInstanceOrBattleground = true;
    Require(Decide(dungeon).step == Step::None, "never inside instances or battlegrounds");
    return 0;
}
