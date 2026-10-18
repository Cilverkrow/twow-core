// twow-repo#541: shaman weapon imbue per spec - the owner's table (09.10.2026) as learned-spell sets.
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "ShamanImbuePolicy.h"
#include "SpecPathPolicy.h"

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

using namespace ai::shaman_imbue;

// What a shaman has learned at a level (trainer levels: Rockbiter 1, Flametongue 10, Frostbrand 20, Windfury 30).
std::set<std::string> LearnedAt(int level)
{
    std::set<std::string> learned = { Rockbiter };
    if (level >= 10)
        learned.insert(Flametongue);
    if (level >= 20)
        learned.insert(Frostbrand);
    if (level >= 30)
        learned.insert(Windfury);
    return learned;
}

std::string At(Spec spec, int level, bool inGroup)
{
    std::set<std::string> const learned = LearnedAt(level);
    return Choose(spec, inGroup, [&learned](char const* name) { return learned.count(name) != 0; });
}
}

int main()
{
    for (bool group : { false, true })
    {
        for (int level : { 1, 9, 10, 19, 20, 29, 30, 45, 60 })
            Require(At(Spec::Tank, level, group) == Rockbiter, "tank: Rockbiter at every level");

        for (Spec spec : { Spec::Elemental, Spec::Restoration, Spec::Enhancement })
        {
            Require(At(spec, 1, group) == Rockbiter, "1-9: Rockbiter for every spec");
            Require(At(spec, 9, group) == Rockbiter, "1-9: Rockbiter for every spec");
            Require(At(spec, 10, group) == Flametongue, "10-19: Flametongue");
            Require(At(spec, 19, group) == Flametongue, "10-19: Flametongue");
        }

        for (Spec spec : { Spec::Elemental, Spec::Restoration })
            for (int level : { 20, 29, 30, 60 })
                Require(At(spec, level, group) == Flametongue, "elemental/restoration: Flametongue from 10 on");

        for (int level : { 30, 45, 60 })
            Require(At(Spec::Enhancement, level, group) == Windfury, "enhancement 30-60: Windfury, solo and in a group");
    }

    Require(At(Spec::Enhancement, 20, false) == Frostbrand, "enhancement 20-29 solo: Frostbrand");
    Require(At(Spec::Enhancement, 29, false) == Frostbrand, "enhancement 20-29 solo: Frostbrand");
    Require(At(Spec::Enhancement, 20, true) == Flametongue, "enhancement 20-29 in a group: Flametongue");
    Require(At(Spec::Enhancement, 29, true) == Flametongue, "enhancement 20-29 in a group: Flametongue");

    // Not trained yet: the spell before it.
    Require(Choose(Spec::Enhancement, false, [](char const* name) { return std::string(name) == Rockbiter; }) == Rockbiter,
        "a level-35 enhancement shaman that only knows Rockbiter keeps Rockbiter");
    Require(Choose(Spec::Elemental, true, [](char const*) { return false; }).empty(), "nothing learned: no imbue");

    Require(SpecFor(true, 1) == Spec::Tank, "tank strategy wins over the talent tab");
    Require(SpecFor(false, 0) == Spec::Elemental && SpecFor(false, 1) == Spec::Enhancement && SpecFor(false, 2) == Spec::Restoration,
        "talent tabs as in AiFactory");
    Require(SpecFor(false, -1) == Spec::Enhancement, "no talents: enhancement, as AiFactory's else branch");

    // twow-repo#602 R06a (AiPlayerbot.Rotation.SpecFromPath): the shaman tank path names tree 1 (enhancement), so the
    // tab handed to SpecFor stays 1 and the imbue is the one it had before: Rockbiter with the tank strategy, as
    // enhancement without it (Flametongue from 10, Windfury from 30).
    {
        int const tankPathTab = 1;   // AiPlayerbot.PremadeSpecTab.7.3 (EXPECTED_TREE_PAGE "shaman tank")
        int const tab = ai::spec_path::PathTabFor(true, 15, tankPathTab);
        Require(tab == 1, "R06a: the shaman tank path keeps tab 1");
        Require(SpecFor(true, tab) == Spec::Tank && At(SpecFor(true, tab), 15, false) == Rockbiter, "R06a: shaman tank: Rockbiter");
        Require(SpecFor(false, tab) == Spec::Enhancement && At(SpecFor(false, tab), 15, false) == Flametongue &&
            At(SpecFor(false, tab), 30, true) == Windfury, "R06a: tank path without the tank strategy: imbue as enhancement");
        Require(ai::spec_path::PathTabFor(true, 9, tankPathTab) == -1, "R06a: below level 10 the majority (class default) stays");
    }

    Require(std::string(TriggerAction(false, "windfury weapon")) == "windfury weapon", "switch off: the strategy's old action");
    Require(std::string(TriggerAction(true, "windfury weapon")) == ActionName, "switch on: the per-spec action");

    std::cout << "shaman_imbue_policy_tests passed\n";
    return 0;
}
