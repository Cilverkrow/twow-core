#include "GatheringTracePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
    using namespace ai::gathering_trace;

    // Spell kinds: the five gathering casts of OpenLootAction, nothing else.
    Require(OpenKindOfSpell(2366) == OpenKind::Herb, "Herb Gathering is a herb open");
    Require(OpenKindOfSpell(32605) == OpenKind::Herb, "the creature herb cast is a herb open");
    Require(OpenKindOfSpell(2575) == OpenKind::Ore, "Mining is an ore open");
    Require(OpenKindOfSpell(32606) == OpenKind::Ore, "the creature ore cast is an ore open");
    Require(OpenKindOfSpell(8613) == OpenKind::Skin, "Skinning is a skin open");
    Require(OpenKindOfSpell(1804) == OpenKind::None, "lockpicking is no gathering open");
    Require(OpenKindOfSpell(49383) == OpenKind::None, "the engineering salvage is no gathering open");
    Require(OpenKindOfSpell(0) == OpenKind::None, "no spell");

    // Overlap rule: a trip outranks a purpose.
    Require(ActivityOf(false, false) == Activity::None, "nothing");
    Require(ActivityOf(true, false) == Activity::Purpose, "purpose only");
    Require(ActivityOf(false, true) == Activity::Trip, "trip only");
    Require(ActivityOf(true, true) == Activity::Trip, "both: the trip counts, the purpose not");

    // Before the first sample nothing counts; the first sample opens the window.
    Window w;
    Require(!w.Open(), "no window before the first sample");
    w.NoteOpen(OpenKind::Herb, 5000);
    w.NoteCraft(3000);
    Require(w.opensHerb == 0 && w.crafts == 0 && w.castMs == 0, "notes before the window are dropped");
    Require(!w.Sample(1000, false, false), "the first sample opens, no line");
    Require(w.Open() && w.start == 1000 && w.PlaySeconds() == 0, "window at 1000");

    // Left-point accounting: each interval goes to the activity seen at its start.
    Require(!w.Sample(1005, true, false), "purpose starts at 1005");
    Require(w.purposeSeconds == 0 && w.tripSeconds == 0, "1000-1005 had nothing");
    Require(!w.Sample(1010, true, true), "trip starts while the purpose runs");
    Require(w.purposeSeconds == 5, "1005-1010 purpose");
    Require(!w.Sample(1020, true, true), "both still active");
    Require(w.tripSeconds == 10 && w.purposeSeconds == 5, "1010-1020 trip only, no double count");
    Require(!w.Sample(1030, true, false), "trip ends");
    Require(w.tripSeconds == 20 && w.purposeSeconds == 5, "1020-1030 trip");
    Require(!w.Sample(1100, false, false), "purpose ends");
    Require(w.purposeSeconds == 75, "1030-1100 purpose");
    Require(w.purposeSeconds + w.tripSeconds <= w.PlaySeconds(), "purpose_s + trip_s <= play_s");

    // A clock that went back counts as no time and never underflows.
    Require(!w.Sample(1090, false, false), "clock back");
    Require(w.lastSample == 1100 && w.PlaySeconds() == 100, "clock back ignored");

    // Casts: counts and cast time per kind, rounded to whole seconds.
    w.NoteOpen(OpenKind::Herb, 5000);
    w.NoteOpen(OpenKind::Ore, 5000);
    w.NoteOpen(OpenKind::Ore, 5000);
    w.NoteOpen(OpenKind::Skin, 1700);
    w.NoteOpen(OpenKind::None, 9000);
    w.NoteCraft(3000);
    Require(w.opensHerb == 1 && w.opensOre == 2 && w.opensSkin == 1 && w.crafts == 1, "counts");
    Require(w.castMs == 19700 && w.CastSeconds() == 20, "cast ms summed, None ignored, rounded half up");

    // Hour rollover: due at the first sample with WindowSeconds passed; play_s is the whole window.
    Require(!w.Sample(1000 + WindowSeconds - 1, true, false), "one second short");
    Require(w.Sample(1000 + WindowSeconds + 4, true, false), "due at the first sample after the hour");
    Require(w.PlaySeconds() == WindowSeconds + 4, "play_s = window length");
    Require(w.purposeSeconds == 75 + 5, "the purpose seen at the sample one second short counts to the line");

    Skill herb, mining, skinning;
    herb.value = 55;
    herb.max = 75;
    mining.value = 1;
    mining.max = 75;
    std::string const line = ShareLine(42, 17, w, herb, mining, skinning);
    Require(line == "[Gathering] event=share bot=42 level=17 play_s=3604 purpose_s=80 trip_s=20 cast_s=20 opens_herb=1 "
        "opens_ore=2 opens_skin=1 crafts=1 s182=55/75 s186=1/75 s393=0/0", "line format and field order");

    // After the line: counters are increments, the next window starts at the last sample with its activity.
    w.Roll();
    Require(w.Open() && w.start == 1000 + WindowSeconds + 4 && w.PlaySeconds() == 0, "new window at the line's sample");
    Require(w.purposeSeconds == 0 && w.tripSeconds == 0 && w.castMs == 0 && w.opensHerb == 0 && w.opensOre == 0 &&
        w.opensSkin == 0 && w.crafts == 0, "counters back to 0");
    Require(w.lastActivity == Activity::Purpose, "the running purpose carries over");
    Require(!w.Sample(w.start + 10, false, false), "10 s later");
    Require(w.purposeSeconds == 10, "the carried purpose counts in the new window");

    // Out of scope: everything goes.
    w.Reset();
    Require(!w.Open() && w.purposeSeconds == 0, "reset");
    return 0;
}
