#include "LootObjectStack.h"
#include "playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/SkinLootPolicy.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/GatherNodePolicy.h"
#include "playerbot/LootHoldPolicy.h"   // twow-repo#575 H1: Loot.HoldTravelWhileLootable
#include "playerbot/strategy/triggers/ProfessionUseTriggers.h"   // twow-repo#333 T28b: IsRosterBotOnItsOwn
#include <cstdlib>   // twow-repo#575 H1: strtoull
#include "playerbot/strategy/values/SharedValueContext.h"

using namespace ai;

#define MAX_LOOT_OBJECT_COUNT 10

namespace
{
// twow-repo#333 P1-f: the lock of a quest herb - a herbalism slot next to a skill slot no profession provides ("open
// kneeling", lock 259), the same mark #414 uses for the gather travel targets (TravelMgr.cpp).
bool IsQuestHerbLock(uint32 lockId)
{
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
    if (!lockInfo)
        return false;

    bool lockHasHerbalism = false;
    bool lockHasNonProfessionSkill = false;
    for (int i = 0; i < 8; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL || !lockInfo->Index[i])
            continue;

        uint32 const skillId = SkillByLockType(LockType(lockInfo->Index[i]));
        if (skillId == SKILL_HERBALISM)
            lockHasHerbalism = true;
        else if (skillId == 0)
            lockHasNonProfessionSkill = true;
    }

    return ai::gather_node::IsQuestHerb(lockHasHerbalism, lockHasNonProfessionSkill);
}
}

LootTarget::LootTarget(ObjectGuid guid) : guid(guid), asOfTime(time(0))
{
}

LootTarget::LootTarget(LootTarget const& other)
{
    guid = other.guid;
    asOfTime = other.asOfTime;
}

LootTarget& LootTarget::operator=(LootTarget const& other)
{
    if((void*)this == (void*)&other)
        return *this;

    guid = other.guid;
    asOfTime = other.asOfTime;

    return *this;
}

bool LootTarget::operator< (const LootTarget& other) const
{
    return guid < other.guid;
}

void LootTargetList::shrink(time_t fromTime)
{
    for (std::set<LootTarget>::iterator i = begin(); i != end(); )
    {
        if (i->asOfTime <= fromTime)
            erase(i++);
		else
			++i;
    }
}

LootObject::LootObject(Player* bot, ObjectGuid guid)
	: guid(), skillId(SKILL_NONE), reqSkillValue(0), reqItem(0)
{
    Refresh(bot, guid);
}

void LootObject::Refresh(Player* bot, ObjectGuid guid, bool debug)
{
    skillId = SKILL_NONE;
    reqSkillValue = 0;
    reqItem = 0;
    this->guid = ObjectGuid();

    PlayerbotAI* ai = GetBotAI(bot);
    Creature* creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
    {
        bool const lootable = creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
        if (lootable)
        {
            // The lootable flag is set group-wide on a tapped corpse, so it alone does not mean
            // this bot may loot it. Require tap rights, otherwise the bot walks to and kneels on
            // corpses whose loot belongs to someone else (e.g. the master's round-robin kill).
            if (creature->IsTappedBy(bot))
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "Creature flag lootable.", "debug loot");

                this->guid = guid;
                // Hotfix 8.6 (twow-repo#471): the core makes a corpse lootable and skinnable at
                // once (Creature.cpp, at death) and skinning fails while loot is left
                // (TARGET_NOT_LOOTED). Normal loot first; the skinnable corpse comes back after.
                return;
            }
            else if (debug)
            {
                ai->TellDebug(ai->GetMaster(), "Creature lootable but not tapped by bot.", "debug loot");
            }
        }

        // #485: loot left on the corpse - not a skinning target. A corpse with someone else's
        // loot used to fall through to the skinning branch: the bot walked over, got no loot
        // permission and picked the corpse up again every second, while the core refuses to
        // skin it until the loot is gone (TARGET_NOT_LOOTED).
        if (skin_loot::IsSkinTarget(lootable, creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE)))
        {
            skillId = creature->GetCreatureInfo()->GetRequiredLootSkill();
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (ai->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "Creature flag skinnable and has skill.", "debug loot");
                this->guid = guid;
            }

            if (debug)
                ai->TellDebug(ai->GetMaster(), "Creature flag skinnable not enough skill.", "debug loot");
            return;
        }

        if (debug)
            ai->TellDebug(ai->GetMaster(), "Creature without loot or skin flag.", "debug loot");

        return;
    }

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go) && !go->IsInUse())
    {
        bool isQuestItemOnly = false;

#ifdef MANGOSBOT_TWO
        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
        {
            int itemId = go->GetGOInfo()->questItems[i];

            if (ItemUsageValue::IsNeededForQuest(bot, itemId))
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "GO has item needed for quest.", "debug loot");
                this->guid = guid;
                return;
            }
            isQuestItemOnly |= itemId > 0;
        }
