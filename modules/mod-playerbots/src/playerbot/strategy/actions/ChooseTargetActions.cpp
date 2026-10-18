#include "playerbot/strategy/Action.h"
#include "ChooseTargetActions.h"
#include "Movement/MovementGenerator.h"
#include "AI/CreatureAI.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/generic/PullStrategy.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/RotCastPolicy.h"
#include "playerbot/RotCastTrace.h"
#include "playerbot/GrindPickPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "Formulas.h"
#include <algorithm>

bool DpsAssistAction::isUseful()
{
    // if carry flag, do not start fight
    if (bot->HasAura(23333) || bot->HasAura(23335) || bot->HasAura(34976))
        return false;

    return true;
}

bool AttackAnythingAction::isUseful()
{
    if (!ai->AllowActivity(GRIND_ACTIVITY)) //Bot not allowed to be active
        return false;

    if (ai->HasRealPlayerMaster())
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    Unit* target = GetTarget();

    if (!target || !ai->IsSafe(target))
        return false;

    if (ai->ContainsStrategy(STRATEGY_TYPE_HEAL) && !ai->HasStrategy("offdps", BotState::BOT_STATE_COMBAT))
        return false;

    if(!target->IsPlayer() && bot->isInFront(target,target->GetAttackDistance(bot)*1.5f, M_PI_F*0.5f) && target->CanAttackOnSight(bot) && target->GetLevel() < bot->GetLevel() + 3.0) //Attack before being attacked.
        return true;

    if (AI_VALUE(bool, "travel target traveling") && CanFreeMoveValue::CanFreeMoveTo(ai, *AI_VALUE(TravelTarget*,"travel target")->GetPosition())) //Bot is traveling
        return false;

    return true;
}

bool ai::AttackAnythingAction::isPossible()
{
    return AttackAction::isPossible() && GetTarget();
}

