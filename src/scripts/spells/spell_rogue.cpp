#include "scriptPCH.h"
#include "ThreatManager.h"
#include "DBCStores.h"

namespace
{
enum RogueSpells
{
    SPELL_ROGUE_BLACKJACK_R1                  = 14076,
    SPELL_ROGUE_BLACKJACK_R2                  = 14094,
    SPELL_ROGUE_BLACKJACK_DEBUFF_R1           = 52532,
    SPELL_ROGUE_BLACKJACK_DEBUFF_R2           = 52533,
    SPELL_ROGUE_RELENTLESS_STRIKES            = 14181,
    SPELL_ROGUE_TASTE_FOR_BLOOD_R1            = 14174,
    SPELL_ROGUE_TASTE_FOR_BLOOD_R2            = 14175,
    SPELL_ROGUE_TASTE_FOR_BLOOD_TRIGGER_R1    = 52528,
    SPELL_ROGUE_TASTE_FOR_BLOOD_TRIGGER_R2    = 52529,
    SPELL_ROGUE_VANISH_PURGE                  = 18461,
    SPELL_ROGUE_BLADE_FLURRY_TRIGGER          = 22482,
    SPELL_ROGUE_CLEAN_ESCAPE_TRIGGER          = 23583,
    SPELL_ROGUE_HONOR_AMONG_THIEVES_TRIGGER   = 52513,
    SPELL_ROGUE_EXPLOIT_VULNERABILITY         = 52539,
    SPELL_ROGUE_CLOAKED_IN_SHADOWS_R1         = 52707,
    SPELL_ROGUE_CLOAKED_IN_SHADOWS_R2         = 52709,
    SPELL_ROGUE_SHADOW_OF_DEATH               = 52710,
    SPELL_ROGUE_SHADOW_OF_DEATH_DAMAGE        = 52711,

