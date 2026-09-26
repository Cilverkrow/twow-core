#include "playerbot/playerbot.h"
#include "ProfessionUseActions.h"
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"

using namespace ai;

bool ProfessionCraftAction::Execute(Event& event)
{
    SET_AI_VALUE2(time_t, "manual time", "profession craft", time(nullptr));

    bool const started = ai->DoSpecificAction("craft random item", Event(), true);
    TraceProfessionUse(ai, "craft", started ? "started" : "failed", started ? "skillup_recipe" : "cast_not_started",
        uint32(bot->GetLevel()));
    return started;
}