bool ai::AttackAnythingAction::Execute(Event& event)
{
    // twow-repo#601 S2 diagnostic ([GrindPick], AiPlayerbot.BotUpdateTrace; OB-10 / OB-00 18.10.): the source of this
    // engagement as it was BEFORE the attack starts - AttackAction::Execute puts the bot on the target's attacker list at
    // once, so afterwards a plain grind or quest pull could look like attacker / victim (OB-10 re-check). Off: only this test.
    Unit* const pickTarget = sPlayerbotAIConfig.botUpdateTrace ? GetTarget() : nullptr;
    ai::grind_pick::Source pickSource = ai::grind_pick::Source::None;
    bool pickGrey = false;
    if (pickTarget)
    {
        std::list<ObjectGuid> const attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        bool const attacker = std::find(attackers.begin(), attackers.end(), pickTarget->GetObjectGuid()) != attackers.end();
        uint32 const entry = pickTarget->GetEntry();
        bool const neededForQuest = entry && AI_VALUE2(bool, "need for quest", std::to_string(entry));
        Creature* const creature = dynamic_cast<Creature*>(pickTarget);
        pickSource = ai::grind_pick::EngagedSource(attacker, pickTarget->GetVictim() == bot, neededForQuest);
        pickGrey = creature && !MaNGOS::XP::Gain(bot, creature);
    }

    // twow-repo#601 S2 / S4 [GrindWindow] engaged= / grey_engaged= (GrindMode 1, OB-10 rev. 2): a plain grind engagement (no
    // attacker, not on the bot, no quest need: R4's plain source) of the gap filler's population inside its window (the travel
    // target carries a window end not yet reached, and the quest route backoff still runs: R1 open, read like R1 - an expired
    // gap trip keeps its window end, so after the backoff end no engagement counts as inside a window, recheck). No kill hook
    // sees a grey kill (the XP packet comes only with XP), so the engagement is counted, read before the attack like
    // [GrindPick]. Per-bot values and one per-bot quest list, no value per creature. GrindMode 0 / 2: IsQuestGapGrind() is
    // false, nothing else read.
    Unit* const gapTarget = (ai->IsQuestGapGrind() &&
        AI_VALUE(TravelTarget*, "travel target")->GetGapWindowEnd() > uint32(time(nullptr)) &&
        uint32(time(nullptr)) < uint32(std::max(0, AI_VALUE2(int, "manual int", "quest route backoff until")))) ? GetTarget() : nullptr;
    bool gapPlain = false;
    bool gapGrey = false;
    if (gapTarget)
    {
        std::list<ObjectGuid> const attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        std::vector<int32> const& needed = AI_VALUE(std::vector<int32>, "needed quest entries");
        Creature* const creature = dynamic_cast<Creature*>(gapTarget);
        gapPlain = creature && ai::grind_pick::EngagedSource(
            std::find(attackers.begin(), attackers.end(), gapTarget->GetObjectGuid()) != attackers.end(), gapTarget->GetVictim() == bot,
            std::binary_search(needed.begin(), needed.end(), int32(gapTarget->GetEntry()))) == ai::grind_pick::Source::Grind;
        gapGrey = creature && !MaNGOS::XP::Gain(bot, creature);
    }

    bool result = AttackAction::Execute(event);
    if (result)
    {
        Unit* grindTarget = GetTarget();
        if (grindTarget)
        {
            std::string grindName = grindTarget->GetName();
            if (!grindName.empty())
            {
                sPlayerbotAIConfig.logEvent(ai, "AttackAnythingAction", grindName + " (lvl " + std::to_string(grindTarget->GetLevel()) + ")", std::to_string(grindTarget->GetEntry()));

                // twow-repo#601 S2 diagnostic: this engagement - the same AttackAnythingAction target bot_events logs above -
                // counted with the source read before the attack, only if it is still the same target.
                if (pickTarget && pickTarget == grindTarget)
                    ai::grind_pick::Global().Add(ai::grind_pick::BandOf(bot->GetLevel()), pickSource, pickGrey);

                // twow-repo#601 S2 / S4 [GrindWindow]: the same target, a plain grind pick of a window (read before the attack).
                if (gapTarget && gapTarget == grindTarget && gapPlain)
                {
                    PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Engaged));
                    if (gapGrey)
                        PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::GreyEngaged));
                }

                // twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace): the bot's HP / mana at the grind pull, as its own
                // line (bot_events.csv unchanged); at most 300 lines a minute.
                if (sPlayerbotAIConfig.rotationCastTrace && ai::rot_cast_rt::TakePullBudget(uint32(time(nullptr))))
                {
                    uint32 const guid = bot->GetGUIDLow();
                    int const mana = bot->GetMaxPower(POWER_MANA) ? int(bot->GetPowerPercent(POWER_MANA)) : -1;
                    sLog.outBasic("%s", ai::rot_cast::FormatPullLine(guid, bot->GetName(), bot->getClass(), bot->GetLevel(),
                        ai::cohort::Letter(guid), grindTarget->GetEntry(), grindTarget->GetLevel(), uint32(bot->GetHealthPercent()),
                        mana, uint32(bot->getAttackers().size()), bot->GetDistance(grindTarget)).c_str());
                }

                if (ai->HasStrategy("pull", BotState::BOT_STATE_COMBAT))
                {
                    if (PullStrategy* strategy = PullStrategy::Get(ai))
                    {
                        if (strategy->CanDoPullAction(grindTarget) && (ai->GetBot()->getClass() == CLASS_DRUID || ai->GetBot()->getClass() == CLASS_PALADIN || AI_VALUE2(uint32, "item count", "ammo")))
                        {
                            Event pullEvent("attack anything", grindTarget->GetObjectGuid());
                            bool doAction = ai->DoSpecificAction("pull my target", pullEvent, true);

                            if (doAction)
                            {
                                return true;
                            }
                        }
                    }
                }

                context->GetValue<ObjectGuid>("attack target")->Set(grindTarget->GetObjectGuid());
                ai->StopMoving();
            }
        }
    }

    return result;
}

