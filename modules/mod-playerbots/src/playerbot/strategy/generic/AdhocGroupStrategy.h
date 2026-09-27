#pragma once
#include "playerbot/strategy/Strategy.h"

namespace ai
{
    // twow-repo#365 step 2: ad-hoc quest groups for roster bots, added by
    // AiFactory only while AiPlayerbot.BotGroups.Enabled = 1. Independent of
    // RandomBotGroupNearby and the upstream "invite nearby" / "invite guild" paths.
    class AdhocGroupStrategy : public Strategy
    {
    public:
        AdhocGroupStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "adhoc group"; }
#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "adhoc group"; } //Must equal iternal name
        virtual std::string GetHelpDescription() {
            return "This strategy lets roster bots on the same quest objective group up while they work on it.";
        }
        virtual std::vector<std::string> GetRelatedStrategies() { return {}; }
#endif
        void InitNonCombatTriggers(std::list<TriggerNode*>& triggers) override
        {
            // The action keeps its own scan interval (BotGroups.AdHoc.ScanIntervalSeconds).
            triggers.push_back(new TriggerNode(
                "very often",
                NextAction::array(0, new NextAction("ad-hoc group", 2.0f), NULL)));
        }
    };
}
