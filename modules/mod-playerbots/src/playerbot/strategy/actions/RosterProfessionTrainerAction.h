#pragma once

#include "TrainerAction.h"
#include "playerbot/RosterProfessionTrace.h"

namespace ai
{
class RosterProfessionTrainerAction : public TrainerAction
{
public:
    RosterProfessionTrainerAction(PlayerbotAI* ai) : TrainerAction(ai, "roster profession trainer") {}
    bool Execute(Event& event) override;

    // Valid persisted pair of a persistent roster bot, otherwise 0.
    static uint32 PlannedPairFor(Player* bot);
    static uint32 GetTrainerSpellSkill(TrainerSpell const* spell);

protected:
    bool AllowsTradeSkillTrainer(Creature const* creature) const override;
    bool AllowsTrainerSpell(Creature const* creature, TrainerSpell const* spell) const override;
    bool UsesFreeTraining() const override;

private:
    uint32 GetPlannedPair() const;
    void TraceDecision(char const* state, char const* reason, Creature const* trainer) const;

    mutable RosterProfessionTraceGate traceGate;
};
}
