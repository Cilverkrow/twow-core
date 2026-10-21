
#include "playerbot/playerbot.h"
#include "GenericTriggers.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/AoeValues.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/EmergencyLadderPolicy.h"
#include "playerbot/EmergencyLadderTrace.h"
#include "playerbot/MeleeFleeBreakPolicy.h"
#include "playerbot/strategy/values/LastMovementValue.h"

#include <regex>

using namespace ai;

bool NoManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < 5;
}

bool LowManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana;
}

bool MediumManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.mediumMana;
}

bool HighManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") < 65;
}

// twow-repo#602 ROT-01 X1: the rest episode itself is the "rest gate" value (false with the switch off and in the cohort it does not
// apply to); these only add the HP / mana target.
bool RestHealthTrigger::IsActive()
{
    return sPlayerbotAIConfig.rotationRestGate && AI_VALUE(bool, "rest gate") &&
        AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.rotationRestGateTargetHp;
}

bool RestManaTrigger::IsActive()
{
    // bot->HasMana(): a druid in cat / bear form has no mana pool to drink for (the "has mana" value reads the stored mana).
    return sPlayerbotAIConfig.rotationRestGate && bot->HasMana() && AI_VALUE(bool, "rest gate") &&
        AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.rotationRestGateTargetMana;
}

bool AlmostFullManaTrigger::IsActive()
{
    return AI_VALUE2(bool, "has mana", "self target") && AI_VALUE2(uint8, "mana", "self target") > 85;
}

bool RageAvailable::IsActive()
{
    return AI_VALUE2(uint8, "rage", "self target") >= amount;
}

bool EnergyAvailable::IsActive()
{
	return AI_VALUE2(uint8, "energy", "self target") >= amount;
}

bool ComboPointsAvailableTrigger::IsActive()
{
    return AI_VALUE2(uint8, "combo", "current target") >= amount;
}

bool LoseAggroTrigger::IsActive()
{
    if(!AI_VALUE2(bool, "has aggro", "current target"))
    {
        // Check if the aggro has been taken by another tank
        if(ai->IsTank(bot))
        {
            Unit* target = AI_VALUE(Unit*, "current target");
            if(target && !target->IsPlayer())
            {
                Unit* targetsTarget = target->GetVictim();
                if(targetsTarget && targetsTarget->IsPlayer())
                {
                    Player* targetsPlayerTarget = (Player*)targetsTarget;
                    return !ai->IsTank(targetsPlayerTarget);
                }
            }
        }

        return true;
    }

    return false;
}

bool HasAggroTrigger::IsActive()
{
    return AI_VALUE2(bool, "has aggro", "current target");
}

namespace
{
// twow-repo#602 ROT-01 X3: the bots the emergency ladder may act for - a solo free bot without a real player, not a healer (F1, OB-00
// 18.10.2026: healers stay outside the ladder). The cohort is checked by the caller.
bool LadderBot(PlayerbotAI* ai, Player* bot)
{
    return !bot->GetGroup() && !ai->IsRealPlayer() && sRandomPlayerbotMgr.IsFreeBot(bot) && !ai->ContainsStrategy(STRATEGY_TYPE_HEAL);
}

// twow-repo#602 X3: the first class escape that is known and off cooldown, or nullptr. Only escapes with an existing critical-health node in
// the class strategies (they fire on their own while panic stays quiet): warrior Intimidating Shout, prot warrior Last Stand / Shield Wall
// (F4), rogue Blind / Vanish (not on the tank path, whose strategy turns Vanish off), mage Ice Block, paladin Lay on Hands / Divine Shield,
// warlock Sacrifice (Voidwalker out). The bear has no critical-health node (Frenzied Regeneration sits on low health): DruidKit.
char const* ReadyEscape(PlayerbotAI* ai, Player* bot)
{
    auto ready = [ai, bot](char const* spell)
    {
        uint32 const id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
        return id && sServerFacade.IsSpellReady(bot, id);
    };
    switch (bot->getClass())
    {
    case CLASS_WARRIOR:
        if (ai->ContainsStrategy(STRATEGY_TYPE_TANK))
            // Last Stand falls back to Intimidating Shout in its node (ProtectionWarriorStrategy), so the shout counts as an escape too.
            return ready("last stand") ? "last stand" : (ready("shield wall") ? "shield wall" :
                (ready("intimidating shout") ? "intimidating shout" : nullptr));
        return ready("intimidating shout") ? "intimidating shout" : nullptr;
    case CLASS_ROGUE:
        if (ready("blind"))
            return "blind";
        return !ai->HasStrategy("tank rogue", BotState::BOT_STATE_COMBAT) && ready("vanish") ? "vanish" : nullptr;
    case CLASS_MAGE:
        return ready("ice block") ? "ice block" : nullptr;
    case CLASS_PALADIN:
        // Divine Shield falls back to Divine Protection in its node (PaladinStrategy), so that counts as an escape too.
        return ready("lay on hands") ? "lay on hands" : (ready("divine shield") ? "divine shield" :
            (ready("divine protection") ? "divine protection" : nullptr));
    case CLASS_WARLOCK:
    {
        // Sacrifice is the Voidwalker's spell (1860): ready as the pet spell action sees it - the pet knows it and it is off cooldown.
        Pet* pet = bot->GetPet();
        if (!pet || !pet->IsAlive() || pet->GetEntry() != 1860)
            return nullptr;
        uint32 const id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "sacrifice")->Get();
        return id && pet->HasSpell(id) && pet->IsSpellReady(id) ? "sacrifice" : nullptr;
    }
    default:
        return nullptr;
    }
}

