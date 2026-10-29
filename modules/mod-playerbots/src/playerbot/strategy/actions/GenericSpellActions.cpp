
#include "playerbot/playerbot.h"
#include "GenericActions.h"
#include "UseItemAction.h"
#include "playerbot/SpellStampPolicy.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/CastLosGatePolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RotCastPolicy.h"
#include "playerbot/RotCastTrace.h"
#include "playerbot/NeverPossiblePolicy.h"
#include "playerbot/LosCachePolicy.h"

using namespace ai;

bool CastSpellAction::NeverPossible()
{
    // the switch first (no value read when off); "mount" is a plain CastSpellAction whose name is no spell (isPossible special-cases it)
    if (!ai::never_possible::Applies(sPlayerbotAIConfig.rotationNeverPossibleSkip, bot->GetGUIDLow()) || KeepsUnknownSpell() || spellName == "mount")
        return false;

    // the "spell id" value (refreshed every few seconds), not the spellId member, which stays 0 after a learn without RefreshOnLearn
    return ai::never_possible::Skip(true, false, AI_VALUE2(uint32, "spell id", spellName));
}

CastSpellAction::CastSpellAction(PlayerbotAI* ai, std::string spell)
: Action(ai, spell)
, range(ai->GetRange("spell"))
{
    SetSpellName(spell);

    float spellRange;
    if (ai->GetSpellRange(spell, &spellRange))
    {
        range = spellRange;
    }

    // twow-repo#541 (audit A27): a subclass that replaces the range afterwards (melee, judgement, shouts) no longer matches this.
    rangeFromSpell = range;
}

bool CastSpellAction::Execute(Event& event)
{
    bool executed = false;
    uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;
    if (spellName == "conjure food" || spellName == "conjure water")
    {
        uint32 castId = 0;
        for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
        {
            uint32 spellId = itr->first;

            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                continue;

            std::string namepart = pSpellInfo->SpellName[0];
            strToLower(namepart);

            if (namepart.find(spellName) == std::string::npos)
                continue;

            if (pSpellInfo->Effect[0] != SPELL_EFFECT_CREATE_ITEM)
                continue;

            uint32 itemId = pSpellInfo->EffectItemType[0];
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
            if (!proto)
                continue;

            if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;

            if (pSpellInfo->Id > castId)
                castId = pSpellInfo->Id;
        }

        executed = ai->CastSpell(castId, bot, nullptr, false, &spellDuration);
    }
    else
    {
        Unit* target = GetTarget();
        if (!target)
        {
            sLog.outDebug("%s: CastSpellAction::Execute aborting '%s' - target resolved to null at cast time", bot->GetName(), spellName.c_str());
            RotStart(ai::rot_cast::StartNoTarget);
            return false;
        }

        if (GetTargetName() == "current target" && (!bot->GetCurrentSpell(CURRENT_MELEE_SPELL) && !bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)))
        {
            if (bot->getClass() == CLASS_HUNTER && spellName != "auto shot" && sServerFacade.GetDistance2d(bot, target) > 5.0f)
                ai->CastSpell("auto shot", target);
        }

        executed = ai->CastSpell(spellName, target, nullptr, false, &spellDuration);
    }

    RotStart(executed ? ai::rot_cast::StartOk : ai::rot_cast::StartFail);

    if (executed)
    {
        if (ai->HasCheat(BotCheatMask::attackspeed))
            spellDuration = 1;

        SetDuration(spellDuration);
    }

    return executed;
}

