/* twow-repo#443 (train 9): Karrsh the Sentinel, first boss of Timbermaw Hold (map 819).
 * Owner design 2026-09-29 (video 1, 02:19-04:42):
 *  - Earthbind totems (800 hp, 20 s) within 45 m: one attempt every 10 s, at most three at once;
 *    after one is lost the next waits 8 s.
 *  - Every 20 s, for 15 s, one of Windfury / Lava Nova / Chain Lightning totem (33 % each).
 *  - From 50 %: Windfury on himself; every 45 s he rushes a random player within 30 m and
 *    Frost Shocks them for 60 s (less movement and attack speed).
 * Existing client spells are reused with scripted values, so no client patch is needed.
 * Totems have their own creature entries 65300-65303 (bot target priority, OB-10). */

#include "scriptPCH.h"

enum
{
    NPC_KARRSH_EARTHBIND_TOTEM      = 65300,
    NPC_KARRSH_WINDFURY_TOTEM       = 65301,
    NPC_KARRSH_LAVA_NOVA_TOTEM      = 65302,
    NPC_KARRSH_CHAIN_TOTEM          = 65303,

    SPELL_EARTHBIND                 = 3600,     // 10 yd slow, cast by the earthbind totem
    SPELL_WINDFURY                  = 51367,    // 20 % chance for an extra attack
    SPELL_LAVA_NOVA                 = 8349,     // Fire Nova visual, 10 yd, scripted damage
    SPELL_CHAIN_LIGHTNING           = 15305,
    SPELL_CHARGE                    = 22911,
    SPELL_FROST_SHOCK               = 15499,    // movement slow + frost damage
    SPELL_CHILLED                   = 16927,    // attack speed and movement slow

    SAY_AGGRO                       = 6530001,
    SAY_HALF                        = 6530002,
    SAY_KILL                        = 6530003,
    SAY_DEATH                       = 6530004,
};

namespace KarrshValues
{
    uint32 const EarthbindAttemptMs  = 10000;
    uint32 const EarthbindRetryMs    = 8000;
    uint32 const EarthbindLifeMs     = 20000;
    uint32 const EarthbindMax        = 3;
    float  const EarthbindRange      = 45.0f;
    uint32 const RotatingTotemMs     = 20000;
    uint32 const RotatingLifeMs      = 15000;
    float  const RotatingRange       = 15.0f;
    uint32 const RushMs              = 45000;
    float  const RushRange           = 30.0f;
    int32  const FrostShockDurMs     = 60000;
    int32  const FrostShockDamage    = 1200;    // start value, tune in test
    int32  const LavaNovaDamage      = 600;     // every 3 s within 10 yd
    int32  const ChainDamage         = 700;     // every 3 s on a random player
    uint32 const TotemPulseMs        = 3000;
}

struct boss_karrsh_the_sentinelAI : public ScriptedAI
{
    explicit boss_karrsh_the_sentinelAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_earthbindTimer;
    uint32 m_rotatingTimer;
    uint32 m_rushTimer;
    uint32 m_earthbindAlive;
    bool m_halfDone;
    std::list<ObjectGuid> m_totems;

    void Reset() override
    {
        m_earthbindTimer = KarrshValues::EarthbindAttemptMs;
        m_rotatingTimer = KarrshValues::RotatingTotemMs;
        m_rushTimer = 0;
        m_earthbindAlive = 0;
        m_halfDone = false;
        DespawnTotems();
        m_creature->RemoveAurasDueToSpell(SPELL_WINDFURY);
    }

    void DespawnTotems()
    {
        for (ObjectGuid const& guid : m_totems)
            if (Creature* totem = m_creature->GetMap()->GetCreature(guid))
                totem->ForcedDespawn();
        m_totems.clear();
    }