ai::emergency_ladder::Situation LadderSituation(PlayerbotAI* ai, Player* bot, char const* escape)
{
    ai::emergency_ladder::Situation s;
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    s.hasTarget = target && target->IsAlive();
    if (s.hasTarget)
    {
        s.targetPlayer = target->IsPlayer();
        Creature* creature = target->ToCreature();
        s.targetElite = creature && (creature->IsElite() || creature->IsWorldBoss());
        s.targetHp = uint32(target->GetHealthPercent());
        s.levelGap = int(target->GetLevel()) - int(bot->GetLevel());
    }
    s.attackers = ai->GetAiObjectContext()->GetValue<uint8>("my attacker count")->Get();
    s.escapeReady = escape != nullptr;
    s.fightTargetHp = sPlayerbotAIConfig.rotationEmergencyLadderFightTargetHp;
    return s;
}
}

bool PanicTrigger::IsActive()
{
    bool const panic = !ai->IsInPvp() &&
           AI_VALUE2(uint8, "health", "self target") < sPlayerbotAIConfig.criticalHealth &&
		   (!AI_VALUE2(bool, "has mana", "self target") ||
            AI_VALUE2(uint8, "mana", "self target") < sPlayerbotAIConfig.lowMana);
    // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): with the switch off nothing below runs, not even the episode state.
    if (!sPlayerbotAIConfig.rotationEmergencyLadder)
        return panic;
    return EmergencyLadder(panic);
}

// One episode = panic active without a break. The choice is made once; a bot the switch does not apply to (cohort A in mode 2) logs it with
// applied=0 and flees as before - the paired counterpart of cohort B.
bool PanicTrigger::EmergencyLadder(bool panic)
{
    namespace el = ai::emergency_ladder;
    if (!panic)
    {
        ladderOpen = false;
        return false;
    }
    if (!LadderBot(ai, bot))
        return true;

    uint32 const guid = bot->GetGUIDLow();
    bool const cohortB = ai::cohort::InB(guid);
    bool const applied = ai::cohort::Applies(sPlayerbotAIConfig.rotationEmergencyLadder, guid);
    uint32 const nowMs = WorldTimer::getMSTime();
    // An episode left open by the last fight (the trigger is not evaluated outside combat) or across a config reload is over.
    if (ladderOpen && (el::EpisodeStale(nowMs, ladderLastSeenMs) || !bot->IsInCombat()))
        ladderOpen = false;
    ladderLastSeenMs = nowMs;
    if (!ladderOpen)
    {
        ladderEscape = ReadyEscape(ai, bot);
        el::Situation const s = LadderSituation(ai, bot, ladderEscape);
        el::Choice const choice = el::Decide(s);
        ladderOpen = true;
        ladderStartMs = nowMs;
        ladderChoice = uint8(choice);
        ladderFallbackCounted = false;
        ladderWon = false;
        ai::emergency_ladder_rt::RecordEpisode(bot->getClass(), cohortB, choice, el::EscapeOverFight(s));
        if (ai::emergency_ladder_rt::TakeLineBudget(uint32(time(nullptr))))
        {
            int const mana = bot->GetMaxPower(POWER_MANA) ? int(bot->GetPowerPercent(POWER_MANA)) : -1;
            sLog.outBasic("%s", el::FormatEpisodeLine(guid, bot->GetName(), bot->getClass(), bot->GetLevel(), ai::cohort::Letter(guid), applied,
                "panic", choice, choice == el::Choice::Escape ? ladderEscape : nullptr, uint32(bot->GetHealthPercent()), mana, s).c_str());
        }
    }
    if (!applied)
        return true;

    switch (el::Choice(ladderChoice))
    {
    case el::Choice::Fight:
    {
        el::Situation const s = LadderSituation(ai, bot, nullptr);
        if (el::FightStillHolds(s))
            return false;   // no flee: the killing blow
        // Won: nothing left to flee from - no new episode, counted once (OB-10 review 18.10.).
        if (el::FightWon(s))
        {
            if (!ladderWon)
                ai::emergency_ladder_rt::RecordFightWon(bot->getClass(), cohortB);
            ladderWon = true;
            return false;
        }
        // The fight is over: target dead or gone with a mob still on the bot, or its conditions broke (a second mob, the target healed).
        // Either way the ladder decides again - a new episode, which may find an escape ready. Only a fight that was not won before
        // counts as an abort.
        if (s.hasTarget && !ladderWon)
            ai::emergency_ladder_rt::RecordFightAbort(bot->getClass(), cohortB);
        ladderOpen = false;
        return EmergencyLadder(panic);   // one level: a new decision is Fight only while FightStillHolds, which returns above
    }
    case el::Choice::Escape:
        if (el::EscapeWindowOpen(nowMs, ladderStartMs))
            return false;   // the class escape node fires on its own
        // Window over: an escape that is still ready was never cast (its node did not fire) - counted per class (OB-10 18.10.).
        if (!ladderFallbackCounted && ladderEscape && ReadyEscape(ai, bot) == ladderEscape)
            ai::emergency_ladder_rt::RecordEscapeFallback(bot->getClass(), cohortB);
        ladderFallbackCounted = true;
        return true;
    default:
        return true;
    }
}

