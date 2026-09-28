/* Copyright (C) 2006 - 2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* ScriptData
SDName: Blasted_Lands
SD%Complete: 90
SDCategory: Blasted Lands
EndScriptData */

/* ContentData
npc_thadius_grimshade
go_stone_of_binding
EndContentData */

#include "scriptPCH.h"

enum
{
    SPELL_SPIRIT_SHOCK          = 10794,
    SPELL_FEL_CURSE             = 12938,
    NPC_SERVANT_OF_RAZELIKH     = 7668,
    NPC_SERVANT_OF_GROL         = 7669,
    NPC_SERVANT_OF_ALLISTARJ    = 7670,
    NPC_SERVANT_OF_SEVINE       = 7671
};

bool GOHello_go_stone_of_binding(Player* pPlayer, GameObject* pGo)
{
// 141812 <= 7668 Servant of Razelikh   // 141857 <= 7669 Servant of Grol
// 141858 <= 7670 Servant of Allistarj  // 141859 <= 7671 Servant of Sevine
    Creature* pCreature = nullptr;
    switch(pGo->GetEntry())
    {
        case 141812:
            pCreature = pGo->FindNearestCreature(NPC_SERVANT_OF_RAZELIKH, 30.0f, true);//servant of razelikh
            break;
        case 141857:
            pCreature = pGo->FindNearestCreature(NPC_SERVANT_OF_GROL, 30.0f, true);//servant of grol
            break;
        case 141858:
            pCreature = pGo->FindNearestCreature(NPC_SERVANT_OF_ALLISTARJ, 30.0f, true);//servant of allistarj
            break;
        case 141859:
            pCreature = pGo->FindNearestCreature(NPC_SERVANT_OF_SEVINE, 30.0f, true);//servant of sevine
            break;
    }
    if (pCreature)
        pCreature->CastSpell(pCreature, SPELL_FEL_CURSE, true);
    return false;
}

struct ServantAI : public ScriptedAI
{
    ServantAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        Reset();
    }

    bool m_freezed;

    void Reset() override
    {
        m_freezed = false;
    }

    void JustRespawned() override
    {
        Reset();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_creature->HealthBelowPct(15) && !m_freezed)
        {
            DoCastSpellIfCan(m_creature, SPELL_SPIRIT_SHOCK);
        }

        DoMeleeAttackIfReady();
    }

    void DamageTaken(Unit* pDoneBy, uint32& uiDamage) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_creature->HealthBelowPctDamaged(10, uiDamage))
        {
            uiDamage = 0;
        }
    }

    void SpellHit(WorldObject* pCaster, SpellEntry const* pSpell) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (pSpell->Id == SPELL_FEL_CURSE && m_creature->HasAura(SPELL_SPIRIT_SHOCK))
        {
            if (Player* pPlayer = m_creature->GetVictim()->ToPlayer())
                pPlayer->DoKillUnit(m_creature);
        }
    }
};

CreatureAI* GetAI_servant(Creature* pCreature)
{
    return new ServantAI(pCreature);
}

/*######
## Voidlord of the Twisting Rift (twow-repo#348, owner design 2026-09-27)
## Summoned by quest 41936 at Daio the Decrepit. Every 20 % of the shared
## health pool it loses, one more void lord splits off (80/60/40/20 % ->
## 2/3/4/5 in total) and every body shrinks by 20 % of the start size. All
## bodies share one health pool, cast all abilities themselves and die
## together; only the lord (65201) carries loot.
######*/

enum
{
    NPC_TWISTING_RIFT_VOIDLORD      = 65201,
    NPC_TWISTING_RIFT_VOIDSPLIT     = 65202,

    // Owner 2026-09-28: the Shadow Nova (45559) looked like Holy Nova in the client, so the
    // pulse uses the Hellfire III visual (2951, an unused NPC spell) with the nova values.
    SPELL_VOID_HELLFIRE             = 2951,     // 236 shadow to all enemies around the caster
    SPELL_VOID_SHADOW_SHIELD        = 22417,    // self absorb
    SPELL_VOID_CORRUPTION           = 25311,    // warlock Corruption rank 7
    SPELL_VOID_SHADOW_BOLT          = 25307,    // warlock Shadow Bolt rank 10