#else
        /*if (!guid.IsEmpty())
        {
            for (auto& entry : GAI_VALUE2(std::list<int32>, "item drop list", -1*uint(go->GetEntry())))
            {
                if (IsNeededForQuest(bot, entry))
                {
                    this->guid = guid;
                    return;
                }
                isQuestItemOnly |= entry > 0;
            }
        }*/
#endif

        if (isQuestItemOnly)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has only quests items we don't need.", "debug loot");
            return;
        }

        uint32 goId = go->GetGOInfo()->id;
        std::set<uint32>& skipGoLootList = ai->GetAiObjectContext()->GetValue<std::set<uint32>&>("skip go loot list")->Get();
        if (skipGoLootList.find(goId) != skipGoLootList.end())
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go in skip go loot list.", "debug loot");
            return;
        }

        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has no lockid.", "debug loot");
            return;
        }

        for (int i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go has lock with key requirement.", "debug loot");
                        reqItem = lockInfo->Index[i];
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535) // Serpentbloom
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go is serpentbloom.", "debug loot");
                        this->guid = guid;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go requires skill.", "debug loot");
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_NONE:
                    if (debug)
                        ai->TellDebug(ai->GetMaster(), "Go has open lock.", "debug loot");
                    this->guid = guid;
                    break;
            }
        }
    }

    if (debug && guid && !this->guid)
        ai->TellDebug(ai->GetMaster(), "Go has bad lock.", "debug loot");
}

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    Refresh(bot, guid);

    PlayerbotAI* ai = GetBotAI(bot);

    Creature *creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        return creature;

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go))
        return go;

    return NULL;
}

LootObject::LootObject(const LootObject& other)
{
    guid = other.guid;
    skillId = other.skillId;
    reqSkillValue = other.reqSkillValue;
    reqItem = other.reqItem;
}

bool LootObject::IsLootPossible(Player* bot)
{
    if (IsEmpty() || !GetWorldObject(bot))
        return false;

    PlayerbotAI* ai = GetBotAI(bot);

    if (reqItem && !bot->HasItemCount(reqItem, 1))
        return false;

    if (guid.IsCreature())
    {
        Creature* creature = ai->GetCreature(guid);
        if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        {
            if (creature->m_loot && skillId != SKILL_SKINNING)
                if (!creature->m_loot->CanLoot(bot))
                    return false;
        }
    }

    AiObjectContext* context = ai->GetAiObjectContext();

    if (!AI_VALUE2_LAZY(bool, "should loot object", std::to_string(guid.GetRawValue())))
        return false;

    // Check if the game object has quest loot and bot has the quest for it
    if (guid.IsGameObject())
    {
        GameObject* go = ai->GetGameObject(guid);
        if (go)
        {
            // Ignore for mining nodes and herbs
            if (skillId != SKILL_MINING && skillId != SKILL_HERBALISM)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (!go->ActivateToQuest(bot))
                    {
                        return false;
                    }
                }
            }
            
            // herb-like quest objects
            if (skillId == SKILL_HERBALISM && reqSkillValue == 1)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (go->ActivateToQuest(bot))
                    {
                        bool hasQuestItems = false;
                        for (auto& entry : GAI_VALUE2(std::list<uint32>, "entry loot list", -1*int(go->GetEntry())))
                        {
                            if (ItemUsageValue::IsNeededForQuest(bot, entry))
                            {
                                hasQuestItems = true;
                            }
                        }
                        return hasQuestItems || go->GetLootState() != GO_READY;
                    }
                }
            }

            // twow-repo#333 P1-f (AiPlayerbot.Gather.QuestHerbFilter): a quest herb whose quest is not active is no herb.
            // Before, it fell through to the herbalism check and a herbalist picked it on the way without the quest.
            if (skillId == SKILL_HERBALISM && ai::cohort::Applies(sPlayerbotAIConfig.gatherQuestHerbFilter, bot->GetGUIDLow()))
            {
                bool const questHerb = IsQuestHerbLock(go->GetGOInfo()->GetLockId());
                if (!ai::gather_node::QuestHerbLootable(questHerb, questHerb && go->ActivateToQuest(bot)))
                    return false;
            }

            //Ignore objects that are currently in use.
            if (go->IsInUse() || go->GetGoState() == GO_STATE_ACTIVE)
                return false;
        }
    }

    if (skillId == SKILL_NONE)
        return true;

    if (skillId == SKILL_FISHING)
        return false;

    if (!ai->HasSkill((SkillType)skillId))
        return false;

    // #485: the tool comes first. A level-10 corpse needs skill 0 (Refresh) and returned
    // below before the knife check, so a skinner without a knife still targeted it.
    if (skillId == SKILL_MINING && !bot->HasItemCount(2901, 1))
        return false;

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1))
        return false;

    if (!reqSkillValue)
        return true;

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
        return false;

    return true;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    if (!availableLoot.insert(guid).second)
        return false;

    if (availableLoot.size() < MAX_LOOT_OBJECT_COUNT)
        return true;

    std::vector<LootObject> ordered = OrderByDistance();
    for (size_t i = MAX_LOOT_OBJECT_COUNT; i < ordered.size(); i++)
        Remove(ordered[i].guid);

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
        availableLoot.erase(i);
}