bool OutNumberedTrigger::IsActive()
{
    // twow-repo#602 ROT-01 (AiPlayerbot.Rotation.MeleeFleeBreak): with the switch off only this test is added.
    if (!sPlayerbotAIConfig.rotationMeleeFleeBreak)
        return OutnumberedActive();
    return MeleeFleeBreak(OutnumberedActive());
}

// MeleeFleeBreakPolicy.h: a melee bot whose flee loop holds (two flee dispatches within ReturnDelay) is not sent to flee
// again until the window has passed - it fights instead. Ranged bots and bots outside the cohort are unchanged.
bool OutNumberedTrigger::MeleeFleeBreak(bool active)
{
    namespace mfb = ai::melee_flee_break;
    if (!active || !mfb::Applies(sPlayerbotAIConfig.rotationMeleeFleeBreak, bot->GetGUIDLow(), ai->IsRanged(bot)))
    {
        fleeBreakHeld = false;
        return active;
    }
    LastMovement& lastMovement = AI_VALUE(LastMovement&, "last movement");
    if (mfb::Holds(int64(time(nullptr)), int64(lastMovement.lastFleeAttempt), lastMovement.fleeCount,
            int64(sPlayerbotAIConfig.returnDelay / 1000)))
    {
        if (!fleeBreakHeld)
            mfb::breaks.fetch_add(1, std::memory_order_relaxed);
        fleeBreakHeld = true;
        mfb::held.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    fleeBreakHeld = false;
    mfb::allowed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool OutNumberedTrigger::OutnumberedActive()
{
    // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): with the switch off only this test is added.
    if (!sPlayerbotAIConfig.rotationEmergencyLadder)
        return Outnumbered();

    bool const outnumbered = Outnumbered();
    // Below critical health the ladder gives a ready class escape its window first (escape part only; panic decides fight / flee).
    if (!outnumbered || AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.criticalHealth || !LadderBot(ai, bot) ||
        !ai::cohort::Applies(sPlayerbotAIConfig.rotationEmergencyLadder, bot->GetGUIDLow()))
    {
        escapeOpen = false;
        return outnumbered;
    }
    uint32 const nowMs = WorldTimer::getMSTime();
    if (escapeOpen && ai::emergency_ladder::EpisodeStale(nowMs, escapeLastSeenMs))
        escapeOpen = false;   // left over from an earlier fight or a config reload
    escapeLastSeenMs = nowMs;
    if (!escapeOpen)
    {
        if (!ReadyEscape(ai, bot))
            return true;
        escapeOpen = true;
        escapeStartMs = nowMs;
    }
    return !ai::emergency_ladder::EscapeWindowOpen(nowMs, escapeStartMs);
}

bool OutNumberedTrigger::Outnumbered()
{
    // Below level 10, Hunter has no viable kiting toolkit (ranged abilities have an 8yd
    // minimum range, no traps yet, only Concussive Shot to slow) - fleeing mid-fight can't
    // actually create distance, it just eats time while still taking hits. The correct
    // pre-10 Hunter playbook is: burst at range, then fight it out in melee once the mob
    // closes. Don't fire this trigger for low-level Hunters at all; PanicTrigger (separate,
    // narrower - critical health AND low/no mana) is untouched, so a genuine near-death
    // escape attempt still fires.
    if (bot->getClass() == CLASS_HUNTER && bot->GetLevel() < 10)
        return false;

    // Druid has Rejuvenation/Healing Touch as a genuine pre-10 self-sufficiency fallback -
    // no need to preserve a flee/retreat option here either. Same reasoning as Hunter above,
    // different tool (self-heal instead of "no viable kiting toolkit").
    if (bot->getClass() == CLASS_DRUID && bot->GetLevel() < 10)
        return false;

    // Mage has no kiting toolkit pre-10 either (Frost Nova is level 10, Blink is level 22) -
    // same reasoning as Hunter: fleeing can't create real distance yet, so don't bother.
    if (bot->getClass() == CLASS_MAGE && bot->GetLevel() < 10)
        return false;

    // Priest gets real self-healing from level 1 (Renew, Heal), so the pre-10 case is the
    // same "self-sufficiency beats fleeing" reasoning as Druid. Additionally, when playing
    // solo (no group to preserve healing capacity for), that reasoning holds at any level -
    // there's no one else depending on this bot surviving longer by running instead of
    // healing through it.
    if (bot->getClass() == CLASS_PRIEST && (bot->GetLevel() < 10 || !bot->GetGroup()))
        return false;

    // Don't trigger if the bot is a dungeon or raid
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMap()->IsDungeon() || bot->GetMap()->IsRaid())
        return false;

    // Don't trigger if the bot is in a raid group
    if (bot->GetGroup() && bot->GetGroup()->IsRaidGroup())
        return false;

    // Don't trigger if the bot is in a group with a real player
    if (bot->GetGroup() && ai->HasRealPlayerMaster())
        return false;

    // Being outmatched on paper is not by itself an emergency. The comparison
    // below weighs levels and headcount, and `dLevel * 200` overtakes the
    // friendly side as soon as a single opponent is two levels up - so a solo
    // bot at full health broke off from one mob before a blow had landed. That
    // is also the least useful moment to run: nothing has gone wrong yet, and
    // the fight may well have been winnable. Engage first and let this decide
    // whether to disengage once the fight has actually turned; at mediumHealth
    // there is still enough left to get away. PanicTrigger covers the genuine
    // near-death case separately, and it is deliberately narrower (critical
    // health *and* no mana). Tunable via AiPlayerbot.MediumHealth.
    if (AI_VALUE2(uint8, "health", "self target") >= sPlayerbotAIConfig.mediumHealth)
        return false;

    int32 botLevel = bot->GetLevel();
    float healthMod = bot->GetHealthPercent() / 100.0f;
    uint32 friendPower = 100 + 100 * healthMod, foePower = 0;
    for (auto &attacker : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get())
    {
        Creature* creature = ai->GetCreature(attacker);
        if (!creature)
            continue;

        if (!creature->CanAttackOnSight(bot))
            continue;

        int32 dLevel = creature->GetLevel() - botLevel;

        healthMod = creature->GetHealthPercent() / 100.0f;

        if(dLevel > -10)
            foePower += std::max(100 + 10 * dLevel, dLevel * 200) * healthMod;
    }

    if (!foePower)
        return false;

    for (auto & helper : ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid> >("nearest friendly players")->Get())
    {
        Unit* player = ai->GetUnit(helper);

        if (!player || player == bot)
            continue;

        int32 dLevel = player->GetLevel() - botLevel;

        healthMod = player->GetHealthPercent() / 100.0f;

        if (dLevel > -10 && sServerFacade.GetDistance2d(bot, player) < 10.0f)
            friendPower += std::max(200 + 20 * dLevel, dLevel * 200)* healthMod;
    }

    return friendPower < foePower;
}

bool BuffTrigger::IsActive()
{
    Unit* target = GetTarget();
	return target && !ai->HasAura(spell, target, false, checkIsOwner) && target->IsAlive();
}

bool MyBuffTrigger::IsActive()
{
    Unit* target = GetTarget();
    return target && !ai->HasMyAura(spell, target);
}

Value<Unit*>* BuffOnPartyTrigger::GetTargetValue()
{
    const std::string qualifier = spell + "-" + (ignoreTanks ? "1" : "0");
	return context->GetValue<Unit*>("friendly unit without aura", qualifier);
}

Value<Unit*>* GreaterBuffOnPartyTrigger::GetTargetValue()
{
    const std::string qualifier = spell + "-" + (ignoreTanks ? "1" : "0");
    return context->GetValue<Unit*>("party member without aura", qualifier);
}

Value<Unit*>* MyBuffOnPartyTrigger::GetTargetValue()
{
    const std::string qualifier = spell + "-" + (ignoreTanks ? "1" : "0");
    return context->GetValue<Unit*>("party member without my aura", qualifier);
}

ai::Value<Unit*>* BuffOnTankTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("party tank without aura", spell);
}

