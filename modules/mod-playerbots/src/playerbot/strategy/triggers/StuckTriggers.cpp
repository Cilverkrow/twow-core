
#include "playerbot/playerbot.h"
#include "StuckTriggers.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/CohortPolicy.h"   // twow-repo#575 / #601 (Unstuck.CombatFightClock)
using namespace ai;

// twow-repo#575 / #601 (AiPlayerbot.Unstuck.CombatFightClock): the clock of "combat stuck" / "combat long stuck".
uint32 ai::CombatStuckSeconds(PlayerbotAI* ai, combat_stuck_clock::Clock& clock)
{
    AiObjectContext* context = ai->GetAiObjectContext();
    Player* bot = ai->GetBot();
    if (!bot || !cohort::Applies(sPlayerbotAIConfig.unstuckCombatFightClock, bot->GetGUIDLow()))
        return AI_VALUE2(uint32, "time since last change", "combat::self target");

    Unit* target = AI_VALUE(Unit*, "current target");
    return combat_stuck_clock::Seconds(clock, std::int64_t(time(nullptr)), std::int64_t(AI_VALUE(time_t, "combat start time")),
        target ? target->GetObjectGuid().GetRawValue() : 0);
}