bool CastSpellAction::isPossible()
{
    RefreshSpellIdOnLearn();

    if (spellName == "mount")
    {
        if (!bot->IsMounted() && !bot->IsInCombat())
        {
            return RotEval(true, ai::rot_cast::EvalMountOk);
        }
        if (bot->IsInCombat())
        {
            ai->Unmount();
            return RotEval(false, ai::rot_cast::EvalMountInCombat);
        }
    }

    // twow-repo#602 (AiPlayerbot.Rotation.NeverPossibleSkip): a spell the bot does not know is IMPOSSIBLE at once (self-casts skipped the
    // spellId test below and went into CanCastSpell every time). IMPOSSIBLE as before, so the node's alternatives are still pushed.
    if (NeverPossible())
    {
        ai::never_possible::possible.fetch_add(1, std::memory_order_relaxed);
        return RotEval(false, ai::rot_cast::EvalNoSpellId);
    }

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return RotEval(false, ai::rot_cast::EvalNoTarget);

    bool canReach = false;
    if (spellTarget == bot)
    {
        canReach = true;
    }
    else
    {
        float dist = bot->GetDistance(spellTarget, true, ai->IsRanged(bot) ? DIST_CALC_COMBAT_REACH : DIST_CALC_COMBAT_REACH_WITH_MELEE);
        if (range == ATTACK_DISTANCE) 
        {
            canReach = bot->CanReachWithMeleeAttack(spellTarget);
        }
        else 
        {
            canReach = dist <= (range + sPlayerbotAIConfig.contactDistance);
            if (!spellId && sPlayerbotAIConfig.rotationCastTrace)
                RotEval(false, ai::rot_cast::EvalNoSpellId);   // twow-repo#602 (D0): counted; the refusal stays as pinned by A27
            if (!spellId)
                return false;

            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                return RotEval(false, ai::rot_cast::EvalNoSpellEntry);

            if (range != ATTACK_DISTANCE && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_COMBAT && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_ANYWHERE)
            {
                float max_range, min_range;
                if (ai->GetSpellRange(GetSpellName(), &max_range, &min_range))
                {
                    canReach = dist < max_range && dist >= min_range;
                }
            }
        }
    }

    if(!canReach)
    {
        return RotEval(false, ai::rot_cast::EvalOutOfReach);
    }

    // Check if the spell can be casted
    // twow-repo#602 (AiPlayerbot.Rotation.CastLosGate, CastLosGatePolicy.h): CanCastSpell tolerates the core's LINE_OF_SIGHT verdict
    // under ignoreRange; with the switch the cast is IMPOSSIBLE instead, and a current target that hotfix 8.2 already marked
    // unreachable is dropped (its reach is USELESS from then on). Off = the paths below, unchanged.
    if (ai::cast_los_gate::Applies(sPlayerbotAIConfig.rotationCastLosGate, bot->GetGUIDLow()))
    {
        // twow-repo#602 (AiPlayerbot.Rotation.LosCacheMs, LosCachePolicy.h): a target the cache knows is out of sight is IMPOSSIBLE at
        // once - no Spell, no CheckCast, no VMap query for every alternative of the node; a spell that ignores line of sight (or whose
        // entry is unknown) takes the full check.
        SpellEntry const* const losSpell = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
        bool const ignoresLos = !losSpell || losSpell->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS);
        bool cachedInLos = true;
        bool const fresh = ai->LosCacheGet(spellTarget->GetObjectGuid(), cachedInLos);
        if (ai::los_cache::SkipCast(fresh, true, cachedInLos, ignoresLos))
        {
            ai::los_cache::cached.fetch_add(1, std::memory_order_relaxed);
            return sPlayerbotAIConfig.rotationCastTrace ? RotEval(false, ai::rot_cast::EvalCode(false, uint8(SPELL_FAILED_LINE_OF_SIGHT))) : false;
        }

        SpellCastResult losResult = SPELL_CAST_OK;
        bool losPossible = ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true, false, false, &losResult);
        if (losPossible && losResult == SPELL_FAILED_LINE_OF_SIGHT)
        {
            losPossible = false;
            ai::cast_los_gate::gated.fetch_add(1, std::memory_order_relaxed);
            if (!ignoresLos)
                ai->LosCacheSet(spellTarget->GetObjectGuid(), false);   // twow-repo#602 (Rotation.LosCacheMs): remember the verdict
            if (ai->IsUnreachableTarget(spellTarget->GetObjectGuid()) && AI_VALUE(Unit*, "current target") == spellTarget)
            {
                SET_AI_VALUE(Unit*, "current target", nullptr);
                ai::cast_los_gate::dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return sPlayerbotAIConfig.rotationCastTrace ? RotEval(losPossible, ai::rot_cast::EvalCode(losPossible, uint8(losResult))) : losPossible;
    }

    if (!sPlayerbotAIConfig.rotationCastTrace)
	    return ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true);

    // twow-repo#602 ROT-01 D0: the same check, with the result it reports (checkResult is only written, never read).
    SpellCastResult result = SPELL_CAST_OK;
    bool const possible = ai->CanCastSpell(spellName, spellTarget, 0, nullptr, true, false, false, &result);
    return RotEval(possible, ai::rot_cast::EvalCode(possible, uint8(result)));
}