Value<Unit*>* DebuffOnAttackerTrigger::GetTargetValue()
{
	return context->GetValue<Unit*>("attacker without aura", spell);
}

bool NoAttackersTrigger::IsActive()
{
    return !AI_VALUE(Unit*, "current target") && AI_VALUE(uint8, "my attacker count") > 0;
}

// twow-repo#568 (D69): "duel target" (the only thing "attack duel opponent" isUseful asks) is null without m_duel.
bool DuelNoAttackersTrigger::IsActive()
{
    return bot->m_duel && NoAttackersTrigger::IsActive();
}

bool InvalidTargetTrigger::IsActive()
{
    return AI_VALUE2(bool, "invalid target", "current target");
}

bool NoTargetTrigger::IsActive()
{
	return !AI_VALUE(Unit*, "current target") || AI_VALUE2(bool, "invalid target", "current target");
}

bool MyAttackerCountTrigger::IsActive()
{
    return AI_VALUE2(bool, "combat", "self target") && AI_VALUE(uint8, "my attacker count") >= amount;
}

bool HighThreatTrigger::IsActive()
{
    uint8 relativeThreat = AI_VALUE2(uint8, "threat", "current target");

    if (relativeThreat >= 80)
    {
        //Watch delta.
        uint32 lastTime = MEM_AI_VALUE(float, "my threat::current target")->GetLastTime();

        if (lastTime < time(0))
        {
            float deltaThreat = LOG_AI_VALUE(float, "my threat::current target")->GetDelta(5.0f);
            float currentThreat = AI_VALUE2(float, "my threat", "current target");

            if (deltaThreat > 0)
            {
                float tankThreat = AI_VALUE2(float, "tank threat", "current target");

                float newThreat = currentThreat + deltaThreat * 5.0f; //No agro in 5 seconds.

                if (newThreat < tankThreat)
                    return false;
            }
        }

        return true;
    }

    return false;
}

