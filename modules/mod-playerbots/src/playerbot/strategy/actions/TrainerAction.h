#pragma once
#include "GenericActions.h"

namespace ai
{
    class TrainerAction : public ChatCommandAction
    {
    public:
        TrainerAction(PlayerbotAI* ai, std::string name = "trainer") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;

    protected:
        typedef void (TrainerAction::*TrainerSpellAction)(uint32, ObjectGuid trainerGuid, uint32 spellId, TrainerSpell const*, std::ostringstream& msg);
        bool Iterate(Player* requester, Creature* creature, TrainerSpellAction action, SpellIds& spells);
        virtual void Learn(uint32 cost, ObjectGuid trainerGuid, uint32 spellId, TrainerSpell const* tSpell, std::ostringstream& msg);
        // The generic trainer command deliberately excludes tradeskill
        // trainers. The roster-specific action opts in explicitly and must
        // still filter every individual trainer spell below.
        virtual bool AllowsTradeSkillTrainer(Creature const* /*creature*/) const { return false; }
        virtual bool AllowsTrainerSpell(Creature const* /*creature*/, TrainerSpell const* /*spell*/) const { return true; }
        virtual bool UsesFreeTraining() const { return false; }

    private:
        void TellHeader(Player* requester, Creature* creature);
        void TellFooter(Player* requester, uint32 totalCost);
    };

    // #292 `train`: the player tells its own roster bot to learn what its class
    // trainer teaches. Only the bot's master (same group) or a GM; only at a
    // class trainer in interaction range (the selected one first). The bot does
    // not travel or teleport on its own - the player brings it there - and the
    // normal trainer gates (level, rank, prerequisites, cost policy) apply.
    class TrainCommandAction : public TrainerAction
    {
    public:
        TrainCommandAction(PlayerbotAI* ai) : TrainerAction(ai, "train") {}
        virtual bool Execute(Event& event) override;

    private:
        Creature* FindClassTrainer(Player* requester);
    };
}
