
#include "playerbot/playerbot.h"
#include "GrindingStrategy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/QuestGrindModePolicy.h"
#include <algorithm>
#include <list>
#include <string>
#include <vector>

using namespace ai;

void GrindingStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "no target",
        NextAction::array(0,
        new NextAction("attack anything", 5.0f), NULL)));
}

// twow-repo#601 GrindMode 2: the grind strategy's own guard. GrindMode is restart-only, so with 0 / 1 no multiplier is
// added and the engine is the same as before. Other strategies that only name "attack anything" (the rogue stealth
// allow list) or attack for real combat (dps / tank assist, attackers) are not touched.
void GrindingStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    if (ai::quest_grind_mode::NoGrind(sPlayerbotAIConfig.questFirstProgressionGrindMode))
        multipliers.push_back(new QuestGrindModeMultiplier(ai));
}

float QuestGrindModeMultiplier::GetValue(Action* action)
{
    if (!action || action->getName() != "attack anything" || !ai->IsQuestNoGrind())
        return 1.0f;

    Unit* target = AI_VALUE(Unit*, "grind target");
    if (!target)
        return 1.0f;   // isPossible fails anyway

    // Self-defence and group help: a creature that attacks the bot or its group.
    std::list<ObjectGuid> const attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
    bool const attacker = std::find(attackers.begin(), attackers.end(), target->GetObjectGuid()) != attackers.end();
    // A quest objective (kill or drop): the one sorted per-bot list, no value per creature.
    std::vector<int32> const& needed = AI_VALUE(std::vector<int32>, "needed quest entries");
    bool const questObjective = target->GetEntry() &&
        std::binary_search(needed.begin(), needed.end(), int32(target->GetEntry()));
    if (ai::quest_grind_mode::TargetAllowed(true, attacker, questObjective))
        return 1.0f;

    ai->NoteQuestIdle(uint8(ai::quest_grind_mode::TargetReason(AI_VALUE(bool, "travel target active"))),
        ai::quest_grind_mode::TargetText("attack_anything", target->GetEntry()));
    return 0.0f;
}