bool MediumThreatTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "threat", "current target") >= 60)
        return true;

    return false;
}

bool SomeThreatTrigger::IsActive()
{
    if (AI_VALUE2(uint8, "threat", "current target") >= 25)
        return true;

    return false;
}

bool NoThreatTrigger::IsActive()
{
    if (SomeThreatTrigger::IsActive())
        return false;

    return true;
}

bool AoeTrigger::IsActive()
{
    std::list<ObjectGuid> aoeEnemies = AoeCountValue::FindMaxDensity(bot, range);
    return aoeEnemies.size() >= amount;
}

bool DebuffTrigger::IsActive()
{
    Unit* target = GetTarget();
    if(target && target->IsAlive())
    {
        if (!ai->HasAura(spell, target, false, checkIsOwner))
        {
            return !HasMaxDebuffs();
        }
    }

    return false;
}

bool DebuffTrigger::HasMaxDebuffs()
{
#ifdef MANGOSBOT_TWO
    return false;
#else
    Unit* target = GetTarget();
    if(target)
    {
#ifdef MANGOSBOT_ONE
        constexpr uint32 debuffLimit = 40;
#else
        constexpr uint32 debuffLimit = 16;
#endif

        return ai->GetAuras(target, false, false).size() >= debuffLimit;
    }
#endif

    return false;
}

bool SpellTrigger::IsActive()
{
	return GetTarget();
}

bool SpellCanBeCastedTrigger::IsActive()
{
	Unit* target = GetTarget();
	// Ignore range/LOS here so this only gates on real cast-blockers (cooldown, mana,
	// proc/aura-state requirements). The action's own "reach spell" prerequisite is what
	// handles closing distance - if range were checked here too, the trigger would never
	// fire while out of range, so the reach step it depends on would never get a chance to run.
	return target && ai->CanCastSpell(spell, target, true, nullptr, true);
}

bool SpellNoCooldownTrigger::IsActive()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", name);
    if (!spellId)
        return false;

    return bot->IsSpellReady(spellId);
}

bool RandomTrigger::IsActive()
{
    if (time(0) - lastCheck < sPlayerbotAIConfig.repeatDelay / 1000)
        return false;

    lastCheck = time(0);
    int k = (int)(probability / sPlayerbotAIConfig.randomChangeMultiplier);
    if (k < 1) k = 1;
    return (rand() % k) == 0;
}

bool AndTrigger::IsActive()
{
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(tname);
        if (!trigger1 || !trigger1->IsActive())
            return false;
    }

    return true;
}

std::string AndTrigger::getName()
{
    std::string name;
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        if (!name.empty())
            name += " and ";
        name += tname;
    }

    return name;
}

bool OrTrigger::IsActive()
{
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(tname);
        if (trigger1 && trigger1->IsActive())
            return true;
    }

    return false;
}

std::string OrTrigger::getName()
{
    std::string name;
    std::vector<std::string> tnames = getMultiQualifiers(getQualifier(), ",");

    for (auto tname : tnames)
    {
        if (!name.empty())
            name += " or ";
        name += tname;
    }

    return name;
}

bool TwoTriggers::IsActive()
{
    if (name1.empty() || name2.empty())
        return false;

    Trigger* trigger1 = ai->GetAiObjectContext()->GetTrigger(name1);
    Trigger* trigger2 = ai->GetAiObjectContext()->GetTrigger(name2);

    if (!trigger1 || !trigger2)
        return false;

    return trigger1->IsActive() && trigger2->IsActive();
}

std::string TwoTriggers::getName()
{
    std::string name;
    name = name1 + " and " + name2;
    return name;
}