    void Aggro(Unit* /*pWho*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void KilledUnit(Unit* pVictim) override
    {
        if (pVictim->IsPlayer() && urand(0, 2) == 0)
            DoScriptText(SAY_KILL, m_creature);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        DoScriptText(SAY_DEATH, m_creature);
        DespawnTotems();
    }

    void JustReachedHome() override
    {
        DespawnTotems();
    }

    bool SummonTotem(uint32 entry, float range, uint32 lifeMs)
    {
        float x, y, z;
        if (!m_creature->GetRandomPoint(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ(), range, x, y, z))
            m_creature->GetPosition(x, y, z);

        Creature* totem = m_creature->SummonCreature(entry, x, y, z, 0.0f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, lifeMs);
        if (!totem)
            return false;

        totem->SetRooted(true);
        m_totems.push_back(totem->GetObjectGuid());
        return true;
    }

    void LoseTotem(Creature* pSummon)
    {
        m_totems.remove(pSummon->GetObjectGuid());

        if (pSummon->GetEntry() == NPC_KARRSH_EARTHBIND_TOTEM && m_earthbindAlive)
        {
            --m_earthbindAlive;
            m_earthbindTimer = KarrshValues::EarthbindRetryMs;
        }
        else if (pSummon->GetEntry() == NPC_KARRSH_WINDFURY_TOTEM && !m_halfDone)
            m_creature->RemoveAurasDueToSpell(SPELL_WINDFURY);
    }

    void SummonedCreatureJustDied(Creature* pSummon) override { LoseTotem(pSummon); }
    void SummonedCreatureDespawn(Creature* pSummon) override { LoseTotem(pSummon); }

    void Rush()
    {
        Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 1, nullptr, SELECT_FLAG_PLAYER | SELECT_FLAG_IN_LOS);
        if (!target || !m_creature->IsWithinDistInMap(target, KarrshValues::RushRange))
            return;

        m_creature->CastSpell(target, SPELL_CHARGE, true);
        m_creature->CastCustomSpell(target, SPELL_FROST_SHOCK, nullptr, &KarrshValues::FrostShockDamage, nullptr, true);
        target->AddAura(SPELL_CHILLED, 0, m_creature);

        for (uint32 spellId : { uint32(SPELL_FROST_SHOCK), uint32(SPELL_CHILLED) })
            if (SpellAuraHolder* holder = target->GetSpellAuraHolder(spellId))
            {
                holder->SetAuraMaxDuration(KarrshValues::FrostShockDurMs);
                holder->SetAuraDuration(KarrshValues::FrostShockDurMs);
                holder->UpdateAuraDuration();
            }
    }

    void UpdateAI(uint32 const uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (!m_halfDone && m_creature->GetHealthPercent() <= 50.0f)
        {
            m_halfDone = true;
            DoScriptText(SAY_HALF, m_creature);
            m_creature->AddAura(SPELL_WINDFURY, 0, m_creature);
            m_rushTimer = 1000;
        }

        if (m_earthbindTimer <= uiDiff)
        {
            if (m_earthbindAlive < KarrshValues::EarthbindMax &&
                SummonTotem(NPC_KARRSH_EARTHBIND_TOTEM, KarrshValues::EarthbindRange, KarrshValues::EarthbindLifeMs))
                ++m_earthbindAlive;
            m_earthbindTimer = KarrshValues::EarthbindAttemptMs;
        }
        else
            m_earthbindTimer -= uiDiff;

        if (m_rotatingTimer <= uiDiff)
        {
            static uint32 const rotating[3] = { NPC_KARRSH_WINDFURY_TOTEM, NPC_KARRSH_LAVA_NOVA_TOTEM, NPC_KARRSH_CHAIN_TOTEM };
            SummonTotem(rotating[urand(0, 2)], KarrshValues::RotatingRange, KarrshValues::RotatingLifeMs);
            m_rotatingTimer = KarrshValues::RotatingTotemMs;
        }
        else
            m_rotatingTimer -= uiDiff;

        if (m_halfDone)
        {
            if (m_rushTimer <= uiDiff)
            {
                Rush();
                m_rushTimer = KarrshValues::RushMs;
            }
            else
                m_rushTimer -= uiDiff;
        }

        DoMeleeAttackIfReady();
    }
};

// One AI for all four totems: rooted, pulses its effect every 3 s while the summoner fights.
struct npc_karrsh_totemAI : public ScriptedAI
{
    explicit npc_karrsh_totemAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_pulseTimer;

    void Reset() override { m_pulseTimer = 500; }
    void AttackStart(Unit* /*pWho*/) override {}
    void MoveInLineOfSight(Unit* /*pWho*/) override {}

    Creature* GetKarrsh() const
    {
        if (TemporarySummon* summon = dynamic_cast<TemporarySummon*>(m_creature))
            return m_creature->GetMap()->GetCreature(summon->GetSummonerGuid());
        return nullptr;
    }

    void Pulse()
    {
        Creature* karrsh = GetKarrsh();
        if (!karrsh || !karrsh->IsAlive() || !karrsh->IsInCombat())
            return;

        switch (m_creature->GetEntry())
        {
            case NPC_KARRSH_EARTHBIND_TOTEM:
                m_creature->CastSpell(m_creature, SPELL_EARTHBIND, true, nullptr, nullptr, karrsh->GetObjectGuid());
                break;
            case NPC_KARRSH_WINDFURY_TOTEM:
                if (!karrsh->HasAura(SPELL_WINDFURY) && m_creature->IsWithinDistInMap(karrsh, 30.0f))
                    karrsh->AddAura(SPELL_WINDFURY, 0, m_creature);
                break;
            case NPC_KARRSH_LAVA_NOVA_TOTEM:
                m_creature->CastCustomSpell(m_creature, SPELL_LAVA_NOVA, &KarrshValues::LavaNovaDamage, nullptr, nullptr, true,
                                            nullptr, nullptr, karrsh->GetObjectGuid());
                break;
            case NPC_KARRSH_CHAIN_TOTEM:
                if (Unit* target = karrsh->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
                    if (m_creature->IsWithinDistInMap(target, 30.0f))
                        m_creature->CastCustomSpell(target, SPELL_CHAIN_LIGHTNING, &KarrshValues::ChainDamage, nullptr, nullptr, true,
                                                    nullptr, nullptr, karrsh->GetObjectGuid());
                break;
        }
    }

    void UpdateAI(uint32 const uiDiff) override
    {
        if (m_pulseTimer <= uiDiff)
        {
            Pulse();
            m_pulseTimer = KarrshValues::TotemPulseMs;
        }
        else
            m_pulseTimer -= uiDiff;
    }
};

CreatureAI* GetAI_boss_karrsh_the_sentinel(Creature* pCreature) { return new boss_karrsh_the_sentinelAI(pCreature); }
CreatureAI* GetAI_npc_karrsh_totem(Creature* pCreature) { return new npc_karrsh_totemAI(pCreature); }

void AddSC_boss_karrsh_the_sentinel()
{
    Script* newscript = new Script;
    newscript->Name = "boss_karrsh_the_sentinel";
    newscript->GetAI = &GetAI_boss_karrsh_the_sentinel;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_karrsh_totem";
    newscript->GetAI = &GetAI_npc_karrsh_totem;
    newscript->RegisterSelf();
}