bool CastSpellAction::RotEval(bool possible, uint16 code)
{
    if (!sPlayerbotAIConfig.rotationCastTrace)
        return possible;

    if (!rotCastSlot)
        rotCastSlot = ai::rot_cast_rt::ActionSlot(getName());
    uint32 const guid = bot->GetGUIDLow();
    ai::rot_cast_rt::RecordEval(bot->getClass(), ai::cohort::InB(guid), rotCastSlot, code);

    // Once per bot and action; also when the line budget is spent (then counted as unknown_dropped).
    if (!rotCastUnknownLogged && ai::rot_cast::IsUnknown(code, uint8(SPELL_FAILED_NOT_KNOWN)))
    {
        rotCastUnknownLogged = true;
        if (ai::rot_cast_rt::TakeUnknownBudget(uint32(time(nullptr))))
            sLog.outBasic("%s", ai::rot_cast::FormatUnknownLine(guid, bot->GetName(), bot->getClass(), bot->GetLevel(),
                ai::cohort::Letter(guid), getName(), spellId, code).c_str());
    }
    return possible;
}

void CastSpellAction::RotStart(uint16 code)
{
    if (!sPlayerbotAIConfig.rotationCastTrace)
        return;

    if (!rotCastSlot)
        rotCastSlot = ai::rot_cast_rt::ActionSlot(getName());
    ai::rot_cast_rt::RecordStart(bot->getClass(), ai::cohort::InB(bot->GetGUIDLow()), rotCastSlot, code);
}

bool CastSpellAction::isUseful()
{
    if (ai->IsInVehicle() && !ai->IsInVehicle(false, false, true))
        return false;

    if(!AI_VALUE2(bool, "spell cast useful", spellName))
        return false;

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return false;

    if (!spellTarget->IsInWorld() || spellTarget->GetMapId() != bot->GetMapId())
        return false;

    return true;
}

NextAction** CastSpellAction::getPrerequisites()
{
    // twow-repo#602 (AiPlayerbot.Rotation.NeverPossibleSkip): no reach action for a spell the bot does not know (it was pushed on every
    // pass and then the cast was IMPOSSIBLE anyway)
    if (NeverPossible())
    {
        ai::never_possible::prereq.fetch_add(1, std::memory_order_relaxed);
        return Action::getPrerequisites();
    }

    // Set the reach action as the cast spell prerequisite when needed
    const std::string reachAction = GetReachActionName();
    if (!reachAction.empty())
    {
        const std::string targetName = GetTargetName();

        // No need for a reach action when target is self
        if (targetName != "self target")
        {
            const std::string spellName = GetSpellName();
            const std::string targetQualifier = GetTargetQualifier();

            // Generate the reach action with qualifiers
            std::vector<std::string> qualifiers = { spellName, targetName };
            if (!targetQualifier.empty())
            {
                qualifiers.push_back(targetQualifier);
            }

            const std::string qualifiersStr = Qualified::MultiQualify(qualifiers, "::");
            return NextAction::merge(NextAction::array(0, new NextAction(reachAction + "::" + qualifiersStr), NULL), Action::getPrerequisites());
        }
    }

    return Action::getPrerequisites();
}