bool BoostTrigger::IsActive()
{
    if (ai->IsStateActive(BotState::BOT_STATE_COMBAT) && BuffTrigger::IsActive())
    {
        if (!ai->HasRealPlayerMaster())
        {
            return AI_VALUE(uint8, "balance") <= balance;
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool ItemCountTrigger::IsActive()
{
	return AI_VALUE2(uint32, "item count", item) < uint32(count);
}

bool InterruptSpellTrigger::IsActive()
{
	return SpellTrigger::IsActive() && ai->IsInterruptableSpellCasting(GetTarget(), getName(), true);
}

bool DeflectSpellTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    if (!target->IsNonMeleeSpellCasted(true))
        return false;

    if (!target->HasTarget(bot->GetObjectGuid()))
        return false;

    uint32 spellid = context->GetValue<uint32>("spell id", spell)->Get();
    if (!spellid)
        return false;

    SpellEntry const *deflectSpell = sServerFacade.LookupSpellInfo(spellid);
    if (!deflectSpell)
        return false;

    // warrior deflects all
    if (spell == "spell reflection")
        return true;

    // human priest feedback
    if (spell == "feedback")
        return true;

    SpellSchoolMask deflectSchool = SpellSchoolMask(deflectSpell->EffectMiscValue[0]);
    SpellSchoolMask attackSchool = SPELL_SCHOOL_MASK_NONE;

    Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (spell)
    {
        SpellEntry const* tarSpellInfo = spell->m_spellInfo;
        if (tarSpellInfo)
        {
            attackSchool = GetSpellSchoolMask(tarSpellInfo);
            if (deflectSchool == attackSchool)
                return true;
        }
    }
    return false;
}

bool HasAuraTrigger::IsActive()
{
   if (!name.empty())
	{
      return ai->HasAura(name, GetTarget(), false, false, -1, false, 0, auraTypeId);
   }

   std::string str = getQualifier();
   std::regex pattern(R"(spellid::(\d+)::([^:]*)::(\d+))");
   std::smatch match;

   if (std::regex_search(str, match, pattern) && match.size() == 4)
   {
      uint32 spell_id = atoi(match[1].str().c_str());

      if (Aura* aura = ai->GetAura(spell_id, GetTarget()))
      {
         uint32 count = atoi(match[3].str().c_str());
         uint32 stack_size = aura->GetStackAmount();
         std::string comp_symb = match[2].str();

         if (comp_symb == "equal")
         {
            return stack_size == count;
         }
         else if (comp_symb == "greater or equal")
         {
            return stack_size >= count;
         }
         else if (comp_symb == "lesser or equal")
         {
            return stack_size <= count;
         }
         else if (comp_symb == "greater")
         {
            return stack_size > count;
         }
         else if (comp_symb == "lesser")
         {
            return stack_size < count;
         }
         else
         {
            return false;
         }
      }
   };

   pattern = R"(spellid::(\d+))";

   if (std::regex_search(str, match, pattern) && match.size() == 2)
   {

      uint32 spell_id = atoi(match[1].str().c_str());
      return ai->HasAura(spell_id, GetTarget());
   }

   return false;
}

std::string HasAuraTrigger::getName()
{
   if (!name.empty())
   {
      return name;
   }

   std::ostringstream ss;
   ss << "has aura with " << getQualifier();
   return ss.str();
}


bool HasNoAuraTrigger::IsActive()
{
    return !ai->HasAura(getName(), GetTarget());
}

bool TankAssistTrigger::IsActive()
{
    if (!AI_VALUE(bool, "has attackers"))
        return false;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    if (!currentTarget)
        return true;

    // do not switch if enemy target
    Unit* enemy = AI_VALUE(Unit*, "enemy player target");
    if (enemy)
    {
        return currentTarget != enemy;
    }

    Unit* tankTarget = AI_VALUE(Unit*, "tank target");
    if (!tankTarget || currentTarget == tankTarget)
        return false;
#ifdef CMANGOS
    return tankTarget->GetVictim() != AI_VALUE(Unit*, "self target");
#endif
#ifdef MANGOS
    return tankTarget->getVictim() != AI_VALUE(Unit*, "self target");
#endif
}

bool IsBehindTargetTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && AI_VALUE2(bool, "behind", "current target");
}

bool IsNotBehindTargetTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    return target && !AI_VALUE2(bool, "behind", "current target");
}

bool IsNotFacingTargetTrigger::IsActive()
{
    return !AI_VALUE2(bool, "facing", "current target");
}

bool HasCcTargetTrigger::IsActive()
{
    uint32 spellid = AI_VALUE2(uint32, "spell id", getName());
    if (spellid && sServerFacade.IsSpellReady(bot, spellid))
    {
        return AI_VALUE2(Unit*, "cc target", getName()) && !AI_VALUE2(Unit*, "current cc target", getName());
    }

    return false;
}

bool NoMovementTrigger::IsActive()
{
	return !AI_VALUE2(bool, "moving", "self target");
}

bool NoPossibleTargetsTrigger::IsActive()
{
    std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");
    return !targets.size();
}

bool PossibleAddsTrigger::IsActive()
{
    return AI_VALUE(bool, "possible adds") && !AI_VALUE(ObjectGuid, "attack target");
}

bool NotDpsTargetActiveTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target)
    {
        if (target->IsPlayer())
        {
            return false;
        }

        if(sServerFacade.IsAlive(target))
        {
            // do not switch if enemy target
            Unit* enemy = AI_VALUE(Unit*, "enemy player target");
            if (enemy)
            {
                return target != enemy;
            }

            Unit* dps = AI_VALUE(Unit*, "dps target");
            if (dps)
            {
                return target != dps;
            }
        }
    }

    return false;
}

bool NotDpsAoeTargetActiveTrigger::IsActive()
{
    Unit* target = AI_VALUE(Unit*, "current target");
    if (target)
    {
        if (target->IsPlayer())
        {
            return false;
        }

        if (sServerFacade.IsAlive(target))
        {
            // do not switch if enemy target
            Unit* enemy = AI_VALUE(Unit*, "enemy player target");
            if (enemy)
            {
                return target != enemy;
            }

            Unit* dps = AI_VALUE(Unit*, "dps aoe target");
            if (dps)
            {
                return target != dps;
            }
        }
    }

    return false;
}

