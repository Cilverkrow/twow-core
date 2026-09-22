#pragma once

#include "TrainerAction.h"

#include <ctime>
#include <string>

namespace ai
{
class RosterProfessionTrainerAction : public TrainerAction
{
public:
    RosterProfessionTrainerAction(PlayerbotAI* ai) : TrainerAction(ai, "roster profession trainer") {}
    bool Execute(Event& event) override;

protected:
    bool AllowsTradeSkillTrainer(Creature const* creature) const override;
    bool AllowsTrainerSpell(Creature const* creature, TrainerSpell const* spell) const override;
    bool UsesFreeTraining() const override;

private:
    uint32 GetPlannedPair() const;
    uint32 GetTrainerSpellSkill(TrainerSpell const* spell) const;
    void TraceDecision(char const* state, char const* reason, Creature const* trainer) const;

    mutable std::string lastTraceState;
    mutable std::time_t nextTraceAt = 0;
};
}
