#include "ProfessionGrantPolicy.h"

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
    using namespace ai::profession_grant;
    namespace p = ai::profession;

    // Mode: 0 off, 1 all, 2 cohort B (odd), 3 cohort A (even), anything else off.
    Require(!Applies(0, 7) && !Applies(0, 8), "mode 0 is off");
    Require(Applies(1, 7) && Applies(1, 8), "mode 1 applies to every bot");
    Require(Applies(2, 7) && !Applies(2, 8), "mode 2 = cohort B (odd low guid)");
    Require(!Applies(3, 7) && Applies(3, 8), "mode 3 = cohort A (even low guid)");
    Require(!Applies(4, 7) && !Applies(4, 8) && ClampMode(9) == 0, "an unknown mode is off");

    // Starter tools (owner economy decision 28.11.2026 via OB-00; OB-40 54e8be01).
    Require(StarterTools(p::kMining)[0] == 2901 && StarterTools(p::kMining)[1] == 0, "mining: Mining Pick");
    Require(StarterTools(p::kSkinning)[0] == 7005, "skinning: Skinning Knife");
    Require(StarterTools(p::kBlacksmithing)[0] == 5956, "blacksmithing: Blacksmith Hammer");
    Require(StarterTools(p::kEngineering)[0] == 5956 && StarterTools(p::kEngineering)[1] == 6219,
        "engineering: Blacksmith Hammer and Arclight Spanner");
    Require(StarterTools(p::kEnchanting)[0] == 6218, "enchanting: Runed Copper Rod");
    Require(StarterTools(p::kJewelcrafting)[0] == 55155, "jewelcrafting: Jewelers Kit");
    for (std::uint32_t const skill : { p::kHerbalism, p::kAlchemy, p::kLeatherworking, p::kTailoring, 185u, 0u })
        Require(StarterTools(skill)[0] == 0 && StarterTools(skill)[1] == 0, "no tool for the other skills");

    // Every primary of every pair is a primary; secondaries are not.
    for (std::uint32_t pair = p::HerbalismAlchemy; pair <= p::HerbalismMining; ++pair)
    {
        p::Definition const* d = p::Find(static_cast<p::Pair>(pair));
        Require(d && IsPrimary(d->first) && IsPrimary(d->second), "both skills of each pair are primaries");
    }
    Require(!IsPrimary(185) && !IsPrimary(129) && !IsPrimary(356) && !IsPrimary(142), "secondaries are no primary");
    Require(kPrimaries.size() == 10, "ten primaries in the table line");
    for (std::uint32_t const skill : kPrimaries)
        Require(IsPrimary(skill), "every listed primary is a primary");

    // Apprentice candidate: no skill required, teaches a first rank, primary skill.
    Require(IsApprenticeCandidate(0, true, p::kMining), "the apprentice rank is a candidate");
    Require(!IsApprenticeCandidate(50, true, p::kMining), "a rank that needs skill is no candidate");
    Require(!IsApprenticeCandidate(0, false, p::kMining), "a recipe (no first rank) is no candidate");
    Require(!IsApprenticeCandidate(0, true, 185), "a secondary is no candidate");

    // Resolver: one distinct spell per skill, duplicates of the same spell are fine, two spells are ambiguous.
    auto const table = ResolveApprentice({
        { p::kMining, 2581 }, { p::kMining, 2581 }, { p::kJewelcrafting, 30219 },
        { p::kTailoring, 3911 }, { p::kTailoring, 9999 }, { 185, 2551 }, { p::kSkinning, 0 } });
    Require(table.at(p::kMining) == 2581, "the same apprentice spell from many trainers resolves");
    Require(table.at(p::kJewelcrafting) == 30219, "template-only skills resolve");
    Require(table.at(p::kTailoring) == kAmbiguous, "two apprentice spells: ambiguous, no guess");
    Require(table.count(185) == 0 && table.count(p::kSkinning) == 0, "secondaries and spell 0 are dropped");

    // Retry and logging: only a refused state is retried; the tick writes no not_green line.
    Require(RetryOnTick(Result::NotGreen), "a refused state is retried on level-up");
    for (Result r : { Result::Ok, Result::LearnFailed, Result::NoApprenticeSpell, Result::AmbiguousSpell })
        Require(!RetryOnTick(r), "every other result is final until the next login");
    Require(ShouldLog(Result::NotGreen, true) && !ShouldLog(Result::NotGreen, false), "not_green only at login");
    Require(ShouldLog(Result::Ok, false) && ShouldLog(Result::LearnFailed, false), "the tick logs grants and failures");
    Require(std::string(ToString(Result::Ok)) == "ok" && std::string(ToString(Result::NotGreen)) == "not_green",
        "result= text");

    std::cout << "profession_grant_policy_tests passed\n";
    return 0;
}