    VOID_SPLIT_STEPS                = 4,        // 80, 60, 40, 20 %
};

static float const VOID_START_SCALE = 5.4f;  // owner 2026-09-28: 20 % larger (was 4.5)

// Abilities and the shared health pool, identical for the lord and every split.
struct twisting_rift_voidAI : public ScriptedAI
{
    explicit twisting_rift_voidAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        twisting_rift_voidAI::Reset();
    }

    uint32 m_uiNovaTimer;
    uint32 m_uiShieldTimer;
    uint32 m_uiCorruptionTimer;
    uint32 m_uiBoltTimer;

    // Every other living body of this encounter.
    virtual void GetOtherBodies(std::vector<Creature*>& bodies) = 0;

    void Reset() override
    {
        m_uiNovaTimer = 2000;
        m_uiShieldTimer = urand(5000, 8000);
        m_uiCorruptionTimer = urand(3000, 5000);
        m_uiBoltTimer = urand(6000, 9000);
    }

    void DamageTaken(Unit* /*pDoneBy*/, uint32& uiDamage) override
    {
        // One pool: mirror the damage onto every other body, but never kill
        // them here; JustDied below takes them down together.
        std::vector<Creature*> bodies;
        GetOtherBodies(bodies);
        for (Creature* pBody : bodies)
        {
            uint32 const uiHealth = pBody->GetHealth();
            uint32 const uiMirrored = std::min(uiDamage, uiHealth > 1 ? uiHealth - 1 : 0u);
            pBody->SetHealth(uiHealth - uiMirrored);
            pBody->CountDamageTaken(uiMirrored, true);
        }
    }

    void JustDied(Unit* pKiller) override
    {
        std::vector<Creature*> bodies;
        GetOtherBodies(bodies);
        for (Creature* pBody : bodies)
            if (pBody->IsAlive() && pKiller)
                pKiller->Kill(pBody, nullptr, false);
    }

    void UpdateAbilities(uint32 const uiDiff)
    {
        if (m_uiNovaTimer <= uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_VOID_HELLFIRE, CF_TRIGGERED) == CAST_OK)
                m_uiNovaTimer = 2000;
        }
        else
            m_uiNovaTimer -= uiDiff;

        if (m_uiShieldTimer <= uiDiff)
        {
            if (m_creature->HasAura(SPELL_VOID_SHADOW_SHIELD) ||
                DoCastSpellIfCan(m_creature, SPELL_VOID_SHADOW_SHIELD, CF_TRIGGERED) == CAST_OK)
                m_uiShieldTimer = 30000;
        }
        else
            m_uiShieldTimer -= uiDiff;

        if (m_uiCorruptionTimer <= uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
                if (DoCastSpellIfCan(pTarget, SPELL_VOID_CORRUPTION) == CAST_OK)
                    m_uiCorruptionTimer = urand(8000, 12000);
        }
        else
            m_uiCorruptionTimer -= uiDiff;

        if (m_uiBoltTimer <= uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
                if (DoCastSpellIfCan(pTarget, SPELL_VOID_SHADOW_BOLT) == CAST_OK)
                    m_uiBoltTimer = urand(10000, 15000);
        }
        else
            m_uiBoltTimer -= uiDiff;
    }
};

struct boss_twisting_rift_voidlordAI : public twisting_rift_voidAI
{
    explicit boss_twisting_rift_voidlordAI(Creature* pCreature) : twisting_rift_voidAI(pCreature)
    {
        boss_twisting_rift_voidlordAI::Reset();
    }

    std::vector<ObjectGuid> m_splitGuids;
    uint32 m_uiSplitsDone;

    void Reset() override
    {
        twisting_rift_voidAI::Reset();
        m_uiSplitsDone = 0;
        m_creature->SetObjectScale(VOID_START_SCALE);
    }

    void GetOtherBodies(std::vector<Creature*>& bodies) override
    {
        for (ObjectGuid const& guid : m_splitGuids)
            if (Creature* pSplit = m_creature->GetMap()->GetCreature(guid))
                if (pSplit->IsAlive())
                    bodies.push_back(pSplit);
    }