    // twow-repo#367 rogue tank (bots only in release train 7; IDs 90140-90149, 90150-90199
    // reserved for the owner's rogue talent line).
    SPELL_ROGUE_SPIT_SPLASH                   = 90141,
    SPELL_ROGUE_SHADOW_DANCE_DODGE_BUFF       = 90145,
    SPELL_ROGUE_SHADOW_DANCE_PARRY_BUFF       = 90146,
};

// #367 D-1/D-7/D-8: Agitating Poison ranks by caster level (rank V = the existing 45613).
struct AgitatingPoisonRank
{
    uint32 minLevel;
    int32 threat;
};

AgitatingPoisonRank const AGITATING_POISON_RANKS[] =
{
    { 60, 395 },
    { 50, 335 },
    { 40, 275 },
    { 30, 210 },
    {  0, 150 },
};

int32 const AGITATING_POISON_RANK_V_THREAT = 395;

int32 GetAgitatingPoisonThreat(Unit const* caster)
{
    uint32 const level = caster ? caster->GetLevel() : 0;
    for (AgitatingPoisonRank const& rank : AGITATING_POISON_RANKS)
        if (level >= rank.minLevel)
            return rank.threat;

    return AGITATING_POISON_RANK_V_THREAT;
}

// #367 D-2: Spit also taunts up to two more enemies around the main target.
uint32 const SPIT_EXTRA_TARGETS  = 2;
float const  SPIT_SPLASH_RADIUS  = 8.0f;

template <class T>
SpellScript* GetSpellScript(SpellEntry const*)
{
    return new T();
}

template <class T>
AuraScript* GetAuraScript(SpellEntry const*)
{
    return new T();
}

void RegisterSpellScript(char const* name, SpellScript* (*getter)(SpellEntry const*))
{
    Script* script = new Script;
    script->Name = name;
    script->GetSpellScript = getter;
    script->RegisterSelf();
}

void RegisterAuraScript(char const* name, AuraScript* (*getter)(SpellEntry const*))
{
    Script* script = new Script;
    script->Name = name;
    script->GetAuraScript = getter;
    script->RegisterSelf();
}

void RegisterSpellAndAuraScript(char const* name, SpellScript* (*spellGetter)(SpellEntry const*), AuraScript* (*auraGetter)(SpellEntry const*))
{
    Script* script = new Script;
    script->Name = name;
    script->GetSpellScript = spellGetter;
    script->GetAuraScript = auraGetter;
    script->RegisterSelf();
}

bool IsRoguePoison(SpellEntry const* spellInfo)
{
    return spellInfo && spellInfo->IsFitToFamily<SPELLFAMILY_ROGUE,
        CF_ROGUE_INSTANT_POISON, CF_ROGUE_CRIPPLING_POISON, CF_ROGUE_MIND_NUMBING_POISON,
        CF_ROGUE_DEADLY_POISON, CF_ROGUE_WOUND_POISON>();
}

uint32 GetBlackjackDebuff(Unit const* caster)
{
    if (!caster)
        return 0;

    if (caster->HasAura(SPELL_ROGUE_BLACKJACK_R2))
        return SPELL_ROGUE_BLACKJACK_DEBUFF_R2;

    if (caster->HasAura(SPELL_ROGUE_BLACKJACK_R1))
        return SPELL_ROGUE_BLACKJACK_DEBUFF_R1;

    return 0;
}

bool IsSapOrBlind(SpellEntry const* spellInfo)
{
    return spellInfo && spellInfo->IsFitToFamily<SPELLFAMILY_ROGUE, CF_ROGUE_SAP, CF_ROGUE_BLIND>();
}

float GetMeleeAttackPowerAgainst(Unit const* caster, Unit const* target)
{
    float attackPower = caster->GetTotalAttackPowerValue(BASE_ATTACK);
    if (target)
        attackPower += caster->GetTotalAuraModifierByMiscMask(SPELL_AURA_MOD_MELEE_ATTACK_POWER_VERSUS, target->GetCreatureTypeMask());

    return attackPower;
}

void CastBlackjackDebuff(Unit* caster, Unit* target)
{
    if (!caster || !target || !target->IsAlive())
        return;

    if (uint32 debuff = GetBlackjackDebuff(caster))
        caster->CastSpell(target, debuff, true);
}

void CastRoguePoisonFromWeapon(Player* player, Unit* target, WeaponAttackType attackType)
{
    Item* weapon = player->GetWeaponForAttack(attackType, true, true);
    if (!weapon)
        return;

    uint32 enchantId = weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
    SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
    if (!enchant)
        return;

    for (int s = 0; s < 3; ++s)
    {
        if (enchant->type[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
            continue;

        uint32 procSpellId = enchant->spellid[s];
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(procSpellId);
        if (!IsRoguePoison(spellInfo))
            continue;

        if (spellInfo->IsPositiveSpell())
            player->CastSpell(player, procSpellId, true, weapon);
        else
            player->CastSpell(target, procSpellId, true, weapon);

        uint32 charges = weapon->GetEnchantmentCharges(TEMP_ENCHANTMENT_SLOT);
        if (charges > 1)
            weapon->SetEnchantmentCharges(TEMP_ENCHANTMENT_SLOT, charges - 1);
        else if (charges == 1)
        {
            player->ApplyEnchantment(weapon, TEMP_ENCHANTMENT_SLOT, false);
            weapon->ClearEnchantment(TEMP_ENCHANTMENT_SLOT);
        }
        break;
    }
}

struct spell_rogue_eviscerate : public SpellScript
{
    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        if (player)
            if (uint32 combo = player->GetComboPoints())
                damage += int32(GetMeleeAttackPowerAgainst(player, spell->GetUnitTarget()) * combo * 0.03f);
    }
};

struct spell_rogue_surprise_attack : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        if (!spell->m_casterUnit || spell->m_targets.getUnitTargetGuid() == spell->m_casterUnit->GetReactiveTarget(REACTIVE_ROGUE_DODGE))
            return SPELL_CAST_OK;

        return SPELL_FAILED_BAD_TARGETS;
    }

    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return;

