#include "playerbot/playerbot.h"
#include "TankRogueStrategy.h"
#include "RogueActions.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TankRoguePoisonPolicy.h"

using namespace ai;

void TankRogueStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // Taunt (design D-2 "Spit", 15 yd, main target plus two around it).
    triggers.push_back(new TriggerNode(
        "lose aggro",
        NextAction::array(0, new NextAction("rogue taunt", ACTION_HIGH + 5), NULL)));

    triggers.push_back(new TriggerNode(
        "medium health",
        NextAction::array(0, new NextAction("evasion", ACTION_HIGH + 3), NULL)));

    // Owner 2026-09-27: existing abilities first - Riposte after a parry,
    // Ghostly Strike uptime for dodge. The Turtle parry finisher follows once
    // OB-20 has identified the spell.
    triggers.push_back(new TriggerNode(
        "riposte",
        NextAction::array(0, new NextAction("riposte", ACTION_HIGH + 4), NULL)));

    triggers.push_back(new TriggerNode(
        "ghostly strike",
        NextAction::array(0, new NextAction("ghostly strike", ACTION_HIGH + 2), NULL)));

    triggers.push_back(new TriggerNode(
        "shadow dance",
        NextAction::array(0, new NextAction("shadow dance", ACTION_HIGH + 3), NULL)));
}

void TankRogueStrategy::InitNonCombatTriggers(std::list<TriggerNode*>& triggers)
{
    // #367 kit (core#188): Shadow Dance stays up between fights too.
    triggers.push_back(new TriggerNode(
        "shadow dance",
        NextAction::array(0, new NextAction("shadow dance", ACTION_NORMAL + 1), NULL)));

    // #367 (owner): the threat poison on the main hand, above the combat kit's
    // main-hand poison (which the multiplier below keeps off the main hand).
    triggers.push_back(new TriggerNode(
        "apply agitating poison main hand",
        NextAction::array(0, new NextAction("apply agitating poison main hand", ACTION_NORMAL + 2), NULL)));
}

void TankRogueStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TankRogueThreatMultiplier(ai));
}

// twow-repo#654 (Rotation.TankRoguePoison): the poison ping-pong ran out of combat, where the
// combat-only multiplier never looked. Feint / Vanish are no non-combat actions, so only the
// poison rule acts here.
void TankRogueStrategy::InitNonCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new TankRogueThreatMultiplier(ai));
}

// twow-repo#654 (OB-10 review 29.10.): the bot carries a threat poison it can use (level, bag or item cheat) - the same test
// as ApplyPoisonAction::isPossible, without the kit grant. Without one the kit's main-hand poison is not blocked.
bool TankRogueThreatMultiplier::HasUsableThreatPoison() const
{
    for (uint32 const itemId : ai::tank_rogue_poison::ThreatPoisonItems)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (proto && bot->GetLevel() >= proto->RequiredLevel && (ai->HasCheat(BotCheatMask::item) || bot->HasItemCount(itemId, 1)))
            return true;
    }

    return false;
}

float TankRogueThreatMultiplier::GetValue(Action* action)
{
    std::string const name = action->getName();
    if (name == "feint" || name == "vanish")
        return 0.0f;

    // twow-repo#654 (Rotation.TankRoguePoison): tell the poisons by the action (hand + threat
    // poison) - the names carry no hand, so the suffix check below never matched.
    if (ai::tank_rogue_poison::Applies(sPlayerbotAIConfig.rotationTankRoguePoison, bot->GetGUIDLow()))
    {
        ApplyPoisonAction const* poison = dynamic_cast<ApplyPoisonAction const*>(action);
        // the bag check only for a foreign main-hand poison (the cheap tests first)
        if (poison && poison->IsMainHand() && !poison->IsThreatPoison() &&
            ai::tank_rogue_poison::BlocksMainHand(true, poison->IsMainHand(), poison->IsThreatPoison(), HasUsableThreatPoison()))
            return 0.0f;

        return 1.0f;
    }

    // #367: no other poison on the main hand - it would replace the threat poison
    // (every "apply ... poison main hand" trigger treats a foreign poison as missing).
    std::string const mainHand = " poison main hand";
    if (name != "apply agitating poison main hand" && name.size() > mainHand.size() &&
        name.compare(0, 6, "apply ") == 0 && name.compare(name.size() - mainHand.size(), mainHand.size(), mainHand) == 0)
        return 0.0f;

    return 1.0f;
}