void LootObjectStack::Clear()
{
    availableLoot.clear();
}

bool LootObjectStack::CanLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return !ordered.empty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return ordered.empty() ? LootObject() : *ordered.begin();
}

std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    size_t beforeShrink = availableLoot.size();
    availableLoot.shrink(time(0) - 30);
    if (availableLoot.size() < beforeShrink)
        sLog.outDebug("[BOT LOOT] %s: loot stack expired %zu corpse(s) (>30s old, dropped before looting)",
            bot->GetName(), beforeShrink - availableLoot.size());

    // twow-repo#333 T28b (AiPlayerbot.Gathering.PaceReach): the one place the reach rule lives - the "has available loot" value,
    // LootAction and the travel / rpg pauses all read this list, so they agree on a far node. Off (or out of scope): nullptr.
    PlayerbotAI* const paceAi = (maxDistance > 0 && sPlayerbotAIConfig.gatheringPaceReach) ? GatheringPaceScope(bot) : nullptr;
    uint32 const paceNow = paceAi ? uint32(time(nullptr)) : 0;

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
        else if (paceAi && distance <= float(sPlayerbotAIConfig.gatheringPaceReachYards) && PaceFarOk(paceAi, lootObject, paceNow))
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);
    return result;
}

// twow-repo#333 T28b begin (AiPlayerbot.Gathering.PaceReach, plan A3-PLAN-ZIEL T28b, OB-00 09.11.2026; GatheringPacePolicy.h).
// Writes only this bot's far-pick state (its loot stack), its [Gathering] window counters and - when a far pick ends without
// an open (20 s, or a strong hostile near its node) - its "loot target". No travel target, quest, strategy, chance, drop or price.
PlayerbotAI* ai::GatheringPaceScope(Player* bot)
{
    PlayerbotAI* const botAi = bot ? GetBotAI(bot) : nullptr;
    if (!botAi || !ai::cohort::Applies(sPlayerbotAIConfig.gatheringPaceReach, bot->GetGUIDLow()) || bot->GetGroup() ||
        !IsRosterBotOnItsOwn(botAi))
        return nullptr;
    return botAi;
}

// A node beyond the loot distance, within the reach: herbalism / mining behind its target, orange or yellow, not missed, budget
// left, 0 strong hostiles within 30 yd of the node (one scan per node and 10 s).
bool LootObjectStack::PaceFarOk(PlayerbotAI* ai, LootObject& lootObject, uint32 now)
{
    if (!gathering_pace::IsPaceSkill(lootObject.skillId))
        return false;
    // The target of the gather purpose (TravelValues.cpp): min(GetSkillMax, GetSkillMaxForLevel = 5 x level).
    uint32 const value = bot->GetSkillValue(lootObject.skillId);
    if (!gathering_pace::Behind(value, bot->GetSkillMax(lootObject.skillId), bot->GetSkillMaxForLevel(bot)) ||
        !gathering_pace::FarColorOk(lootObject.reqSkillValue, value))
        return false;
    uint64 const guid = lootObject.guid.GetRawValue();
    if (!pace.FarOk(guid, now))
        return false;
    // Not yet picked: offered only while "loot available" can fire for it (no "all targets", not mounted, LootTriggers.cpp) -
    // else the travel / rpg pause would hold for a node LootAction never picks, and its clock (from the pick) never starts.
    AiObjectContext* const context = ai->GetAiObjectContext();
    if (pace.pending != guid && (!context->GetValue<std::list<ObjectGuid>>("all targets")->Get().empty() ||
        context->GetValue<bool>("mounted", "self target")->Get()))
        return false;
    int clear = pace.HostilesClear(guid, now);
    if (clear < 0)
    {
        WorldObject* wo = lootObject.GetWorldObject(bot);
        clear = wo ? 1 : 0;
        if (wo)
            for (Unit* hostile : ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, gathering_pace::HostileYards))
                if (gathering_pace::Strong(bot->GetLevel(), hostile->GetLevel()))
                {
                    clear = 0;
                    break;
                }
        pace.PutHostiles(guid, now, clear == 1);
    }
    return clear == 1;
}