        damage += int32(player->GetTotalAttackPowerValue(BASE_ATTACK) * 0.25f);
        player->ModifyAuraState(AURA_STATE_TARGET_DODGED, false);
    }
};

struct spell_rogue_noxious_assault : public SpellScript
{
    void OnEffectDamageCalculate(Spell* spell, SpellEffectIndex /*effIdx*/, float& damage) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return;

        damage += player->GetTotalAttackPowerValue(BASE_ATTACK) * 0.3f;
    }

    void OnAfterHit(Spell* spell) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        Unit* target = spell->GetUnitTarget();
        if (!player || !target || !target->IsAlive())
            return;

        CastRoguePoisonFromWeapon(player, target, BASE_ATTACK);
        CastRoguePoisonFromWeapon(player, target, OFF_ATTACK);
    }
};

struct spell_rogue_mark_for_death : public SpellScript
{
    void OnHit(Spell* spell, SpellMissInfo missInfo) const override
    {
        if (missInfo != SPELL_MISS_NONE || !spell->m_casterUnit)
            return;

        spell->m_casterUnit->CastSpell(spell->m_casterUnit, SPELL_ROGUE_EXPLOIT_VULNERABILITY, true);
    }
};

struct spell_rogue_exploit_vulnerability : public AuraScript
{
    int32 OnAuraValueCalculate(Aura* /*aura*/, Unit* caster, Unit* /*target*/, SpellEntry const* /*spellProto*/, SpellEffectIndex effIdx, Item* /*castItem*/, int32 value) override
    {
        if (!caster)
            return value;

        float const attackPower = caster->GetTotalAttackPowerValue(BASE_ATTACK);

        switch (effIdx)
        {
            case EFFECT_INDEX_0:
                return int32(attackPower * 0.18f);
            case EFFECT_INDEX_1:
            case EFFECT_INDEX_2:
                return int32(attackPower * 0.30f);
            default:
                return value;
        }
    }
};

struct spell_rogue_blackjack : public SpellScript, public AuraScript
{
    void OnPrepareProcFlags(Spell* spell, bool& canTrigger, uint32& procAttacker, uint32& procVictim) const override
    {
        if (!IsSapOrBlind(spell->m_spellInfo))
            return;

        canTrigger = true;
        procAttacker = PROC_FLAG_DEAL_HARMFUL_ABILITY;
        procVictim = PROC_FLAG_NONE;
    }

    void OnHit(Spell* spell, SpellMissInfo missInfo) const override
    {
        if (missInfo != SPELL_MISS_RESIST && missInfo != SPELL_MISS_IMMUNE && missInfo != SPELL_MISS_IMMUNE2)
            return;

        if (!IsSapOrBlind(spell->m_spellInfo))
            return;

        CastBlackjackDebuff(spell->m_casterUnit, spell->GetUnitTarget());
    }

    void OnBeforeProc(Spell* spell, Unit* /*target*/, SpellMissInfo /*missInfo*/, uint32& procAttacker, uint32& procVictim, uint32& procEx, bool& /*triggerWeaponProcs*/) const override
    {
        if (!IsSapOrBlind(spell->m_spellInfo))
            return;

        procAttacker = PROC_FLAG_NONE;
        procVictim = PROC_FLAG_NONE;
        procEx = PROC_EX_NONE;
    }

    void OnAfterApply(Aura* aura, bool apply) override
    {
        if (apply || !IsSapOrBlind(aura->GetSpellProto()))
            return;

        AuraRemoveMode const removeMode = aura->GetRemoveMode();
        if (removeMode != AURA_REMOVE_BY_EXPIRE && removeMode != AURA_REMOVE_BY_DEFAULT && removeMode != AURA_REMOVE_BY_CANCEL)
            return;

        CastBlackjackDebuff(aura->GetCaster(), aura->GetTarget());
    }
};