void CastSpellAction::SetSpellName(const std::string& name, std::string spellIDContextName /*= "spell id"*/, bool force)
{
    if (force || spellName != name)
    {
        spellName = name;
        spellId = ai->GetAiObjectContext()->GetValue<uint32>(spellIDContextName, name)->Get();

        float spellRange;
        if (ai->GetSpellRange(spellName, &spellRange))
        {
            range = spellRange;
            rangeFromSpell = range;  // twow-repo#541 (audit A27): name switches (blessing, shoot/throw) keep the range spell-owned
        }
    }
}

// twow-repo#541 (audit A27, AiPlayerbot.CastSpell.RefreshOnLearn, default 0): the spell id was read when the action
// was first created and actions live for the whole session, so a spell, rank or pet spell that came later was
// ignored by isPossible() until the next login. With the switch on, id and range are read again once per change of
// the bot's known-spell stamp (spell map size, level, free talent points; the pet number only for pet spell
// actions). Steady state: a few field reads and one compare, no per-cast spell name lookup. Deliberately not
// SetSpellName(..., true): that would overwrite the ATTACK_DISTANCE/custom range of the melee subclasses.
void CastSpellAction::RefreshSpellIdOnLearn()
{
    if (!sPlayerbotAIConfig.castSpellRefreshOnLearn)
        return;

    const bool tracksPet = SpellStampTracksPet();
    // Pet guid entry = pet number, stable across resummons. Deliberately not Unit::GetPet(): it clears a pet guid
    // it cannot find. Every other spell action leaves the pet out of its stamp.
    const uint32 petNumber = tracksPet ? bot->GetPetGuid().GetEntry() : 0;
    if (ai::spellstamp::DeferWithoutPet(tracksPet, petNumber))
        return;

    const uint64 stamp = ai::spellstamp::Make(bot->GetSpellMap().size(), bot->GetLevel(), bot->GetFreeTalentPoints(), petNumber);
    if (stamp == knownSpellStamp)
        return;

    knownSpellStamp = stamp;

    // The 'spell id' value may be up to 5 s old and predate the learn: recalculate it once now (own bot's value).
    Value<uint32>* idValue = ai->GetAiObjectContext()->GetValue<uint32>("spell id", spellName);
    if (!idValue)
        return;

    idValue->Reset();
    const uint32 oldId = spellId;
    spellId = idValue->Get();

    const float oldRange = range;
    float spellRange;
    if (ai::spellstamp::RangeFollowsSpell(range, rangeFromSpell, ATTACK_DISTANCE) && ai->GetSpellRange(spellName, &spellRange))
    {
        range = spellRange;
        rangeFromSpell = range;
    }

    if (spellId != oldId)
    {
        sLog.outBasic("[SpellIdRefresh] bot=%u level=%u spell=\"%s\" id=%u->%u range=%.1f->%.1f",
            bot->GetGUIDLow(), bot->GetLevel(), spellName.c_str(), oldId, spellId, oldRange, range);
    }
    else if (range != oldRange)
    {
        sLog.outBasic("[SpellRangeRefresh] bot=%u level=%u spell=\"%s\" id=%u range=%.1f->%.1f",
            bot->GetGUIDLow(), bot->GetLevel(), spellName.c_str(), spellId, oldRange, range);
    }
}

Unit* CastSpellAction::GetTarget()
{
    std::string targetName = GetTargetName();
    std::string targetNameQualifier = GetTargetQualifier();
    return targetNameQualifier.empty() ? AI_VALUE(Unit*, targetName) : AI_VALUE2(Unit*, targetName, targetNameQualifier);
}

