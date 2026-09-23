#pragma once
#include "playerbot/strategy/Value.h"
#include "NearestUnitsValue.h"
#include "playerbot/PlayerbotAIConfig.h"

namespace ai
{
    class PossibleRpgTargetsValue : public NearestUnitsValue
	{
	public:
        PossibleRpgTargetsValue(PlayerbotAI* ai, float range = sPlayerbotAIConfig.rpgDistance);

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;

    public:
        static std::vector<uint32> allowedNpcFlags;
	};

    // Tradeskill trainers within AiPlayerbot.ProfessionTraining.LocalTrainerRadius
    // (default 120 yards). Only added to the RPG candidates of persistent roster
    // bots, so a bot already in town picks up its planned profession on the way.
    // This is a wider local reach, never a travel target.
    class RosterProfessionTrainersValue : public NearestUnitsValue
    {
    public:
        RosterProfessionTrainersValue(PlayerbotAI* ai) :
            NearestUnitsValue(ai, "roster profession trainers", sPlayerbotAIConfig.professionTrainingLocalTrainerRadius, true, 10) {}

    protected:
        void FindUnits(std::list<Unit*> &targets) override;
        bool AcceptUnit(Unit* unit) override;
    };
}