bool IsSwimmingTrigger::IsActive()
{
    return AI_VALUE2(bool, "swimming", "self target");
}

bool HasNearestAddsTrigger::IsActive()
{
    std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "nearest adds");
    return targets.size();
}

bool HasItemForSpellTrigger::IsActive()
{
    std::string spell = getName();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return spellId && AI_VALUE2(Item*, "item for spell", spellId);
}


bool TargetChangedTrigger::IsActive()
{
    Unit* oldTarget = context->GetValue<Unit*>("old target")->Get();
    Unit* target = context->GetValue<Unit*>("current target")->Get();
    return target && oldTarget != target;
}

Value<Unit*>* InterruptEnemyHealerTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("enemy healer target", spell);
}

Value<Unit*>* SnareTargetTrigger::GetTargetValue()
{
    return context->GetValue<Unit*>("snare target", spell);
}

bool StayTimeTrigger::IsActive()
{
    time_t stayTime = AI_VALUE(time_t, "stay time");
    time_t now = time(0);
    return delay && stayTime && now > stayTime + 2 * delay / 1000;
}

bool IsMountedTrigger::IsActive()
{
    return AI_VALUE2(bool, "mounted", "self target");
}

bool CorpseNearTrigger::IsActive()
{
    return bot->GetCorpse() && bot->GetCorpse()->IsWithinDistInMap(bot, CORPSE_RECLAIM_RADIUS, true);
}

bool IsFallingTrigger::IsActive()
{
#ifndef MANGOSBOT_TWO
    return bot->HasMovementFlag(MOVEFLAG_JUMPING);
#else
    return bot->HasMovementFlag(MOVEFLAG_FALLING);
#endif
}

bool IsFallingFarTrigger::IsActive()
{
    return bot->HasMovementFlag(MOVEFLAG_FALLINGFAR);
}

bool HasAreaDebuffTrigger::IsActive()
{
    return AI_VALUE2(bool, "has area debuff", "self target");
}

bool ReturnToStayPositionTrigger::IsActive()
{
    PositionEntry stayPosition = AI_VALUE(PositionMap&, "position")["stay"];
    if (stayPosition.isSet())
    {
        const float distance = bot->GetDistance(stayPosition.x, stayPosition.y, stayPosition.z);
        return distance > ai->GetRange("follow");
    }

    return false;
}

bool ReturnToPullPositionTrigger::IsActive()
{
    PositionEntry pullPosition = AI_VALUE(PositionMap&, "position")["pull"];
    if (pullPosition.isSet())
    {
        const float distance = bot->GetDistance(pullPosition.x, pullPosition.y, pullPosition.z);
        return distance > ai->GetRange("follow");
    }

    return false;
}

bool NoBuffAndComboPointsAvailableTrigger::IsActive()
{
    if (BuffTrigger::IsActive())
    {
        return AI_VALUE2(uint8, "combo", GetComboPointsTargetName()) >= comboPoints;
    }

    return false;
}

bool InPvpTrigger::IsActive()
{
    return ai->IsInPvp();
}

bool InPveTrigger::IsActive()
{
    return ai->IsInPve();
}

bool InRaidFightTrigger::IsActive()
{
    return ai->IsInRaid();
}

bool GreaterBuffOnPartyTrigger::IsActive()
{
    Unit* target = GetTarget();
    return target && bot->IsInGroup(target) && BuffOnPartyTrigger::IsActive() && !ai->HasAura(lowerSpell, target, false, checkIsOwner);
}

bool TargetOfAttacker::IsActive()
{
    return !AI_VALUE(std::list<ObjectGuid>, "attackers targeting me").empty();
}

bool TargetOfAttackerInRange::IsActive()
{
    const Unit* closestAttacker = AI_VALUE(Unit*, "closest attacker targeting me");
    return closestAttacker && bot->GetDistance(closestAttacker, true, DIST_CALC_COMBAT_REACH) <= (distance - sPlayerbotAIConfig.contactDistance);
}

