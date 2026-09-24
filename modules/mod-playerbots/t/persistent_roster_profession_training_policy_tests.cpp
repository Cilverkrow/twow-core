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

    Require(!IsEligible(true, 0, 1, HerbalismAlchemy), "level zero is blocked");
    Require(IsEligible(true, 1, 1, HerbalismAlchemy), "level one is eligible when the core trainer permits it");
    Require(!IsEligible(false, 60, 1, HerbalismAlchemy), "non-roster bot is blocked");
    Require(!IsEligible(true, 60, 1, None), "invalid pair fails closed");

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

    Require(IsEligibleProfessionTraining(true, 1, 1, HerbalismAlchemy, kHerbalism),
        "planned initial profession is allowed from the configured start level");
    Require(IsEligibleProfessionTraining(true, 1, 1, HerbalismAlchemy, kCooking),
        "allowed secondary initial profession is accepted");
    Require(!IsEligibleProfessionTraining(true, 1, 1, HerbalismAlchemy, kMining),
        "unplanned initial primary profession fails closed");
    Require(!IsEligibleProfessionTraining(false, 60, 1, HerbalismAlchemy, kHerbalism),
        "non-roster initial profession remains blocked");
    Require(!IsEligibleProfessionTraining(true, 10, 1, HerbalismAlchemy, kMining),
        "unplanned primary remains blocked at level ten and above");
    Require(IsEligibleProfessionTraining(true, 10, 1, HerbalismAlchemy, kHerbalism),
        "planned primary rank remains allowed at level ten and above");

    Require(!MayRequestRemoteTrainerTravel(true, true), "roster profession plan never starts remote trainer travel");
    Require(MayRequestRemoteTrainerTravel(false, true), "generic bot trainer travel is unchanged");
    Require(MayRequestRemoteTrainerTravel(true, false), "non-tradeskill trainer travel is unchanged");
    Require(IsWithinLocalTrainerRadius(0.0f, 30.0f), "co-located trainer is local");
    Require(IsWithinLocalTrainerRadius(30.0f, 30.0f), "local-radius boundary is accepted");
    Require(!IsWithinLocalTrainerRadius(30.1f, 30.0f), "distant city trainer is rejected");
    Require(!IsWithinLocalTrainerRadius(-1.0f, 30.0f), "unknown trainer distance is rejected");
    Require(!IsWithinLocalTrainerRadius(10.0f, -1.0f), "invalid configured radius is rejected");

    Require(ShouldEmitTrace(false, 1000, 0), "first decision is always traced");
    Require(!ShouldEmitTrace(true, 1000, 1300), "repeated decision inside cooldown is suppressed");
    Require(ShouldEmitTrace(true, 1300, 1300), "repeated decision after cooldown is traced again");
    return 0;
}
