#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // #367 phase 1 (owner 2026-09-27: bots only, no client patch): skeleton of
    // the rogue tank. It runs on top of the combat kit for a rogue on premade
    // path 4.3 "rogue tank" (AiFactory, roll weight 0 = off until the owner
    // signs off the design numbers). The taunt "Spit" (design D-2, PR #386) and
    // the tank poisons (D-1) do not exist as spells yet; the taunt action only
    // fires once a bot knows a spell of that name. Evasion covers the
    // parry/dodge tank, threat dumps (Feint, Vanish) are never used.
    class TankRogueStrategy : public Strategy
    {
    public:
        TankRogueStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        int GetType() override { return STRATEGY_TYPE_TANK | STRATEGY_TYPE_MELEE; }
        std::string getName() override { return "tank rogue"; }

    protected:
        void InitCombatTriggers(std::list<TriggerNode*>& triggers) override;
        void InitCombatMultipliers(std::list<Multiplier*>& multipliers) override;
    };

    class TankRogueThreatMultiplier : public Multiplier
    {
    public:
        TankRogueThreatMultiplier(PlayerbotAI* ai) : Multiplier(ai, "tank rogue threat") {}
        float GetValue(Action* action) override;
    };
}