bool CastPetSpellAction::isPossible()
{
    RefreshSpellIdOnLearn();

    Unit* spellTarget = GetTarget();
    if (!spellTarget)
        return RotEval(false, ai::rot_cast::EvalNoTarget);

    uint16 why = ai::rot_cast::EvalPetMissing;   // twow-repo#602 (D0): the [RotCast] reason when this returns false
    Unit* pet = AI_VALUE(Unit*, "pet target");
    if (pet && ai->IsSafe(pet))
    {
        why = ai::rot_cast::EvalPetNotReady;
        const uint32& spellId = GetSpellID();
        if (pet->HasSpell(spellId) && pet->IsSpellReady(spellId))
        {
            why = ai::rot_cast::EvalPetTooFar;
            // Check if the pet is not too far from the owner
            if (bot->GetDistance(pet) <= sPlayerbotAIConfig.sightDistance)
            {
                why = ai::rot_cast::EvalPetCannotReach;
                bool canReach = false;
                const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
                if (pSpellInfo)
                {
                    const float dist = pet->GetDistance(spellTarget, true, DIST_CALC_COMBAT_REACH);
                    canReach = dist <= (range + sPlayerbotAIConfig.contactDistance);

                    if (pSpellInfo->rangeIndex != SPELL_RANGE_IDX_COMBAT && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_SELF_ONLY && pSpellInfo->rangeIndex != SPELL_RANGE_IDX_ANYWHERE)
                    {
                        float max_range, min_range;
                        if (ai->GetSpellRange(GetSpellName(), &max_range, &min_range))
                        {
                            canReach = dist < max_range&& dist >= min_range;
                        }
                    }
                }

                if (canReach)
                {
                    if (!sPlayerbotAIConfig.rotationCastTrace)
                        return ai->CanCastSpell(spellId, spellTarget, 0, true);

                    SpellCastResult result = SPELL_CAST_OK;
                    bool const possible = ai->CanCastSpell(spellId, spellTarget, 0, true, nullptr, false, false, false, &result);
                    return RotEval(possible, ai::rot_cast::EvalCode(possible, uint8(result)));
                }
            }
        }
    }

    return RotEval(false, why);
}

bool CastAuraSpellAction::isUseful()
{
    return CastSpellAction::isUseful() && !ai->HasAura(GetSpellName(), GetTarget(), false, isOwner);
}

bool CastMeleeAoeSpellAction::isUseful()
{
    return CastSpellAction::isUseful() && sServerFacade.IsDistanceLessOrEqualThan(AI_VALUE2(float, "distance", GetTargetName()), radius);
}

bool CastEnchantItemAction::isPossible()
{
    if (!CastSpellAction::isPossible())
        return false;

    return GetSpellID() && AI_VALUE2(Item*, "item for spell", GetSpellID());
}

bool CastAoeHealSpellAction::isUseful()
{
    return CastSpellAction::isUseful();
}

bool HealHotPartyMemberAction::isUseful()
{
    return HealPartyMemberAction::isUseful() && !ai->HasAura(GetSpellName(), GetTarget());
}

bool CastVehicleSpellAction::isPossible()
{
    return ai->CanCastVehicleSpell(GetSpellID(), GetTarget());
}

bool CastVehicleSpellAction::isUseful()
{
    return ai->IsInVehicle(false, true);
}

bool CastVehicleSpellAction::Execute(Event& event)
{
    return ai->CastVehicleSpell(GetSpellID(), GetTarget(), speed, needTurn);
}

bool CastFrozenDeathboltAction::isPossible()
{
    Unit* target = GetTarget();

    if (!target)
        return false;

    if (target->GetDistance(bot) > range)
        return false;

    return CastVehicleSpellAction::isPossible();
}

bool CastDevourHumanoidAction::isPossible()
{
    Unit* target = GetTarget();

    if (!target)
        return false;

    if (target->GetDistance(bot) > range)
        return false;

    return CastVehicleSpellAction::isPossible();
}