bool TargetOfCastedAuraTypeTrigger::IsActive()
{
    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        // Check against the given creature id
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker)
        {
            const Spell* auraTypeSpell = nullptr;
            const Spell* genericSpell = attacker->GetCurrentSpell(CurrentSpellTypes::CURRENT_GENERIC_SPELL);
            if (genericSpell)
            {
                const SpellEntry* spellInfo = genericSpell->m_spellInfo;
                if (spellInfo)
                {
                    for (int32 i = SpellEffectIndex::EFFECT_INDEX_0; i <= SpellEffectIndex::EFFECT_INDEX_2; i++)
                    {
                        if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA) && (spellInfo->EffectApplyAuraName[i] == auraType))
                        {
                            auraTypeSpell = genericSpell;
                            break;
                        }
                    }
                }
            }

            if (!auraTypeSpell)
            {
                const Spell* channeledSpell = attacker->GetCurrentSpell(CurrentSpellTypes::CURRENT_CHANNELED_SPELL);
                if (channeledSpell)
                {
                    const SpellEntry* spellInfo = channeledSpell->m_spellInfo;
                    if (spellInfo)
                    {
                        for (int32 i = SpellEffectIndex::EFFECT_INDEX_0; i <= SpellEffectIndex::EFFECT_INDEX_2; i++)
                        {
                            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA) && (spellInfo->EffectApplyAuraName[i] == auraType))
                            {
                                auraTypeSpell = channeledSpell;
                                break;
                            }
                        }
                    }
                }
            }

            if (auraTypeSpell)
            {
                Unit* spellTarget = auraTypeSpell->m_targets.getUnitTarget();
                if (spellTarget == bot)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool BuffOnTargetTrigger::IsActive()
{
    const Unit* target = GetTarget();
    return target && target->HasAura(buffID);
}

bool DispelOnTargetTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (target)
    {
        const uint32 dispelMask = GetDispellMask(dispelType);
        const std::vector<Aura*> auras = ai->GetAuras(target);
        for (const Aura* aura : auras)
        {
            const SpellEntry* spellInfo = aura->GetSpellProto();
            if (spellInfo && ((1 << spellInfo->Dispel) & dispelMask))
            {
                return true;
            }
        }
    }

    return false;
}

bool RtscJumpTrigger::IsActive()
{
    return AI_VALUE2(WorldPosition, "RTSC saved location", "jump point") && AI_VALUE2(WorldPosition, "RTSC saved location", "jump");
}

bool SpellTargetTrigger::IsActive()
{
    if (IsSpellReady())
    {
        // Check for assigned targets
        const std::list<ObjectGuid>& possibleTargets = AI_VALUE(std::list<ObjectGuid>, targetsValue);
        if (!possibleTargets.empty())
        {
            for (const ObjectGuid& possibleTargetGuid : possibleTargets)
            {
                if (IsTargetValid(ai->GetUnit(possibleTargetGuid)))
                {
                    return true;
                }
            }
        }
        else
        {
            // Check for the default target
            if (IsTargetValid(GetTarget()))
            {
                return true;
            }
        }
    }

    return false;
}

bool SpellTargetTrigger::IsTargetValid(Unit* target)
{
    return target &&
           ai->IsSafe(target) &&
           (bot == target || sServerFacade.GetDistance2d(bot, target) < sPlayerbotAIConfig.sightDistance) &&
           (bot->IsInGroup(target)) &&
           (!aliveCheck || !target->IsDead()) &&
           (!auraCheck || !ai->HasAura(spell, target));
}

bool SpellTargetTrigger::IsSpellReady()
{
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    return spellId && bot->IsSpellReady(spellId);
}

bool ItemTargetTrigger::IsTargetValid(Unit* target)
{
    if (SpellTargetTrigger::IsTargetValid(target))
    {
        if (itemAuraCheck)
        {
            const uint32 itemId = GetItemId();
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
            if (proto)
            {
                for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
                {
#ifdef MANGOSBOT_ZERO
                    if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
                    if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
#endif
                    {
                        if (proto->Spells[i].SpellId > 0 && ai->HasAura(proto->Spells[i].SpellId, target))
                        {
                            return false;
                        }
                    }
                }

                return true;
            }
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool ItemTargetTrigger::IsSpellReady()
{
    const uint32 itemId = GetItemId();
    if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))
        return false;

    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (proto)
    {
        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
#ifdef MANGOSBOT_ZERO
            if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
            if (proto->Spells[i].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
#endif
            {
                if (proto->Spells[i].SpellId > 0)
                {
                    if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId) ||
                        !sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, itemId))
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    return false;
}

bool AtWarTrigger::IsActive()
{
    ReputationMgr& mgr = bot->GetReputationMgr();

#ifndef MANGOSBOT_ONE
    for (uint32 id = 0; id < sFactionStore.GetNumRows(); ++id)
#else
    for (uint32 id = 0; id < sFactionStore.GetMaxEntry(); ++id)
#endif
    {
#ifndef MANGOSBOT_ONE
        const FactionEntry* factionEntry = sFactionStore.LookupEntry(id);
#else
        const FactionEntry* factionEntry = sFactionStore.LookupEntry<FactionEntry>(id);
#endif

        if (!factionEntry)
            continue;

        FactionState const* repState = mgr.GetState(factionEntry);

        if (!repState)
            continue;

        if (!(repState->Flags & FACTION_FLAG_VISIBLE))
            continue;

        if (repState->Flags & FACTION_FLAG_HIDDEN)
            continue;

        if (repState->Flags & FACTION_FLAG_INVISIBLE_FORCED)
            continue;

        if (repState->Flags & FACTION_FLAG_PEACE_FORCED)
            continue;

        if (!(repState->Flags & FACTION_FLAG_AT_WAR))
            continue;

        if (mgr.GetRank(factionEntry) < REP_HOSTILE)
            continue;

        return true;
    }

    return false;
}