struct spell_rogue_preparation : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return false;

        SpellCooldowns cooldowns = player->GetSpellCooldownMap();
        for (const auto& cooldown : cooldowns)
        {
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(cooldown.first);
            if (spellInfo && spellInfo->SpellFamilyName == SPELLFAMILY_ROGUE &&
                    spellInfo->Id != spell->m_spellInfo->Id && spellInfo->GetRecoveryTime() > 0)
                player->RemoveSpellCooldown(cooldown.first, true);
        }

        return false;
    }
};

struct spell_rogue_deadly_throw_poison : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return true;

        Player* player = spell->m_caster->ToPlayer();
        Unit* target = spell->GetUnitTarget();
        if (!player || !target || target == player)
            return false;

        CastRoguePoisonFromWeapon(player, target, OFF_ATTACK);
        return false;
    }
};

struct spell_rogue_vanish : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (spell->m_spellInfo->Effect[effIdx] == SPELL_EFFECT_SANCTUARY)
        {
            Unit* target = spell->GetUnitTarget();
            if (!target)
                return false;

            bool noGuards = true;

            target->InterruptSpellsCastedOnMe(true);
            target->InterruptAttacksOnMe(0.0f, true);
            target->m_lastSanctuaryTime = WorldTimer::getMSTime();
            target->CombatStop();

            HostileReference* reference = target->GetHostileRefManager().getFirst();
            while (reference)
            {
                HostileReference* nextReference = reference->next();
                if (!reference->getSource()->getOwner()->IsContestedGuard())
                {
                    reference->removeReference();
                    delete reference;
                }
                else
                    noGuards = false;

                reference = nextReference;
            }

            if (noGuards && target->IsPlayer())
                static_cast<Player*>(target)->SetCannotBeDetectedTimer(1000);

            spell->AddExecuteLogTarget(effIdx, target->GetObjectGuid());
            return false;
        }

        if (spell->m_spellInfo->Effect[effIdx] != SPELL_EFFECT_TRIGGER_SPELL ||
                spell->m_spellInfo->EffectTriggerSpell[effIdx] != SPELL_ROGUE_VANISH_PURGE)
            return true;

        Unit* target = spell->GetUnitTarget();
        if (!target)
            return false;

        target->RemoveSpellsCausingAura(SPELL_AURA_MOD_ROOT);
        target->RemoveSpellsCausingAura(SPELL_AURA_MOD_DECREASE_SPEED);
        target->RemoveSpellsCausingAura(SPELL_AURA_MOD_STALKED);

        if (Player* player = target->ToPlayer())
            player->CastHighestStealthRank();

        return false;
    }
};

