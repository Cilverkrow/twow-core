
#include "playerbot/playerbot.h"
#include "CcTargetValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/EmergencyLadderPolicy.h"
#include "playerbot/EmergencyLadderTrace.h"

using namespace ai;

class FindTargetForCcStrategy : public FindTargetStrategy
{
public:
    FindTargetForCcStrategy(PlayerbotAI* ai, std::string spell, bool soloCc = false) : FindTargetStrategy(ai)
    {
        this->spell = spell;
        maxDistance = 0;
        this->soloCc = soloCc;
    }

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* threatManager)
    {
        Player* bot = ai->GetBot();

        AiObjectContext* context = ai->GetAiObjectContext();

        if (!ai->CanCastSpell(spell, creature, true, nullptr, false, true))
            return;

        if (AI_VALUE(Unit*,"rti cc target") == creature)
        {
            result = creature;
            return;
        }

        if (AI_VALUE(Unit*,"current target") == creature)
            return;

        if (AI_VALUE(Unit*,"rti target") == creature)
            return;

        uint8 health = creature->GetHealthPercent();
        if (health < sPlayerbotAIConfig.mediumHealth)
            return;

        float minDistance = ai->GetRange("spell");
        Group* group = bot->GetGroup();
        if (!group)
        {
            // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): solo CC on an add. The solo branch ends here, before the group
            // members are read below (a player target would reach them otherwise).
            if (!soloCc)
                return;
            Creature* add = creature->ToCreature();
            Unit* victim = creature->GetVictim();
            Unit* current = AI_VALUE(Unit*, "current target");
            ai::emergency_ladder::CcCandidate c;
            c.player = creature->IsPlayer();
            c.elite = add && (add->IsElite() || add->IsWorldBoss());
            c.attacksBotOrPet = victim && (victim == bot || (bot->GetPet() && victim == bot->GetPet()));
            c.hasDot = creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE);
            c.currentTargetHp = current ? uint32(current->GetHealthPercent()) : 100;
            if (!result && ai::emergency_ladder::SoloCcEligible(c))
                result = creature;
            return;
        }

        if (AI_VALUE(uint8,"aoe count") > 2)
        {
            WorldLocation aoe = AI_VALUE(WorldLocation,"aoe position");
            if (sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.GetDistance2d(creature, aoe.coord_x, aoe.coord_y), sPlayerbotAIConfig.aoeRadius))
                return;
        }

        if (creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) && !(spell == "fear" || spell == "banish"))
            return;

        if (!creature->IsPlayer())
        {
            int tankCount, dpsCount;
            GetPlayerCount(creature, &tankCount, &dpsCount);
            if (!tankCount || !dpsCount)
            {
                result = creature;
                return;
            }
        }

        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *member = sObjectMgr.GetPlayer(itr->guid);
            if(!member || !sServerFacade.IsAlive(member) || member == bot || bot->GetMapId() != member->GetMapId())
                continue;

            if (!ai->IsTank(member))
                continue;

            float distance = sServerFacade.GetDistance2d(member, creature);
            if (distance < minDistance)
                minDistance = distance;
        }

        if ((!result && !creature->IsPlayer()) || minDistance > maxDistance)
        {
            result = creature;
            maxDistance = minDistance;
        }
    }

private:
    std::string spell;
    float maxDistance;
    bool soloCc = false;   // twow-repo#602 X3: the solo branch may pick (switch, cohort, spell list, >= 2 attackers)
};

Unit* CcTargetValue::Calculate()
{
    // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): solo CC for the solo free bots the switch applies to, only with the
    // spells of AiPlayerbot.Rotation.EmergencyLadder.SoloCcSpells (F3). With the switch off soloCc stays false and nothing else changes.
    bool soloCc = false;
    if (sPlayerbotAIConfig.rotationEmergencyLadder && !bot->GetGroup() && !ai->IsRealPlayer() && sRandomPlayerbotMgr.IsFreeBot(bot) &&
        ai::cohort::Applies(sPlayerbotAIConfig.rotationEmergencyLadder, bot->GetGUIDLow()) &&
        ai::emergency_ladder::SpellAllowed(qualifier, sPlayerbotAIConfig.rotationEmergencyLadderSoloCcSpells))
        soloCc = true;
    uint32 const nowMs = soloCc ? WorldTimer::getMSTime() : 0;
    auto held = [this, soloCc, nowMs](ObjectGuid guid)
    {
        if (soloCc && soloCcHeld != guid)
        {
            soloCcHeld = guid;
            soloCcHeldSinceMs = nowMs;
        }
    };

    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>,"possible targets no los");

    for (std::list<ObjectGuid>::iterator i = possible.begin(); i != possible.end(); ++i)
    {
        ObjectGuid guid = *i;
        Unit* add = ai->GetUnit(guid);
        if (!add)
            continue;

        if (!ai->IsSafe(add))
            continue;

        if (ai->HasMyAura(qualifier, add))
        {
            held(guid);
            return NULL;
        }

        if (qualifier == "polymorph")
        {
            if (ai->HasMyAura("polymorph: pig", add) || ai->HasMyAura("polymorph: turtle", add))
            {
                held(guid);
                return NULL;
            }
        }
    }

    if (soloCc)
    {
        // The add the bot had under its CC is free again: broken when that happened sooner than CcBrokenBeforeMs (a natural end of
        // Polymorph / Banish / Fear takes longer) and the add still lives (OB-10 18.10.: count the bot's own AoE breaking its CC).
        if (soloCcHeld)
        {
            Unit* was = ai->GetUnit(soloCcHeld);
            if (was && was->IsAlive() && ai::emergency_ladder::CcBroken(soloCcHeldSinceMs, nowMs))
                ai::emergency_ladder_rt::RecordCcBroken(bot->getClass(), ai::cohort::InB(bot->GetGUIDLow()));
            if (soloCcPick == soloCcHeld)
                soloCcPick = ObjectGuid();   // a CC of the same add again counts as a new solo CC
            soloCcHeld = ObjectGuid();
        }
        // Only an add: at least two attackers, and no Fear while other mobs are near that it could pull.
        if (AI_VALUE(uint8, "my attacker count") < 2 || (qualifier == "fear" && AI_VALUE(bool, "possible adds")))
            return NULL;
    }

    FindTargetForCcStrategy strategy(ai, qualifier, soloCc);
    Unit* target = FindTarget(&strategy);
    if (soloCc && target && target->GetObjectGuid() != soloCcPick)
    {
        soloCcPick = target->GetObjectGuid();
        uint32 const guid = bot->GetGUIDLow();
        ai::emergency_ladder_rt::RecordSoloCc(bot->getClass(), ai::cohort::InB(guid));
        if (ai::emergency_ladder_rt::TakeLineBudget(uint32(time(nullptr))))
            sLog.outBasic("%s", ai::emergency_ladder::FormatSoloCcLine(guid, bot->GetName(), bot->getClass(), ai::cohort::Letter(guid),
                qualifier, target->GetEntry(), AI_VALUE(uint8, "my attacker count")).c_str());
    }
    return target;
}
