#include "playerbot/playerbot.h"
#include "ProfessionUseActions.h"
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"

using namespace ai;

bool ProfessionCraftAction::Execute(Event& event)
{
    SET_AI_VALUE2(time_t, "manual time", "profession craft", time(nullptr));

    // Hotfix 8.13: "craft random item" only sends the cast command; "started" is traced by
    // CastCustomSpellAction once the cast really began, a failure there with its cast result.
    bool const requested = ai->DoSpecificAction("craft random item", Event(), true);
    if (!requested)
        TraceProfessionUse(ai, "craft", "failed", "cast_not_started", uint32(bot->GetLevel()));
    return requested;
}
