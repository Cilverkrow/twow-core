#include "PersistentRosterProfessionTrainingPolicy.h"

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
    using namespace ai;
    using namespace ai::profession;
    using namespace ai::profession_training;

    Require(!IsEligible(true, 2, 3, HerbalismAlchemy), "level two is blocked");
    Require(IsEligible(true, 3, 3, HerbalismAlchemy), "level three is eligible");
    Require(!IsEligible(false, 60, 3, HerbalismAlchemy), "non-roster bot is blocked");
    Require(!IsEligible(true, 60, 3, None), "invalid pair fails closed");

    Require(IsAllowedSkill(HerbalismAlchemy, kHerbalism), "planned first primary accepted");
    Require(IsAllowedSkill(HerbalismAlchemy, kAlchemy), "planned second primary accepted");
    Require(!IsAllowedSkill(HerbalismAlchemy, kMining), "unplanned primary rejected");
    Require(!IsAllowedSkill(HerbalismAlchemy, kEngineering), "third primary rejected");

    Require(IsAllowedSkill(MiningEngineering, kCooking), "cooking is a secondary skill");
    Require(IsAllowedSkill(MiningEngineering, kFishing), "fishing is a secondary skill");
    Require(IsAllowedSkill(MiningEngineering, kFirstAid), "first aid is a secondary skill");
    Require(IsAllowedSkill(MiningEngineering, kSurvival), "Turtle survival skill 142 is accepted");
    Require(kSurvival == 142, "survival keeps the Turtle skill identifier");
    Require(!IsAllowedSkill(MiningEngineering, 0), "unknown skill is rejected");

    Require(MayStartAutonomousTravel(false), "unled roster bot may travel");
    Require(!MayStartAutonomousTravel(true), "real master blocks autonomous trainer travel");
    return 0;
}
