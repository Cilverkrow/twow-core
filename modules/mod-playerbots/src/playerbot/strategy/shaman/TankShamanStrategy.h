#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // #357 phase 1 (owner 2026-09-27: bots only, no client patch): skeleton of
    // the shaman tank. It runs on top of the enhancement kit for a shaman on
    // premade path 7.3 "shaman tank" (AiFactory, roll weight 0 = off until the
    // owner signs off the design numbers). Threat per the design (PR #392):
    // Rockbiter (+35 %) instead of Windfury, Earthshaker Slam 51365 as taunt,
    // Lightning Shield charges kept up. Talent auras and numbers follow later.
    class TankShamanStrategy : public Strategy
    {
    public:
        TankShamanStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        int GetType() override { return STRATEGY_TYPE_TANK | STRATEGY_TYPE_MELEE; }
        std::string getName() override { return "tank shaman"; }

    protected:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
        void InitNonCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    // The tank keeps Rockbiter (the other weapon imbues are not cast) and uses
    // Stormstrike only with aggro and at least 4 shield charges (owner 2026-09-27).
    class TankShamanMultiplier : public Multiplier
    {
    public:
        TankShamanMultiplier(PlayerbotAI* ai) : Multiplier(ai, "tank shaman") {}
        float GetValue(Action* action) override;
    };
}