struct spell_rogue_honor_among_thieves : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* /*owner*/, Unit* victim, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 cooldown) override
    {
        if (!victim || !victim->IsAlive())
            return SPELL_AURA_PROC_FAILED;

        Unit* auraCaster = aura->GetCaster();
        if (!auraCaster || !auraCaster->IsAlive())
            return SPELL_AURA_PROC_FAILED;

        if (cooldown && auraCaster->HasSpellCooldown(SPELL_ROGUE_HONOR_AMONG_THIEVES_TRIGGER))
            return SPELL_AURA_PROC_FAILED;

        int32 comboPoints = aura->GetModifier()->m_amount;
        auraCaster->CastCustomSpell(auraCaster, SPELL_ROGUE_HONOR_AMONG_THIEVES_TRIGGER, &comboPoints, nullptr, nullptr, true, nullptr, aura);

        if (cooldown)
            auraCaster->AddSpellCooldown(SPELL_ROGUE_HONOR_AMONG_THIEVES_TRIGGER, 0, time(nullptr) + cooldown);

        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_clean_escape : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* procSpell, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!victim || !victim->IsAlive() || !procSpell || procSpell->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_NONE)
            return SPELL_AURA_PROC_FAILED;

        owner->CastSpell(victim, SPELL_ROGUE_CLEAN_ESCAPE_TRIGGER, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_blade_flurry : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 damage, int32 /*originalAmount*/, Aura* aura, SpellEntry const* procSpell, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_1)
            return SPELL_AURA_PROC_CANT_TRIGGER;

        if (!victim || !victim->IsAlive() || (procSpell && procSpell->Id == SPELL_ROGUE_BLADE_FLURRY_TRIGGER))
            return SPELL_AURA_PROC_FAILED;

        Unit* target = owner->SelectRandomUnfriendlyTarget(victim, 5.0f, false, true, true);
        if (!target)
            return SPELL_AURA_PROC_FAILED;

        int32 basepoints = damage * 100 / owner->CalcArmorReducedDamage(victim, 100);
        owner->CastCustomSpell(target, SPELL_ROGUE_BLADE_FLURRY_TRIGGER, &basepoints, nullptr, nullptr, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_improved_ambush : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* /*victim*/, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        int32 energy = aura->GetModifier()->m_amount;
        owner->CastCustomSpell(owner, aura->GetSpellProto()->EffectTriggerSpell[aura->GetEffIndex()], &energy, nullptr, nullptr, true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_setup : public AuraScript
{
    std::optional<SpellProcEventTriggerCheck> OnCheckProc(Unit const* owner, Unit* victim, SpellAuraHolder* holder, SpellEntry const* /*procSpell*/, uint32 procFlag, uint32 procExtra, WeaponAttackType /*attType*/, bool isVictim) override
    {
        if (!owner || !owner->IsPlayer() || !victim || !victim->IsAlive() || !isVictim)
            return SPELL_PROC_TRIGGER_FAILED;

        if (!(procFlag & (PROC_FLAG_TAKE_MELEE_SWING | PROC_FLAG_TAKE_MELEE_ABILITY | PROC_FLAG_TAKE_HARMFUL_SPELL)))
            return SPELL_PROC_TRIGGER_FAILED;

        if (!(procExtra & (PROC_EX_DODGE | PROC_EX_RESIST)))
            return SPELL_PROC_TRIGGER_FAILED;

        float chance = holder->GetSpellProto()->procChance;
        if (Player* modOwner = owner->GetSpellModOwner())
        {
            modOwner->ApplySpellMod(holder->GetId(), SPELLMOD_CHANCE_OF_SUCCESS, chance);
            if (modOwner->HasOption(PLAYER_CHEAT_ALWAYS_PROC))
                return SPELL_PROC_TRIGGER_OK;
        }

        return roll_chance_f(chance) ? SPELL_PROC_TRIGGER_OK : SPELL_PROC_TRIGGER_ROLL_FAILED;
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 /*damage*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        if (!owner || !victim || !victim->IsAlive())
            return SPELL_AURA_PROC_FAILED;

        owner->CastSpell(victim, aura->GetSpellProto()->EffectTriggerSpell[aura->GetEffIndex()], true, nullptr, aura);
        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_rupture_spell : public SpellScript
{
    mutable bool m_triggeredTasteForBlood = false;

    void OnCast(Spell* spell) const override
    {
        if (m_triggeredTasteForBlood)
            return;

        Player* player = spell->m_caster->ToPlayer();
        if (!player)
            return;

        uint32 triggerSpellId = 0;
        uint32 bonusPerComboPoint = 0;
        if (player->HasAura(SPELL_ROGUE_TASTE_FOR_BLOOD_R2))
        {
            triggerSpellId = SPELL_ROGUE_TASTE_FOR_BLOOD_TRIGGER_R2;
            bonusPerComboPoint = 2;
        }
        else if (player->HasAura(SPELL_ROGUE_TASTE_FOR_BLOOD_R1))
        {
            triggerSpellId = SPELL_ROGUE_TASTE_FOR_BLOOD_TRIGGER_R1;
            bonusPerComboPoint = 1;
        }

        if (!triggerSpellId)
            return;

        uint8 const comboPoints = player->GetComboPoints();
        if (!comboPoints)
            return;

        Unit* target = spell->GetUnitTarget();
        int32 duration = spell->m_spellInfo->CalculateDuration(player, target, nullptr);
        if (duration <= 0)
            return;

        int32 bonus = int32(comboPoints * bonusPerComboPoint);
        if (SpellAuraHolder* holder = player->AddAura(triggerSpellId, ADD_AURA_POSITIVE, player, &bonus))
        {
            holder->SetAuraMaxDuration(duration);
            holder->SetAuraDuration(duration);
            holder->UpdateAuraDuration();
        }

        m_triggeredTasteForBlood = true;
    }
};

struct spell_rogue_rupture_aura : public AuraScript
{
    int32 OnAuraValueCalculate(Aura* /*aura*/, Unit* caster, Unit* target, SpellEntry const* spellProto, SpellEffectIndex effIdx, Item* /*castItem*/, int32 value) override
    {
        if (!spellProto || spellProto->EffectApplyAuraName[effIdx] != SPELL_AURA_PERIODIC_DAMAGE || !caster || !caster->IsPlayer())
            return value;

        uint8 comboPoints = caster->ToPlayer()->GetComboPoints();
        if (comboPoints > 3)
            comboPoints = 3;

        return value + int32(GetMeleeAttackPowerAgainst(caster, target) * comboPoints / 100);
    }
};

struct spell_rogue_cloaked_in_shadows : public AuraScript
{
    int32 OnAuraValueCalculate(Aura* /*aura*/, Unit* caster, Unit* /*target*/, SpellEntry const* /*spellProto*/, SpellEffectIndex effIdx, Item* /*castItem*/, int32 value) override
    {
        if (!caster || effIdx != EFFECT_INDEX_0)
            return value;

        Aura* talent = caster->GetAura(SPELL_ROGUE_CLOAKED_IN_SHADOWS_R2, EFFECT_INDEX_0);
        if (!talent)
            talent = caster->GetAura(SPELL_ROGUE_CLOAKED_IN_SHADOWS_R1, EFFECT_INDEX_0);
        if (!talent)
            return value;

        return int32(caster->GetMaxHealth() * talent->GetModifier()->m_amount / 100);
    }
};

struct spell_rogue_shadow_of_death : public AuraScript
{
    uint8 m_comboPoints = 0;
    int32 m_maxDamage = 0;
    int32 m_accumulatedDamage = 0;
    bool m_reachedCapacity = false;

    void Detonate(Aura* aura)
    {
        if (m_accumulatedDamage <= 0)
            return;

        Unit* caster = aura->GetCaster();
        Unit* target = aura->GetTarget();
        if (!target->IsAlive() || !caster || !caster->IsAlive())
            return;

        int32 damage = m_accumulatedDamage;
        if (Aura* relentlessStrikes = caster->GetAura(SPELL_ROGUE_RELENTLESS_STRIKES, EFFECT_INDEX_1))
            damage += damage * relentlessStrikes->GetModifier()->m_amount * relentlessStrikes->GetStackAmount() / 100;

        m_accumulatedDamage = 0;
        caster->CastCustomSpell(target, SPELL_ROGUE_SHADOW_OF_DEATH_DAMAGE, &damage, nullptr, nullptr, true, nullptr, aura);
    }

    void OnAfterApply(Aura* aura, bool apply) override
    {
        Unit* caster = aura->GetCaster();
        if (apply)
        {
            if (caster && caster->IsPlayer())
            {
                m_comboPoints = caster->ToPlayer()->GetComboPoints();
                m_maxDamage = int32(caster->GetTotalAttackPowerValue(BASE_ATTACK) * m_comboPoints) / 2;
            }
        }
        else
        {
            if (m_reachedCapacity || aura->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
                Detonate(aura);
        }
    }

    std::optional<SpellAuraProcResult> OnProc(Unit* /*owner*/, Unit* /*victim*/, uint32 damage, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 /*procEx*/, uint32 /*cooldown*/) override
    {
        Unit* auraCaster = aura->GetCaster();
        if (!auraCaster || !auraCaster->IsAlive() || !m_comboPoints)
            return SPELL_AURA_PROC_FAILED;

        m_accumulatedDamage = std::min(m_accumulatedDamage + int32(damage * 0.1f * m_comboPoints), m_maxDamage);

        if (m_accumulatedDamage >= m_maxDamage)
        {
            m_reachedCapacity = true;
            Detonate(aura);
            aura->GetTarget()->RemoveAurasDueToSpell(SPELL_ROGUE_SHADOW_OF_DEATH);
        }

        return SPELL_AURA_PROC_OK;
    }
};

struct spell_rogue_improved_sap_vanish : public SpellScript
{
    void OnEffectExecuted(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (spell->m_spellInfo->Effect[effIdx] != SPELL_EFFECT_SANCTUARY)
            return;

        if (Player* player = ToPlayer(spell->GetUnitTarget()))
            player->CastHighestStealthRank();
    }
};

// #367 O-9 (a): Agitating Poison proc 45613 keeps its enchantment; threat and Nature damage
// follow the caster's level band (L20 +150 ... L60 +395, damage in the same ratio). Rank V at
// level 60 is unchanged; the lower ranks exist only through the bot poison item 90140.
struct spell_rogue_agitating_poison : public SpellScript
{
    bool OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (!spell || !spell->m_casterUnit)
            return true;

        int32 const threat = GetAgitatingPoisonThreat(spell->m_casterUnit);
        if (effIdx == EFFECT_INDEX_0)
            spell->damage = int32(float(spell->damage) * threat / AGITATING_POISON_RANK_V_THREAT + 0.5f);
        else if (effIdx == EFFECT_INDEX_1)
            spell->damage = threat;

        return true;
    }
};

// #367 D-2 Spit (bots): taunt on the main target plus up to two enemies around it, and the
// rogue spits at the main target (text emote 89).
struct spell_rogue_spit : public SpellScript
{
    void OnHit(Spell* spell, SpellMissInfo missInfo) const override
    {
        Unit* caster = spell ? spell->m_casterUnit : nullptr;
        Unit* target = spell ? spell->GetUnitTarget() : nullptr;
        if (!caster || !target || missInfo != SPELL_MISS_NONE)
            return;

        SendSpitEmote(caster, target);

        std::list<Unit*> nearby;
        MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck check(target, caster, SPIT_SPLASH_RADIUS);
        MaNGOS::UnitListSearcher<MaNGOS::AnyUnfriendlyUnitInObjectRangeCheck> searcher(nearby, check);
        Cell::VisitAllObjects(target, searcher, SPIT_SPLASH_RADIUS);

        uint32 taunted = 0;
        for (Unit* unit : nearby)
        {
            if (taunted >= SPIT_EXTRA_TARGETS)
                break;
            if (!unit || unit == target || unit == caster || !unit->IsAlive())
                continue;
            if (!caster->IsValidAttackTarget(unit) || !target->IsWithinLOSInMap(unit))
                continue;

            caster->CastSpell(unit, SPELL_ROGUE_SPIT_SPLASH, true);
            ++taunted;
        }
    }

    static void SendSpitEmote(Unit* caster, Unit* target)
    {
        if (EmotesTextEntry const* emote = sEmotesTextStore.LookupEntry(TEXTEMOTE_SPIT))
            caster->HandleEmote(emote->textid);

        char const* name = target->GetName();
        uint32 const nameLength = (name ? strlen(name) : 0) + 1;
        WorldPacket data(SMSG_TEXT_EMOTE, 20 + nameLength);
        data << caster->GetObjectGuid();
        data << uint32(TEXTEMOTE_SPIT);
        data << uint32(0);
        data << uint32(nameLength);
        if (nameLength > 1)
            data.append(name, nameLength);
        else
            data << uint8(0x00);
        caster->SendMessageToSet(&data, true);
    }
};

// #367 D-4/D-9 Shadow Dance (bots): parry -> +5 % dodge for 3 s, dodge -> +5 % parry for
// 3 s; every dodge and parry adds the rank's flat threat (50/90/130) on the attacker.
struct spell_rogue_shadow_dance : public AuraScript
{
    std::optional<SpellAuraProcResult> OnProc(Unit* owner, Unit* victim, uint32 /*amount*/, int32 /*originalAmount*/, Aura* aura, SpellEntry const* /*procSpell*/, uint32 /*procFlag*/, uint32 procEx, uint32 /*cooldown*/) override
    {
        if (!owner || !aura)
            return std::nullopt;

        if (procEx & PROC_EX_PARRY)
            owner->CastSpell(owner, SPELL_ROGUE_SHADOW_DANCE_DODGE_BUFF, true, nullptr, aura);
        else if (procEx & PROC_EX_DODGE)
            owner->CastSpell(owner, SPELL_ROGUE_SHADOW_DANCE_PARRY_BUFF, true, nullptr, aura);
        else
            return SPELL_AURA_PROC_FAILED;

        int32 const threat = aura->GetModifier()->m_amount;
        if (victim && threat > 0 && victim->IsAlive() && victim->CanHaveThreatList())
            victim->AddThreat(owner, float(threat), false, SPELL_SCHOOL_MASK_NORMAL, aura->GetSpellProto());

        return SPELL_AURA_PROC_OK;
    }
};
}

void AddSC_rogue_spell_scripts()
{
    RegisterSpellScript("spell_rogue_agitating_poison", &GetSpellScript<spell_rogue_agitating_poison>);
    RegisterSpellScript("spell_rogue_spit", &GetSpellScript<spell_rogue_spit>);
    RegisterAuraScript("spell_rogue_shadow_dance", &GetAuraScript<spell_rogue_shadow_dance>);
    RegisterSpellScript("spell_rogue_eviscerate", &GetSpellScript<spell_rogue_eviscerate>);
    RegisterSpellScript("spell_rogue_surprise_attack", &GetSpellScript<spell_rogue_surprise_attack>);
    RegisterSpellScript("spell_rogue_noxious_assault", &GetSpellScript<spell_rogue_noxious_assault>);
    RegisterSpellScript("spell_rogue_mark_for_death", &GetSpellScript<spell_rogue_mark_for_death>);
    RegisterSpellAndAuraScript("spell_rogue_blackjack", &GetSpellScript<spell_rogue_blackjack>, &GetAuraScript<spell_rogue_blackjack>);
    RegisterSpellScript("spell_rogue_preparation", &GetSpellScript<spell_rogue_preparation>);
    RegisterSpellScript("spell_rogue_deadly_throw_poison", &GetSpellScript<spell_rogue_deadly_throw_poison>);
    RegisterSpellScript("spell_rogue_vanish", &GetSpellScript<spell_rogue_vanish>);
    RegisterAuraScript("spell_rogue_exploit_vulnerability", &GetAuraScript<spell_rogue_exploit_vulnerability>);
    RegisterAuraScript("spell_rogue_honor_among_thieves", &GetAuraScript<spell_rogue_honor_among_thieves>);
    RegisterAuraScript("spell_rogue_clean_escape", &GetAuraScript<spell_rogue_clean_escape>);
    RegisterAuraScript("spell_rogue_blade_flurry", &GetAuraScript<spell_rogue_blade_flurry>);
    RegisterAuraScript("spell_rogue_improved_ambush", &GetAuraScript<spell_rogue_improved_ambush>);
    RegisterAuraScript("spell_rogue_setup", &GetAuraScript<spell_rogue_setup>);
    RegisterSpellAndAuraScript("spell_rogue_rupture", &GetSpellScript<spell_rogue_rupture_spell>, &GetAuraScript<spell_rogue_rupture_aura>);
    RegisterAuraScript("spell_rogue_cloaked_in_shadows", &GetAuraScript<spell_rogue_cloaked_in_shadows>);
    RegisterAuraScript("spell_rogue_shadow_of_death", &GetAuraScript<spell_rogue_shadow_of_death>);
    RegisterSpellScript("spell_rogue_improved_sap_vanish", &GetSpellScript<spell_rogue_improved_sap_vanish>);
}