bool CastShootAction::isPossible()
{
    // Check if the bot has a ranged weapon equipped and has ammo
    UpdateWeaponInfo();
    if (rangedWeapon && !needsAmmo)
    {
        // Check if the target exist and it can be shot
        Unit* target = GetTarget();
        if (target && sServerFacade.IsWithinLOSInMap(bot, target))
        {
            return CastSpellAction::isPossible();
        }
    }

    return false;
}

bool CastShootAction::Execute(Event& event)
{
    bool succeeded = false;

    UpdateWeaponInfo();
    if (rangedWeapon && !needsAmmo)
    {
        // Prevent calling the shoot spell when already active
        Spell* autoRepeatSpell = ai->GetBot()->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
        if (autoRepeatSpell && (autoRepeatSpell->m_spellInfo->Id == GetSpellID()))
        {
            succeeded = true;
        }
        else if (CastSpellAction::Execute(event))
        {
            succeeded = true;
        }

        if (succeeded)
        {
            SetDuration(weaponDelay);
        }
    }

    return succeeded;
}

void CastShootAction::UpdateWeaponInfo()
{
    // Check if we have a new ranged weapon equipped
    const Item* equippedWeapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
    if (equippedWeapon)
    {
        if (equippedWeapon != rangedWeapon)
        {
            std::string spellName = "shoot";
            bool isRangedWeapon = false;

#ifdef MANGOSBOT_ZERO
            needsAmmo = true;
#endif

            const ItemPrototype* itemPrototype = equippedWeapon->GetProto();
            switch (itemPrototype->SubClass)
            {
                case ITEM_SUBCLASS_WEAPON_GUN:
                {
                    isRangedWeapon = true;
#ifdef MANGOSBOT_ZERO
                    spellName += " gun";
#endif
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_BOW:
                {
                    isRangedWeapon = true;
#ifdef MANGOSBOT_ZERO
                    spellName += " bow";
#endif
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                {
                    isRangedWeapon = true;
#ifdef MANGOSBOT_ZERO
                    spellName += " crossbow";
#endif
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_WAND:
                {
                    isRangedWeapon = true;
#ifdef MANGOSBOT_ZERO
                    needsAmmo = false;
#endif
                    break;
                }
                case ITEM_SUBCLASS_WEAPON_THROWN:
                {
                    isRangedWeapon = true;
                    spellName = "throw";
                    break;
                }

                default: break;
            }

            // Set the new weapon parameters
            if (isRangedWeapon)
            {
                SetSpellName(spellName);
                rangedWeapon = equippedWeapon;
                weaponDelay = itemPrototype->Delay + sPlayerbotAIConfig.globalCoolDown;
            }
        }

        // Check the ammunition
#ifdef MANGOSBOT_ZERO
        needsAmmo = (GetSpellName() != "shoot") ? (AI_VALUE2(uint32, "item count", "ammo") <= 0) : false;
#endif
    }
    else
    {
        rangedWeapon = nullptr;
    }
}

bool RemoveBuffAction::isUseful()
{
    return ai->HasAura(name, bot);
}

bool RemoveBuffAction::Execute(Event& event)
{
    ai->RemoveAura(name);
    return !ai->HasAura(name, bot);
}

bool InterruptCurrentSpellAction::isUseful()
{
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* currentSpell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (currentSpell && currentSpell->CanBeInterrupted())
            return true;
    }
    return false;
}

bool InterruptCurrentSpellAction::Execute(Event& event)
{
    bool interrupted = false;
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* currentSpell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (currentSpell && currentSpell->CanBeInterrupted())
        {
            bot->InterruptSpell((CurrentSpellTypes)type);
            ai->SpellInterrupted(currentSpell->m_spellInfo->Id);
            interrupted = true;
        }
    }
    return interrupted;
}

Unit* CastSpellTargetAction::GetTarget()
{
    // Check for assigned targets
    const std::list<ObjectGuid>& possibleTargets = AI_VALUE(std::list<ObjectGuid>, targetsValue);
    if (!possibleTargets.empty())
    {
        for (const ObjectGuid& possibleTargetGuid : possibleTargets)
        {
            Unit* possibleTarget = ai->GetUnit(possibleTargetGuid);
            if (IsTargetValid(possibleTarget))
            {
                return possibleTarget;
            }
        }
    }
    else
    {
        // Check for the default target
        Unit* possibleTarget = CastSpellAction::GetTarget();
        if (IsTargetValid(possibleTarget))
        {
            return possibleTarget;
        }
    }

    return nullptr;
}

bool CastSpellTargetAction::IsTargetValid(Unit* target)
{
    return target &&
           ai->IsSafe(target) &&
           (bot == target || sServerFacade.GetDistance2d(bot, target) < sPlayerbotAIConfig.sightDistance) &&
           bot->IsInGroup(target) &&
           (!aliveCheck || !target->IsDead()) &&
           (!auraCheck || !ai->HasAura(GetSpellID(), target));
}

bool CastItemTargetAction::IsTargetValid(Unit* target)
{
    if (CastSpellTargetAction::IsTargetValid(target))
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

bool CastItemTargetAction::isUseful()
{
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(GetItemId());
    if (proto)
    {
        std::set<uint32>& skipSpells = AI_VALUE(std::set<uint32>&, "skip spells list");
        if (!skipSpells.empty())
        {
            for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            {
                const _Spell& spellData = proto->Spells[i];
                if (spellData.SpellId)
                {
                    if (skipSpells.find(spellData.SpellId) != skipSpells.end())
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

bool CastItemTargetAction::isPossible()
{
    uint32 itemId = GetItemId();
    if (!itemId)
        return false;

    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    if (HasSpellCooldown(itemId))
        return false;

    if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))
        return false;

    uint32 spellCount = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
#ifdef MANGOSBOT_ZERO
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        spellCount++;
    }

    return spellCount;
}

bool CastItemTargetAction::Execute(Event& event)
{
    uint32 itemId = GetItemId();
    Unit* target = GetTarget();
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    Item* item = nullptr;

    if (!ai->HasCheat(BotCheatMask::item)) //If bot has no item cheat it needs an item to cast.
    {
        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", chat->formatQItem(itemId));

        if (items.empty())
            return false;

        item = items.front();
    }

    SpellCastTargets targets;
    if (target)
    {
        targets.setUnitTarget(target);
        targets.setDestination(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
    }
    else
        targets.m_targetMask = TARGET_FLAG_SELF;

    // use triggered flag only for items with many spell casts and for not first cast
    int count = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
#ifdef MANGOSBOT_ZERO
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
#else
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
#endif
            continue;

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        if (spellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
            targets.m_targetMask = TARGET_FLAG_DEST_LOCATION;

        BotUseItemSpell* spell = new BotUseItemSpell(bot, spellInfo, (count > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);

        Item* tItem = nullptr;

        if (item)
        {
            spell->SetCastItem(item);
            item->SetUsedInSpell(true);
        }

        spell->m_clientCast = true;

        bool result = (spell->ForceSpellStart(&targets) == SPELL_CAST_OK);

        if (!result)
            return false;

        if (ai->HasCheat(BotCheatMask::item))
        {
            if (!HasSpellCooldown(itemId))
            {
                bot->RemoveSpellCooldown(*spellInfo, false);
                bot->AddCooldown(*spellInfo, proto, false);
            }
        }

        ++count;
    }

    return count;
}

bool CastItemTargetAction::HasSpellCooldown(uint32 itemId)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);

    if (!proto)
        return false;

    uint32 spellId = 0;
    for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
        {
            continue;
        }

        if (proto->Spells[i].SpellId > 0)
        {
            if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId))
                return true;

            if (!sServerFacade.IsSpellReady(bot, proto->Spells[i].SpellId, itemId))
                return true;
        }
    }

    return false;
}