    void DespawnSplits()
    {
        for (ObjectGuid const& guid : m_splitGuids)
            if (Creature* pSplit = m_creature->GetMap()->GetCreature(guid))
                pSplit->ForcedDespawn();
        m_splitGuids.clear();
    }

    void EnterEvadeMode() override
    {
        DespawnSplits();
        ScriptedAI::EnterEvadeMode();
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() != NPC_TWISTING_RIFT_VOIDSPLIT)
            return;

        m_splitGuids.push_back(pSummoned->GetObjectGuid());
        pSummoned->SetHealth(std::min(m_creature->GetHealth(), pSummoned->GetMaxHealth()));
        if (Unit* pVictim = m_creature->GetVictim())
            pSummoned->AI()->AttackStart(pVictim);
    }

    void Rescale()
    {
        float const fScale = VOID_START_SCALE * (1.0f - 0.2f * float(m_uiSplitsDone));
        m_creature->SetObjectScale(fScale);
        std::vector<Creature*> bodies;
        GetOtherBodies(bodies);
        for (Creature* pBody : bodies)
            pBody->SetObjectScale(fScale);
    }

    void UpdateAI(uint32 const uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // 80 % -> 2 bodies, 60 % -> 3, 40 % -> 4, 20 % -> 5.
        while (m_uiSplitsDone < VOID_SPLIT_STEPS &&
               m_creature->GetHealthPercent() <= 80.0f - 20.0f * float(m_uiSplitsDone))
        {
            float const fAngle = frand(0.0f, 2.0f * M_PI_F);
            m_creature->SummonCreature(NPC_TWISTING_RIFT_VOIDSPLIT,
                m_creature->GetPositionX() + 5.0f * cos(fAngle), m_creature->GetPositionY() + 5.0f * sin(fAngle),
                m_creature->GetPositionZ(), m_creature->GetOrientation(), TEMPSUMMON_CORPSE_TIMED_DESPAWN, 60000);
            ++m_uiSplitsDone;
            Rescale();
        }

        UpdateAbilities(uiDiff);
        DoMeleeAttackIfReady();
    }
};

struct npc_twisting_rift_voidsplitAI : public twisting_rift_voidAI
{
    explicit npc_twisting_rift_voidsplitAI(Creature* pCreature) : twisting_rift_voidAI(pCreature) {}

    Creature* GetLord()
    {
        return GetClosestCreatureWithEntry(m_creature, NPC_TWISTING_RIFT_VOIDLORD, 100.0f);
    }

    void GetOtherBodies(std::vector<Creature*>& bodies) override
    {
        Creature* pLord = GetLord();
        if (!pLord || !pLord->IsAlive())
            return;
        bodies.push_back(pLord);
        if (boss_twisting_rift_voidlordAI* pLordAI = dynamic_cast<boss_twisting_rift_voidlordAI*>(pLord->AI()))
        {
            std::vector<Creature*> splits;
            pLordAI->GetOtherBodies(splits);
            for (Creature* pSplit : splits)
                if (pSplit != m_creature)
                    bodies.push_back(pSplit);
        }
    }

    void UpdateAI(uint32 const uiDiff) override
    {
        Creature* pLord = GetLord();
        if (!pLord || !pLord->IsAlive())
        {
            m_creature->ForcedDespawn();
            return;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        UpdateAbilities(uiDiff);
        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_boss_twisting_rift_voidlord(Creature* pCreature)
{
    return new boss_twisting_rift_voidlordAI(pCreature);
}

CreatureAI* GetAI_npc_twisting_rift_voidsplit(Creature* pCreature)
{
    return new npc_twisting_rift_voidsplitAI(pCreature);
}

void AddSC_blasted_lands()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "go_stone_of_binding";
    newscript->pGOHello = &GOHello_go_stone_of_binding;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_servant";
    newscript->GetAI = &GetAI_servant;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "boss_twisting_rift_voidlord";
    newscript->GetAI = &GetAI_boss_twisting_rift_voidlord;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_twisting_rift_voidsplit";
    newscript->GetAI = &GetAI_npc_twisting_rift_voidsplit;
    newscript->RegisterSelf();
}