void ai::GatheringPacePick(PlayerbotAI* ai, LootObject& lootObject)
{
    Player* bot = ai->GetBot();
    if (!GatheringPaceScope(bot))
        return;
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    uint32 const now = uint32(time(nullptr));
    uint64 const guid = lootObject.guid.GetRawValue();
    WorldObject* wo = lootObject.GetWorldObject(bot);
    bool const far = wo && bot->GetDistance(wo) > sPlayerbotAIConfig.lootDistance;
    if (pace.pending && pace.pending != guid && pace.Leave(now))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);
    if (far && pace.Pick(guid, now))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Pick);
}

bool ai::GatheringPaceTimeout(PlayerbotAI* ai, LootObject const& lootObject)
{
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    uint64 const guid = lootObject.guid.GetRawValue();
    uint32 const now = uint32(time(nullptr));
    if (!pace.EndDue(guid, now))
        return false;
    // Within the loot distance the near rules apply as before: a node reached at 19.8 s still opens (and counts as a far open).
    Player* bot = ai->GetBot();
    LootObject target = lootObject;
    WorldObject* wo = target.GetWorldObject(bot);
    if (wo && bot->GetDistance(wo) <= sPlayerbotAIConfig.lootDistance)
        return false;
    // MoveToLootAction follows the loot target without a distance cap: the far pick ends here.
    pace.End(guid, now);
    ai->GetAiObjectContext()->GetValue<LootObject>("loot target")->Set(LootObject());
    ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Timeout);
    return true;
}

void ai::GatheringPaceOpened(PlayerbotAI* ai, LootObject const& lootObject)
{
    gathering_pace::State& pace = ai->GetAiObjectContext()->GetValue<LootObjectStack*>("available loot")->Get()->Pace();
    if (pace.Opened(lootObject.guid.GetRawValue()))
        ai->GetGatheringTrace().NoteFar(gathering_trace::FarEvent::Open);
}
// twow-repo#333 T28b end


// twow-repo#575 H1 (AiPlayerbot.Loot.HoldTravelWhileLootable, LootHoldPolicy.h): the bot's loot target is open for a hold - set,
// within the loot distance of the bot, still lootable and its hold younger than Loot.HoldTravelWhileLootable.MaxSeconds (OB-10
// review 09.11.: the hold clock per loot target guid). Writes only this bot's own hold clock ("manual string" / "manual time",
// qualifiers "loot hold guid" / "loot hold since"); the loot target itself is not set or reset here.
bool ai::loot_hold::LootTargetOpen(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    AiObjectContext* context = ai->GetAiObjectContext();
    LootObject lootObject = context->GetValue<LootObject>("loot target")->Get();
    if (lootObject.IsEmpty())
        return false;
    WorldObject* wo = lootObject.GetWorldObject(bot);
    if (!wo || !ai::loot_hold::WithinLootDistance(bot->GetDistance(wo), sPlayerbotAIConfig.lootDistance))
        return false;
    if (!lootObject.IsLootPossible(bot))
        return false;

    Value<std::string>* const heldGuidValue = context->GetValue<std::string>("manual string", "loot hold guid");
    Value<time_t>* const heldSinceValue = context->GetValue<time_t>("manual time", "loot hold since");
    std::uint64_t const guid = lootObject.guid.GetRawValue();
    std::string const heldGuidText = heldGuidValue->Get();
    std::uint64_t const heldGuid = heldGuidText.empty() ? 0 : std::strtoull(heldGuidText.c_str(), nullptr, 10);
    time_t const now = time(nullptr);
    time_t const heldSince = heldSinceValue->Get();
    ai::loot_hold::Step const step = ai::loot_hold::Next(guid, heldGuid, std::int64_t(heldSince), std::int64_t(now),
        sPlayerbotAIConfig.lootHoldTravelWhileLootableMaxSeconds);
    if (step == ai::loot_hold::Step::Start)
    {
        heldGuidValue->Set(std::to_string(guid));
        heldSinceValue->Set(now);
    }
    else if (step == ai::loot_hold::Step::Expire)
    {
        sLog.outBasic("[LootHold] bot=%u level=%u map=%u target_entry=%u target_low=%u kind=%s held_s=%u result=expired",
            bot->GetGUIDLow(), bot->GetLevel(), bot->GetMapId(), lootObject.guid.GetEntry(), lootObject.guid.GetCounter(),
            lootObject.guid.IsGameObject() ? "go" : "creature", uint32(now - heldSince));
        heldSinceValue->Set(0);   // this guid holds no more until the bot takes another loot target
    }
    return ai::loot_hold::Holds(step);
}