bool AttackEnemyPlayerAction::isUseful()
{
    return !sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(bot));
}

bool AttackEnemyFlagCarrierAction::isUseful()
{
    Unit* target = context->GetValue<Unit*>("enemy flag carrier")->Get();
    // Was bot->HasAura(...) (2026-07-27, fixed) - that's the "am I personally
    // carrying a flag" check used correctly one function above in
    // DpsAssistAction::isUseful() ("if carry flag, do not start fight"), but
    // it was copy-pasted here unchanged. Requiring the ATTACKING bot to
    // already be a flag carrier made this action nearly always false for a
    // normal bot chasing the enemy flag carrier, since regular chasers don't
    // carry a flag - confirmed live: bots chased flag carriers relentlessly
    // but never actually attacked once they caught up. The check belongs on
    // the TARGET (confirming it's genuinely still an active flag carrier),
    // not on the attacker.
    return target && sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.GetDistance2d(bot, target), 75.0f) && (target->HasAura(23333) || target->HasAura(23335) || target->HasAura(34976));
}

bool SelectNewTargetAction::Execute(Event& event)
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target && sServerFacade.UnitIsDead(target))
    {
        // Save the dead target for later looting
        ObjectGuid guid = target->GetObjectGuid();
        if (guid)
        {
            AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
        }
    }

    // Clear the target variables
    ObjectGuid attackTarget = AI_VALUE(ObjectGuid, "attack target");
    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>, "possible targets no los");
    if (attackTarget && find(possible.begin(), possible.end(), attackTarget) == possible.end())
    {
        SET_AI_VALUE(ObjectGuid, "attack target", ObjectGuid());
    }

    // Save the old target and clear the current target
    if(target)
    {
        SET_AI_VALUE(Unit*, "old target", target);
        SET_AI_VALUE(Unit*, "current target", nullptr);
    }
    
    // Stop attacking
    bot->SetSelectionGuid(ObjectGuid());
    ai->InterruptSpell();
    bot->AttackStop();


    bool moreAttackers = false;
    // Check if there is any enemy targets available to attack
    if (AI_VALUE(bool, "has attackers"))
    {
        if (ai->HasStrategy("pvp", BotState::BOT_STATE_COMBAT) ||
            ai->HasStrategy("duel", BotState::BOT_STATE_COMBAT))
        {
            // Check if there is an enemy player nearby
            if (AI_VALUE(bool, "has enemy player targets"))
            {
                moreAttackers = true;
                return ai->DoSpecificAction("attack enemy player", event, true);
            }
        }

        // Let the dps/tank assist pick a target to attack
        if (ai->HasStrategy("dps assist", BotState::BOT_STATE_NON_COMBAT))
        {
            moreAttackers = true;
            return ai->DoSpecificAction("dps assist", event, true);
        }
        else if (ai->HasStrategy("tank assist", BotState::BOT_STATE_NON_COMBAT))
        {
            moreAttackers = true;
            return ai->DoSpecificAction("tank assist", event, true);
        }
    }
    
    if (!moreAttackers)
    {
        // Stop pet attacking
        Pet* pet = bot->GetPet();
        if (pet)
        {
            UnitAI* creatureAI = ((Creature*)pet)->AI();
            if (creatureAI)
            {
                // Send pet action packet
                const ObjectGuid& petGuid = pet->GetObjectGuid();
                const ObjectGuid& targetGuid = ObjectGuid();
                const uint8 flag = ACT_COMMAND;
                const uint32 spellId = COMMAND_FOLLOW;
                const uint32 command = (flag << 24) | spellId;

                WorldPacket data(CMSG_PET_ACTION);
                data << petGuid;
                data << command;
                data << targetGuid;
                bot->GetSession()->HandlePetAction(data);
            }
        }
    }

    return false;
}