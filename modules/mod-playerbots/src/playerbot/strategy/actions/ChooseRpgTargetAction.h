#pragma once

#include "playerbot/strategy/Action.h"
#include "RpgAction.h"
#include "playerbot/strategy/values/LastMovementValue.h"

namespace ai
{
    class RpgSubAction;
    class TriggerNode;
    class Trigger;

    class ChooseRpgTargetAction : public Action {
    public:
        ChooseRpgTargetAction(PlayerbotAI* ai, std::string name = "choose rpg target") : Action(ai, name) {}

        virtual bool Execute(Event& event);
        virtual bool isUseful();

        std::unordered_map<ObjectGuid, float> GetTargets(Player* requester, bool debug = false);

        std::string GetRpgActionReason(ObjectGuid target) { auto reason = rgpActionReason.find(target); if (reason != rgpActionReason.end())  return reason->second; return ""; }
    private:
        float getMaxRelevance(GuidPosition guidP);
        bool HasSameTarget(ObjectGuid guid, uint32 max, std::list<ObjectGuid>& nearGuids);

        std::unordered_map <ObjectGuid, std::string> rgpActionReason;

        // twow-repo#541 (deep dive R1): getMaxRelevance ran for every candidate target and rebuilt the
        // strategy list, every rpg trigger node and every handler array each time (PerfMon A1600: the
        // most expensive single action, 18-22 ms per call). Only IsActive (and an rpg sub-action's name)
        // depends on the target, so the list is built once per GetTargets call and evaluated per target,
        // in the same order.
        struct RpgTriggerEntry
        {
            TriggerNode* node = nullptr;
            Trigger* trigger = nullptr;
            float relevance = 0.0f;
            bool isRpg = false;
            RpgSubAction* lastSubAction = nullptr;  // the handlers' last rpg sub-action (its name may depend on the target)
        };
        std::vector<RpgTriggerEntry> rpgTriggers;
        bool rpgTriggersBuilt = false;
        bool rpgTriggerCacheActive = false;
        void BuildRpgTriggers();
        void ClearRpgTriggers();

        // Keeps the trigger list for the duration of one GetTargets call.
        struct RpgTriggerCacheScope
        {
            explicit RpgTriggerCacheScope(ChooseRpgTargetAction* action) : action(action) { action->rpgTriggerCacheActive = true; }
            ~RpgTriggerCacheScope() { action->rpgTriggerCacheActive = false; action->ClearRpgTriggers(); }
            ChooseRpgTargetAction* action;
        };
    };

    class ClearRpgTargetAction : public ChooseRpgTargetAction {
    public:
        ClearRpgTargetAction(PlayerbotAI* ai) : ChooseRpgTargetAction(ai, "clear rpg target") {}
    };

}
