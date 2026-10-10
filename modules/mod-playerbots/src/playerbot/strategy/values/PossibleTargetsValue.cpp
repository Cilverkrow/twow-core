
#include "playerbot/playerbot.h"
#include "PossibleTargetsValue.h"
#include "PossibleAttackTargetsValue.h"
#include "FreeMoveValues.h"

#include "playerbot/ServerFacade.h"
#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

std::list<ObjectGuid> PossibleTargetsValue::Calculate()
{
    float rangeCheck = range;
    bool shouldIgnoreValidate = false;
    if (!qualifier.empty())
    {
        rangeCheck = Qualified::getMultiQualifierInt(qualifier, 0, ":");
        shouldIgnoreValidate = Qualified::getMultiQualifierInt(qualifier, 1, ":");
    }

    // twow-repo#541 (audit A32): for a bot without a group, the unqualified variants ("possible targets",
    // "possible targets no los", "all targets"; "nearest adds" only if its range equals the sight distance)
    // filter this bot's OWN unfiltered snapshot "possible targets::{<sight>:1}" - the value
    // AttackersValue::AddTargetsOf reads - instead of running the same grid search again. Every snapshot
    // unit must pass the grid search's own check again now (alive, not friendly, CanSeeInWorld, same map
    // instance and within range) and AcceptUnit, so the result is a subset of what the search would return
    // now: only a unit that entered the ring after the snapshot was taken (< 1 s, interval 2) is missing.
    // Grouped bots keep the search below: group members read the snapshot from their own threads
    // (AttackersValue::AddTargetsOf, group travel IsActive on a member's context). The snapshot itself
    // (qualifier set) never enters this block.
    if (sPlayerbotAIConfig.possibleTargetsSharedSearch && qualifier.empty() && !bot->GetGroup())
    {
        int32 const sharedRange = (int32)sPlayerbotAIConfig.sightDistance;
        if (sharedRange > 0 && range == (float)sharedRange)
        {
            const std::vector<std::string> sharedQualifiers = { std::to_string(sharedRange), std::to_string(true) };
            Value<std::list<ObjectGuid>>* shared = context->GetValue<std::list<ObjectGuid>>("possible targets", Qualified::MultiQualify(sharedQualifiers, ":"));
            if (shared)
            {
                const std::list<ObjectGuid> sharedGuids = shared->Get();
                MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck inRange(bot, range);
                std::list<ObjectGuid> results;
                for (const ObjectGuid& guid : sharedGuids)
                {
                    Unit* unit = ai->GetUnit(guid);
                    if (unit && inRange(unit) && AcceptUnit(unit))
                        results.push_back(guid);
                }
                return results;
            }
        }
    }

    std::list<Unit*> targets;
    FindPossibleTargets(bot, targets, rangeCheck);

    std::list<ObjectGuid> results;
    for (std::list<Unit*>::iterator i = targets.begin(); i != targets.end(); ++i)
    {
        Unit* unit = *i;
        if (unit && (shouldIgnoreValidate || AcceptUnit(unit)))
        {
            results.push_back(unit->GetObjectGuid());
        }
    }

    return results;
}

void PossibleTargetsValue::FindUnits(std::list<Unit*> &targets)
{
    FindPossibleTargets(bot, targets, range);
}

bool PossibleTargetsValue::AcceptUnit(Unit* unit)
{
    return IsValid(unit, bot, ignoreLos);
}

void PossibleTargetsValue::FindPossibleTargets(Player* player, std::list<Unit*>& targets, float range)
{
    MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck u_check(player, range);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(targets, u_check);
    Cell::VisitAllObjects(player, searcher, range);
}

bool PossibleTargetsValue::IsFriendly(Unit* target, Player* player)
{
    bool friendly = false;
    if (sServerFacade.IsFriendlyTo(target, player))
    {
        friendly = true;

#ifndef MANGOSBOT_ZERO
        // Check if the target is another player in a duel/arena
        Player* targetPlayer = dynamic_cast<Player*>(target);
        if (targetPlayer)
        {
            // If the target is in an arena with the player and is not on the same team
            if (targetPlayer->InArena() && player->InArena() && (targetPlayer->GetBGTeam() != player->GetBGTeam()))
            {
                friendly = false;
            }
        }
#endif
    }

    return friendly;
}

bool PossibleTargetsValue::IsAttackable(Unit* target, Player* player)
{
    const bool inVehicle = GetBotAI(player) && GetBotAI(player)->IsInVehicle();
    return !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_ATTACKABLE_1) &&
           !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNTARGETABLE) &&
           (inVehicle || !target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE)) &&
           !target->HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION);
}

bool PossibleTargetsValue::IsValid(Unit* target, Player* player, bool ignoreLos)
{
    // If the target is available
    if (target && target->IsInWorld() && (target->GetMapId() == player->GetMapId()))
    {
        // If the target is dead
        if (sServerFacade.UnitIsDead(target))
        {
            return false;
        }

        // If the target is friendly
        if (IsFriendly(target, player))
        {
            return false;
        }

        // If the target can't be attacked
        if (!IsAttackable(target, player))
        {
            return false;
        }

        // Being in combat with *this* target is a reason to know where it is
        // without seeing it. Being in combat at all is not: player->IsInCombat()
        // used to be part of this, which meant a bot fighting anyone could pick
        // out every stealthed player within range.
        bool isInCombatWithTarget = target->GetVictim() == player || 
                                     target->getThreatManager().getThreat(player) > 0.0f;

        if (!ignoreLos && !isInCombatWithTarget)
        {
            if (!target->IsVisibleForOrDetect(player, player->GetCamera().GetBody(), true))
            {
                return false;
            }
        }
        if (!CanFreeMoveValue::CanFreeAttack(GetBotAI(player), target))
            return false;

        return true;
    }

    return false;
}