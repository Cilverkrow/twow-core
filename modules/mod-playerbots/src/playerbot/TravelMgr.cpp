#include "playerbot/TravelMgr.h"
#include "playerbot/GatherNodePolicy.h"
#include "playerbot/PathFailPolicy.h"
#include "playerbot/strategy/actions/MovementActions.h"   // twow-repo#342 (S1): TraceTransportEdgeStallAbandon
#include "playerbot/PathUnitlessBotPolicy.h"
#include "playerbot/UnreachableTargetPolicy.h"
#include "playerbot/StartZonePolicy.h"
#include <numeric>
#include "playerbot/QuestAreaLevelPolicy.h"
#include "playerbot/QuestGrindModePolicy.h"
#include "playerbot/QuestSearchPolicy.h"
#include "playerbot/QuestFeasibilityPolicy.h"
#include "playerbot/QuestGroupWorld.h"   // twow-repo#627 S7 hooks (CLI-Deep): help-group members out of PosStuck, S3c, turn-in recovery
#include "playerbot/AdhocGroupWorld.h"   // twow-repo#601 F5b: a bot-only group shares the kill credit (CLI-GRP 25.10.)
#include "playerbot/CohortPolicy.h"
#include "playerbot/ContestedObjectivePolicy.h"
#include <algorithm>
#include <vector>
#include <iomanip>

#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "Maps/PathFinder.h"
#include "TravelNode.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"
#include "BotTests.h"
#include "ObjectAccessor.h"
#include "playerbot/HomeBindPolicy.h"
#include "playerbot/RidingStagesBotPolicy.h"
#include "playerbot/DirectTargetDestinations.h"
#include "Formulas.h"
#include "playerbot/strategy/values/RestGateValue.h"
#include "playerbot/strategy/values/QuestValues.h"   // twow-repo#601 S5a: DialogStatusValue::getDialogStatus (band giver filter F)
#include "playerbot/QuestBand.h"   // twow-repo#601 S5a (BandLadder): the band trip hooks, the ladder loader
#include "playerbot/CohortPolicy.h"   // twow-repo#601 QuestLoop.InactiveAfterProgress mode 2 = cohort B

using namespace ai;
using namespace MaNGOS;

namespace
{
// twow-repo#601 (QuestLoop.InactiveAfterProgress): the bot's count on a quest objective destination - quest items for a loot
// objective, kill / use credit otherwise; -1 when the destination is no objective or the quest is not in the log.
int32 ObjectiveCountOf(Player* bot, TravelDestination const* destination)
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(destination);
    Quest const* quest = objective ? sObjectMgr.GetQuestTemplate(objective->GetQuestId()) : nullptr;
    if (!bot || !quest)
        return -1;
    auto const it = bot->getQuestStatusMap().find(objective->GetQuestId());
    if (it == bot->getQuestStatusMap().end())
        return -1;
    uint8 const index = objective->GetObjective();
    return quest->ReqItemCount[index] > 0 ? int32(it->second.m_itemcount[index]) : int32(it->second.m_creatureOrGOcount[index]);
}

// twow-repo#601 (QuestLoop.GiverTakerDone): a giver / taker destination whose quest this bot has taken (giver) or turned in (taker);
// in the log = incomplete or complete and not rewarded. False for every other destination.
bool GiverTakerTripDone(Player* bot, TravelDestination const* destination)
{
    QuestRelationTravelDestination const* relation = dynamic_cast<QuestRelationTravelDestination const*>(destination);
    if (!bot || !relation)
        return false;
    uint32 const questId = relation->GetQuestId();
    bool const rewarded = bot->GetQuestRewardStatus(questId);
    QuestStatus const status = bot->GetQuestStatus(questId);
    bool const inLog = !rewarded && (status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE);
    return quest_loop::GiverTakerTripDone(relation->GetRelation(), inLog, rewarded);
}
}

// Penqle's Singleton<> requires an explicit instantiation in a .cpp file.
INSTANTIATE_SINGLETON_1(ai::TravelMgr);

PlayerTravelInfo::PlayerTravelInfo(Player* player, bool gapGrind)
{
    PlayerbotAI* ai = GetBotAI(player);
    AiObjectContext* context = ai->GetAiObjectContext();

    position = player;

    team = player->GetTeam();
    level = player->GetLevel();
    currentSkill[SKILL_MINING] = player->GetSkillValue(SKILL_MINING);
    currentSkill[SKILL_HERBALISM] = player->GetSkillValue(SKILL_HERBALISM);
    currentSkill[SKILL_FISHING] = player->GetSkillValue(SKILL_FISHING);
    currentSkill[SKILL_SKINNING] = player->GetSkillValue(SKILL_SKINNING);

    skillMax[SKILL_MINING] = player->GetSkillMax(SKILL_MINING);
    skillMax[SKILL_HERBALISM] = player->GetSkillMax(SKILL_HERBALISM);
    skillMax[SKILL_FISHING] = player->GetSkillMax(SKILL_FISHING);
    skillMax[SKILL_SKINNING] = player->GetSkillMax(SKILL_SKINNING);

    money = player->GetMoney();

    if (player->GetGroup())
        groupSize = player->GetGroup()->GetMembersCount();

    focusList = AI_VALUE(focusQuestTravelList, "focus travel target");

    for (auto& [valueName, value] : boolValues)
        value = AI_VALUE(bool, valueName);

    for (auto& [valueName, value] : uint8Values)
        value = AI_VALUE(uint8, valueName);

    for (auto& [valueName, value] : uint32Values)
        value = AI_VALUE(uint32, valueName);

    // twow-repo#601 S2 R3 (GrindMode 1, OB-10 rev. 2 point 1): the scope bit, here on the bot thread - only the gap filler's
    // population gets its creature level window [max(gray + 1, L - 5), L + 1]; the async grind fetch reads it only as a
    // value. GrindMode 0 / 2 and every other bot: 0 / 0 (IsQuestGapGrind reads the config first). Only a snapshot for a gap
    // grind (gapGrind: the grind fetch, its choice, the gap trip; review C-F2) - a quest objective fetch, choice or trip to grind
    // mobs keeps the old template test and area gate.
    if (ai->IsQuestGapGrind() && gapGrind)
    {
        gapWindowLow = ai::quest_grind_mode::GapWindowLowLevel(level, MaNGOS::XP::GetGrayLevel(level));
        gapWindowHigh = ai::quest_grind_mode::GapWindowHighLevel(level);
    }

    // twow-repo#601 S5a (BandLadder, S5A-DESIGN.md 5.4): the band giver filter bit, here on the bot thread - only an in-scope bot
    // whose band has a usable ladder row (QuestBandGiverFilter reads the switch first); the giver checks of IsPossible (E) and
    // IsActive (F) read it only as a value. BandLadder 0 and every other bot: false / 0, as before.
    bandGiverFilter = ai->QuestBandGiverFilter();
    bandGreyLevel = bandGiverFilter ? MaNGOS::XP::GetGrayLevel(level) : 0;
}

std::string EntryTravelDestination::GetShortName() const
{
    switch (purpose)
    {
    case TravelDestinationPurpose::QuestGiver:
        return "questgiver";
    case TravelDestinationPurpose::QuestObjective1:
    case TravelDestinationPurpose::QuestObjective2:
    case TravelDestinationPurpose::QuestObjective3:
    case TravelDestinationPurpose::QuestObjective4:
        return "questobjective";
    case TravelDestinationPurpose::QuestTaker:
        return "questtaker";
    case TravelDestinationPurpose::Vendor:
        return "vendor";
    case TravelDestinationPurpose::AH:
        return "ah";
    case TravelDestinationPurpose::Repair:
        return "repair";
    case TravelDestinationPurpose::Mail:
        return "mail";
    case TravelDestinationPurpose::Trainer:
        return "trainer";
    case TravelDestinationPurpose::Explore:
        return "explore";
    case TravelDestinationPurpose::GenericRpg:
        return "rpg";
    case TravelDestinationPurpose::Grind:
        return "grind";
    case TravelDestinationPurpose::Boss:
        return  "boss";
    case TravelDestinationPurpose::GatherFishing:
    case TravelDestinationPurpose::GatherHerbalism:
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherSkinning:
        return "gather";
    default:
        return "unknown";
    }
    return "none";
};

std::string QuestTravelDestination::GetTitle() const {
    return ChatHelper::formatQuest(GetQuestTemplate());
}

bool QuestRelationTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!info.GetBoolValue2("has strategy", "rpg quest"))
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    if (forceThisQuest && !info.IsFocusQuest(GetQuestId()))
        return false;

    const Quest* quest = GetQuestTemplate();

    if (GetRelation() == 0)
    {
        if (!forceThisQuest && (int32)quest->GetQuestLevel() >= (int32)info.GetLevel() + (int32)5)
            return false;

        if ((int32)info.GetLevel() < quest->GetMinLevel() || (int32)info.GetLevel() > quest->GetMaxLevel())
            return false;

        // twow-repo#601 S5a (BandLadder, giver filter E): an in-scope bot whose band has a usable ladder row skips the giver of a
        // quest the accept rule would refuse (grey or red; class quests stay; QuestAcceptPolicy.h), so a worked-out zone comes
        // back empty. The snapshot bit only (async job); quest level 0 = the bot's level (Player::GetQuestLevelForPlayer).
        if (!forceThisQuest && info.HasBandGiverFilter() && ai::quest_band::GreyGiverSkip(info.GetLevel(),
                quest->GetQuestLevel() > 0 ? uint32(quest->GetQuestLevel()) : info.GetLevel(), info.GetBandGreyLevel(),
                quest->GetRequiredClasses() != 0))
            return false;

        if (OnMap(info.GetPosition())) //CanTakeQuest will check required conditions which will fail on a different map.
            if (quest->GetRequiredCondition())          //So we skip this quest for now.
                return false;

        if (!info.HasFocusQuest())
        {
            if (info.GetBoolValue("can fight equal"))
            {
                if (info.GetUint8Value("free quest log slots") < 5)
                    return false;
            }
            else
            {
                if (info.GetUint8Value("free quest log slots") < 10)
                    return false;
            }

            //Do not try to pick up dungeon/elite quests in instances without a group.
            if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) && !info.GetBoolValue("can fight boss"))
                return false;
        }
    }
    else
    {
        // twow-repo#485: IsOverWorld() checks the taker's map closest to the bot,
        // not the bot; skip a taker inside an instance, as the objectives below do.
        //Do not try to hand-in dungeon/elite quests in instances without a group.
        if ((quest->GetType() == QUEST_TYPE_ELITE || quest->GetType() == QUEST_TYPE_DUNGEON) && !info.GetBoolValue("can fight boss"))
        {
            if (!IsOverWorld(info.GetPosition()))
                return false;
        }
    }

    return true;
}

bool QuestRelationTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const {
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if(!IsPossible(info))
        return false;

    bool forceThisQuest = info.HasFocusQuest(); //Checked in IsPossible if it's 'this' quest.

    if (GetRelation() == 0)
    {
        // twow-repo#601 S1 (AbandonStructuralParked): no trip to the giver of a quest this bot abandoned after two
        // structural parks - it would be taken, parked and abandoned again every hour.
        if (sPlayerbotAIConfig.questFirstProgressionAbandonStructuralParked && !ai->HasRealPlayerMaster() &&
            AI_VALUE(TravelTarget*, "travel target")->IsStructurallyAbandoned(GetQuestId()))
            return false;

        // twow-repo#601 S3a (QuestFeasibility.Enable): no trip to a skipped quest's giver; never the taker (rev. 2).
        if (sPlayerbotAIConfig.questFeasibilityEnable && ai->SkipInfeasibleQuest(GetQuestTemplate(), ai::quest_feasibility::Path::Giver))
            return false;

        if (!bot->GetMap()->IsContinent())
        {
            // twow-repo#485: a giver can have no point (a script-only game object with
            // SkipScriptOnlyQuestTakers = 1); GetClosestPoint is null then.
            WorldPosition const* closestPoint = GetClosestPoint(bot);
            if (!closestPoint || closestPoint->getMapId() != bot->GetMapId()) //This gives issues for bot->CanTakeQuest so stop here.
                return false;
        }

        // twow-repo#601 S5a (BandLadder, giver filter F, OB-10 Q1 19.10.2026): for an in-scope bot whose band has a usable ladder
        // row the giver counts only while it offers this quest to the bot (DIALOG_STATUS_AVAILABLE; a quest in the log, done or
        // repeatable is no reason to stay). One static per-quest status, no value per creature. Snapshot bit false: as before.
        if (!forceThisQuest && info.HasBandGiverFilter() &&
            DialogStatusValue::getDialogStatus(bot, GetEntry(), GetQuestId()) != DIALOG_STATUS_AVAILABLE)
            return false;

        if (forceThisQuest)
        {
            if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()))) //Noone can do this quest.
                    return false;
        }
        else
        {
            if (info.GetBoolValue("can fight equal"))
            {
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                    if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()) + ",need quest reward::" + std::to_string(GetQuestId()))) //Noone can do this quest for a usefull reward.
                        return false;
            }
            else
            {
                if (!AI_VALUE2(bool, "group or", "following party,can accept quest npc::" + std::to_string(GetEntry()))) //Noone has yellow exclamation mark.
                    if (!AI_VALUE2(bool, "group or", "following party,can accept quest low level npc::" + std::to_string(GetEntry()))) //Noone can pick up this quest for money.
                        return false;
            }
        }
    }
    else
    {
        if (!AI_VALUE2(bool, "group or", "following party,can turn in quest npc::" + std::to_string(GetEntry())))
            return false;
    }

    if (GetEntry() > 0)
    {
        return !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot);
    }

    return true;
}

std::string QuestRelationTravelDestination::GetTitle() const {
    std::ostringstream out;

    out << "talk to ";

    if (GetRelation() == 0)
        out << "questgiver ";
    else
        out << "questtaker ";

    out << ChatHelper::formatWorldEntry(GetEntry());
    return out.str();
}

bool QuestObjectiveTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!info.GetBoolValue2("has strategy", "rpg quest"))
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    if (forceThisQuest && !info.IsFocusQuest(GetQuestId()))
        return false;

    bool skipShouldGrindCheck = false;

    //Check mob level
    if (GetEntry() > 0)
    {
#ifdef MANGOSBOT_TWO
        switch (GetQuestId())
        {
            case 12779: //An End To All Things
                skipShouldGrindCheck = true;
        }
#endif
    }

    if (!forceThisQuest)
    {
        if ((int32)GetQuestTemplate()->GetQuestLevel() > (int32)info.GetLevel() + (int32)1)
            return false;

        if (!skipShouldGrindCheck && GetQuestTemplate()->GetQuestLevel() + 5 > (int)info.GetLevel() && !info.GetBoolValue("can fight equal"))
            return false;
    }

    if (info.IsInRaid() != (GetQuestTemplate()->GetType() == QUEST_TYPE_RAID))
        return false;


    bool skipKillableCheck = false;

    //Check mob level
    if (GetEntry() > 0)
    {
#ifdef MANGOSBOT_TWO        
        switch (GetQuestId()) {
        case 12680: //Grand Theft Palomino
        case 12687: //Into the Realm of Shadows
        case 12698: //The Gift That Keeps On Giving
        case 12779: //An End To All Things
        case 12801: //The Light of Dawn
        case 12701: //Massacre At Light's Point
            skipKillableCheck = true;
        }
#endif

        CreatureInfo const* cInfo = GetCreatureInfo();

        if (cInfo->NpcFlags & UNIT_NPC_FLAG_VENDOR && GetQuestTemplate()->ReqItemId[GetObjective()])
        {
            ItemPrototype const* proto = sObjectMgr.GetItemPrototype(GetQuestTemplate()->ReqItemId[GetObjective()]);
            if (GetQuestTemplate()->ReqItemCount[GetObjective()] * proto->BuyPrice > info.GetMoney()) //Need more money.
                return false;

            skipKillableCheck = true;
        }

        if (!skipKillableCheck && !forceThisQuest)
        {
            if (cInfo && (int)cInfo->MaxLevel - (int)info.GetLevel() > 4)
                return false;

            //Do not try to hand-in dungeon/elite quests in instances without a group.
            if (cInfo->Rank > CREATURE_ELITE_NORMAL)
            {
                if (!IsOverWorld(info.GetPosition()) && info.GetPosition().getMapId() != 609 && !info.GetBoolValue("can fight boss"))
                    return false;
                else if (!info.GetBoolValue("can fight elite"))
                    return false;
            }
        }
    }

    if (!forceThisQuest)
    {
        if (!skipKillableCheck && GetQuestTemplate()->GetType() == QUEST_TYPE_ELITE && !info.GetBoolValue("can fight elite"))
            return false;

        //Do not try to do dungeon/elite quests in instances without a group.
        if ((GetQuestTemplate()->GetType() == QUEST_TYPE_ELITE || GetQuestTemplate()->GetType() == QUEST_TYPE_DUNGEON || GetQuestTemplate()->GetType() == QUEST_TYPE_RAID) && !info.GetBoolValue("can fight boss"))
        {
            if (!IsOverWorld(info.GetPosition()))
                return false;
        }

        //Do not try to do pvp quests in bg's (no way to travel there). 
        if (GetQuestTemplate()->GetType() == QUEST_TYPE_PVP)
        {
            if (!IsOverWorld(info.GetPosition()))
                return false;
        }
    }

    return true;
}

bool QuestObjectiveTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const {
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    bool forceThisQuest = info.HasFocusQuest();

    bool skipKillableCheck = false;

    if (GetEntry() > 0)
    {
#ifdef MANGOSBOT_TWO
        switch (GetQuestId())
        {
            case 12848: //The Endless Hunger
                skipKillableCheck = true;
                break;
            case 12680: //Grand Theft Palomino
                switch (GetEntry())
                {
                    case 28605: //Havenshire horse
                    case 28606:
                    case 28607: return !AI_VALUE2(bool, "trigger active", "in vehicle") && ai->CanSpellClick(bot, GetEntry()); break;
                    case 28653: //Salanar the Horseman
                        return true;
                        break;
                }
                break;
            case 12687: //Into the Realm of Shadows
                switch (GetEntry())
                {
                    case 28768: //Dark Rider of Acherus
                    case 28909:
                    case 28782: //Acherus Deathcharger
                        return !AI_VALUE2(bool, "trigger active", "in vehicle");
                    case 29501: //Scourge Gryphon (return to hand in)
                        return AI_VALUE2(bool, "trigger active", "in vehicle");
                }
                break;
            case 12698: //The Gift That Keeps On Giving
                switch (GetEntry())
                {
                    case 28819: //Scarlet Miner
                        return !bot->FindGuardianWithEntry(28845);
                    case 28658: //Gothic the Harvester
                        return bot->FindGuardianWithEntry(28845);
                }
                break;
            case 12701: //Massacre At Light's Point
                switch (GetEntry())
                {
                    case 28833: //Scarlet Cannon
                        return DistanceTo(bot) < 30.0f;
                }
        }
#endif

        CreatureInfo const* cInfo = GetCreatureInfo();

        if (!skipKillableCheck && cInfo->NpcFlags & UNIT_NPC_FLAG_VENDOR && GetQuestTemplate()->ReqItemId[GetObjective()] &&
            !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot))
        {
            skipKillableCheck = true;
        }
    }

    if (!skipKillableCheck)
        skipKillableCheck = ai->CanSpellClick(bot, GetEntry());

    std::vector<std::string> qualifier = { std::to_string(GetQuestTemplate()->GetQuestId()), std::to_string(GetObjective()) };

    if (!AI_VALUE2(bool, "group or", "following party,need quest objective::" + Qualified::MultiQualify(qualifier, ","))) //Noone needs the quest objective.
        return false;

    WorldPosition botPos(bot);

    if (!skipKillableCheck && GetEntry() > 0 && !IsOut(botPos))
    {
        TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

        //Only look for the target if it is unique or if we are currently working on it.
        if (IsUnique() || (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK && target->GetEntry() == GetEntry()))
        {
            std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

            for (auto& target : targets)
                if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                    return true;

            return false;
        }
    }

    return true;
}

std::string QuestObjectiveTravelDestination::GetTitle() const {
    std::ostringstream out;

    if (GetQuestTemplate()->ReqItemCount[GetObjective()] > 0)
        out << "loot " << ChatHelper::formatItem(sObjectMgr.GetItemPrototype(GetQuestTemplate()->ReqItemId[GetObjective()]), 0, 0) << " from";
    else if (GetEntry() > 0)
        out << "kill";
    else
        out << "use";

    out << " " << ChatHelper::formatWorldEntry(GetEntry());

    out << " (objective " << (GetObjective() + 1) << ")";

    return out.str();
}

uint8 QuestObjectiveTravelDestination::GetObjective() const
{
    switch (GetPurpose())
    {
    case TravelDestinationPurpose::QuestObjective1:
        return 0;
    case TravelDestinationPurpose::QuestObjective2:
        return 1;
    case TravelDestinationPurpose::QuestObjective3:
        return 2;
    case TravelDestinationPurpose::QuestObjective4:
        return 3;
    default:
        return 0;
    }
    return 0;
}

bool RpgTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    // Don't send low-level bots on RPG travel — the path to any NPC typically
    // crosses level 5+ mobs that kill level 1-4 bots instantly, creating a death loop.
    if (info.GetLevel() < 5)
        return false;

    // Don't send a bot to an NPC sitting in a zone far above its level — the journey
    // crosses (and the destination sits among) mobs that farm the bot into a death
    // spiral at the local high-level graveyard. Mirrors the grind-target level gate
    // (GrindTravelDestination::IsPossible). Margin matches the quest-level gate (+5).
    // getAreaLevel() returns -1/-2 for unknown areas; only reject on a real level.
    int32 destAreaLevel = GuidPosition(HIGHGUID_UNIT, GetEntry()).getAreaLevel();
    if (destAreaLevel > 0 && destAreaLevel > (int32)info.GetLevel() + 5)
        return false;

    //Horde pvp baracks
    if (ClosestMapId(info.GetPosition()) == 450 && info.GetTeam() == ALLIANCE)
        return false;

    //Alliance pvp baracks
    if (ClosestMapId(info.GetPosition()) == 449 && info.GetTeam() == HORDE)
        return false;

    return true;
}

bool RpgTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;   

    //Once the target rpged with it is added to the ignore list. We can now move on.
    std::set<ObjectGuid>& ignoreList = AI_VALUE(std::set<ObjectGuid>&,"ignore rpg target");

    for (auto& i : ignoreList)
    {
        if (i.GetEntry() == GetEntry())
        {
            return false;
        }
    }    

    return !GuidPosition(HIGHGUID_UNIT, GetEntry()).IsHostileTo(bot);
}

std::string RpgTravelDestination::GetTitle() const 
{
    std::ostringstream out;

    switch (GetPurpose())
    {    
    case TravelDestinationPurpose::Vendor:
        out << "sell items to";
        break;
    case TravelDestinationPurpose::AH:
        out << "put items on auction at";
        break;
    case TravelDestinationPurpose::Repair:
        out << "repair at";
        break;
    case TravelDestinationPurpose::Mail:
        out << "receive mail from";
        break;
    case TravelDestinationPurpose::Trainer:
        out << "train a skill at";
        break;
    case TravelDestinationPurpose::GenericRpg: 
        out << "find"; //Named travel purpose.
        break;
    default:
        out << "";
        break;
    }

    /*
    out << " " << GetShortName();

    if (GetEntry() > 0)
        out << " npc ";
    else
        out << " object ";

    */

    out << " ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

AreaTableEntry const* ZoneTravelDestination::GetArea() const
{
    for (uint32 areaid = 0; areaid <= sAreaStore.GetNumRows(); ++areaid)
    {
        AreaTableEntry const* areaEntry = sAreaStore.LookupEntry<AreaEntry>(areaid);
        if (areaEntry && areaEntry->ID == GetEntry())
        {
            return areaEntry;
        }
    }

    return nullptr;
}

bool ExploreTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    AreaTableEntry const* area = GetArea();

    if (GetLevel() && (uint32)GetLevel() > info.GetLevel() && info.GetLevel() < DEFAULT_MAX_LEVEL)
        return false;

    return true;
}

bool ExploreTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    if (!IsPossible(info))
        return false;

    AreaTableEntry const* area = GetArea();

    if (area->exploreFlag == 0xffff)
        return false;
    int offset = area->exploreFlag / 32;

    uint32 val = (uint32)(1 << (area->exploreFlag % 32));
    uint32 currFields = bot->GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);

    return !(currFields & val);    
}

bool GrindTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (info.GetBoolValue("should sell") && (info.GetBoolValue("can sell") || info.GetBoolValue("can ah sell")))
        return false;

    CreatureInfo const* cInfo = GetCreatureInfo();

#ifdef MANGOSBOT_TWO
    if (cInfo->Rank == CREATURE_ELITE_NORMAL && cInfo->MinLootGold == 0 && info.GetPosition().getMapId() == 609)
        return true;
#endif

    int32 botLevel = info.GetLevel();

    uint8 botPowerLevel = info.GetUint8Value("durability");
    float levelMod = botPowerLevel / 500.0f; //(0-0.2f)
    float levelBoost = botPowerLevel / 50.0f; //(0-2.0f)

    int32 maxLevel = std::max(botLevel * (0.5f + levelMod), botLevel - 5.0f + levelBoost);

    // twow-repo#601 S2 R3 (GrindMode 1, OB-10 rev. 2 point 1): the gap filler's population grinds where the template MaxLevel
    // is inside [max(gray + 1, L - 5), L + 1] instead of the durability window below (money and elite tests stay). The
    // window is a value of the snapshot, set on the bot thread (PlayerTravelInfo); every other bot: the old window.
    bool const gapWindow = info.HasGapWindow();
    if (gapWindow && !ai::quest_grind_mode::InGapWindow(info.GetGapWindowLow(), info.GetGapWindowHigh(), cInfo->MaxLevel))
        return false;

    if (!gapWindow && (int32)cInfo->MaxLevel > maxLevel) //@lvl5 max = 3, @lvl60 max = 57
        return false;

    int32 minLevel = std::max(botLevel * (0.4f + levelMod), botLevel - 12.0f + levelBoost);

    if (!gapWindow && (int32)cInfo->MaxLevel < minLevel) //@lvl5 min = 3, @lvl60 max = 50
        return false;

    if (cInfo->MinLootGold == 0)
        return false;

    if (cInfo->Rank > CREATURE_ELITE_NORMAL && !info.GetBoolValue("can fight elite"))
        return false;

    return true;
}

bool GrindTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    // twow-repo#601 GrindMode 2 (owner 17.10.2026): the Grind bit of the shared entry purpose map (TravelValues.cpp,
    // creatures that drop money) makes every such creature a grind destination. For a quest-first roster bot on its
    // own only the ones needed for an active quest stay (quest objective mobs, e.g. the guild-order QuestReward
    // objectives); a pure grind destination is not active. One sorted per-bot list, no value per creature.
    if (ai->IsQuestNoGrindTrip())   // + QuestFirstOrder (#601 review)
    {
        std::vector<int32> const& needed = AI_VALUE(std::vector<int32>, "needed quest entries");
        if (!ai::quest_grind_mode::GrindDestinationActive(true, std::binary_search(needed.begin(), needed.end(), GetEntry())))
            return false;
    }

    return GuidPosition(bot).IsHostileTo(GuidPosition(HIGHGUID_UNIT, GetEntry()), bot->GetInstanceId());
}

std::string GrindTravelDestination::GetTitle() const
{
    std::ostringstream out;

    out << "get xp or gold from killing ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

bool BossTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    if (!info.GetBoolValue("can fight boss"))
        return false;

    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(GetEntry());

    if ((int32)cInfo->MaxLevel > info.GetLevel() + 3)
        return false;
    
    const MapEntry* mapEntry = ClosetMapEntry(info.GetPosition());
    
    if (info.IsInGroup())
    {
        if (info.IsInRaid())
        {
#ifndef MANGOSBOT_TWO
            if (mapEntry && mapEntry->IsNonRaidDungeon())
#else
            if (mapEntry && mapEntry->IsNonRaidDungeon())
#endif
                return false;
        }
        else if (mapEntry && mapEntry->IsRaid())
            return false;
    }

    //Ragefire casm
    if (ClosestMapId(info.GetPosition()) == 389 && info.GetTeam() == ALLIANCE)
        return false;

    //Stockades
    if (ClosestMapId(info.GetPosition()) == 34 && info.GetTeam() == HORDE)
        return false;

    //Do not move to overworld bosses/uniques that are far away.
    if (mapEntry && mapEntry->IsContinent() && DistanceTo(info.GetPosition()) > 2000.0f)
        return false;

    return true;
}

bool BossTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;

    if (!GuidPosition(bot).IsHostileTo(GuidPosition(HIGHGUID_UNIT, GetEntry()), bot->GetInstanceId()))
        return false;

    WorldPosition botPos(bot);

    if (!IsOut(botPos))
    {
        std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

        for (auto& target : targets)
            if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                return true;

        return false;
    }

    if (!AI_VALUE2(bool, "has upgrade",  GetEntry()))
        return false;

    return true;
}

std::string BossTravelDestination::GetTitle() const
{
    std::ostringstream out;

    out << "get loot from ";

    out << ChatHelper::formatWorldEntry(GetEntry());

    return out.str();
}

bool GatherTravelDestination::IsPossible(const PlayerTravelInfo& info) const
{
    uint32 skillId = SKILL_NONE;
    uint32 reqSkillValue = 0;

    if (GetPurpose() == TravelDestinationPurpose::GatherFishing)
    {
        skillId = SKILL_FISHING;

        // twow-repo#596 / P3-b review (CLI-PATH 18.10.2026): this branch declared its own int32 reqSkillValue, so the
        // outer one stayed 0 and the skill check below never applied to fishing - fishing skill 1 counted as enough
        // for every zone. AiPlayerbot.Gather.FishingSkillCheck = 1 hands the zone's value to the check (0 = as before).
        int32 fishingSkillValue = sObjectMgr.GetFishingBaseSkillLevel(GetEntry());

        if (!fishingSkillValue) //no fishable zone or area should be 0
            return false;

        if (fishingSkillValue > 5)
            fishingSkillValue -= 5; //Give chance to levelup.

        if (sPlayerbotAIConfig.gatherFishingSkillCheck)
            reqSkillValue = uint32(std::max<int32>(1, fishingSkillValue));
    }
    else if (GetEntry() > 0)
    {
        CreatureInfo const* cInfo = GetCreatureInfo();

        if (!cInfo)
            return false;

        skillId = cInfo->GetRequiredLootSkill();
        uint32 targetLevel = cInfo->MaxLevel;
        reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
    }
    else
    {
        GameObjectInfo const* goInfo = GetGoInfo();

        if (!goInfo)
            return false;

        uint32 lockId = goInfo->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        // #414: only real herb/ore nodes. A lock slot that asks for a skill no
        // profession provides (e.g. "open kneeling") marks a quest object.
        bool lockHasNonProfessionSkill = false;
        for (int i = 0; i < 8; ++i)
            if (lockInfo->Type[i] == LOCK_KEY_SKILL && lockInfo->Index[i] && SkillByLockType(LockType(lockInfo->Index[i])) == 0)
                lockHasNonProfessionSkill = true;

        if (!ai::gather_node::IsGatherNode(goInfo->type == GAMEOBJECT_TYPE_CHEST, lockHasNonProfessionSkill))
            return false;

        for (int i = 0; i < 8; ++i)
        {
            if (lockInfo->Type[i] == LOCK_KEY_SKILL)
                if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                {
                    skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                    reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                    break;
                }
        }
    }

    if (!info.GetCurrentSkill((SkillType)skillId))
        return false;

    uint32 skillValue = uint32(info.GetCurrentSkill((SkillType)skillId));
    if (reqSkillValue > skillValue)
        return false;

    if (info.GetSkillMax((SkillType)skillId) <= skillValue) //Not able to increase skill.
        return false;

    if (GetPurpose() != TravelDestinationPurpose::GatherFishing && reqSkillValue + 100 < skillValue) //Gray level = no skillup
        return false;

    return true;
}

bool GatherTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const
{
    PlayerbotAI* ai = GetBotAI(bot);
    AiObjectContext* context = ai->GetAiObjectContext();

    if (!IsPossible(info))
        return false;   


    if (GetPurpose() != TravelDestinationPurpose::GatherFishing)
    {
        if (!IsOut(bot))
        {
            if (this->GetEntry() > 0)
            {
                std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "possible targets");

                for (auto& target : targets)
                    if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())
                        return true;
            }
            else
            {
                std::list<ObjectGuid> targets = AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");

                for (auto& target : targets)
                    if (target.GetEntry() == GetEntry())
                        return true;
            }

            return false;
        }
    }

    return true;
}

std::string GatherTravelDestination::GetTitle() const {
    std::ostringstream out;

    if (GetPurpose() == TravelDestinationPurpose::GatherFishing)
    {
        out << "fish";
    }
    else
    {   
        switch (GetPurpose())
        {
            case TravelDestinationPurpose::GatherSkinning:
                out << "skin ";
                break;
            case TravelDestinationPurpose::GatherMining:
                out << "mine ";
                break;
            case TravelDestinationPurpose::GatherHerbalism:
                out << "gather from ";
                break;
        }

        out << ChatHelper::formatWorldEntry(GetEntry());
    }

    return out.str();
}

TravelTarget::TravelTarget(PlayerbotAI* ai) : AiObject(ai)
{
    sTravelMgr.SetNullTravelTarget(this);
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
}

void TravelTarget::SetTarget(TravelDestination* tDestination1, WorldPosition* wPosition1) {
    // twow-repo#594 / P3-b: a direct-target trip ends where its target leaves the destination - a reset (null target),
    // a group copy, any new choice ([DirectTarget] state=trip_end).
    if (ai::direct_targets::AnyBuilt())
        ai::direct_targets::OnTargetChange(ai, tDestination, tDestination1);

    if (dynamic_cast<TemporaryTravelDestination*>(tDestination) && tDestination1 != tDestination)
        delete tDestination;

    // twow-repo#601 S2 / S4 R2: a gap grind window belongs to its destination; another one (world buff, go, null target)
    // never inherits it. The refresh keeps the destination and so the window end (no renewal). CopyTarget sets it again.
    if (tDestination1 != tDestination)
        gapWindowEnd = 0;

    // twow-repo#601 S5a (BandLadder): a band trip another destination replaces (zone escape, death suppression, park, rescue,
    // battleground, world buff, go, a reset) ends here - one skip line (replaced / park), no failure; bandTripEnd goes to 0.
    if (tDestination1 != tDestination && bandTripEnd)
        NoteBandTripReplaced();

    // twow-repo#601 (QuestLoop.InactiveAfterProgress): a new destination takes the objective's count; a refresh of the same one
    // keeps it (progress since the target was first set). Off: never read.
    if (sPlayerbotAIConfig.questLoopInactiveAfterProgress && tDestination1 != tDestination)
        objectiveCountAtStart = ObjectiveCountOf(bot, tDestination1);

    // twow-repo#601 (QuestLoop.InactiveReselect): the fallback belongs to the target that ended through an emptied camp only.
    if (tDestination1 != tDestination)
        inactiveReselectPending = false;

    wPosition = wPosition1;
    tDestination = tDestination1;

    // #329: only a different quest target restarts the observed progress;
    // re-selecting the same one (or passing through no target) keeps it, so
    // the stall recovery can finally run out on a target that is dropped and
    // picked again every few seconds.
    QuestTravelDestination const* quest = dynamic_cast<QuestTravelDestination const*>(tDestination1);
    if (turnin_recovery::ShouldResetProgressOnSetTarget(turnInRecovery, quest != nullptr,
            quest ? uint32(quest->GetEntry()) : 0, quest ? quest->GetQuestId() : 0))
        turnInRecovery.ResetProgress();

    SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
}

void TravelTarget::TraceQuestCommit(TravelDestination const* destination, char const* event, char const* reason)
{
    if (!sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions || !bot)
        return;

    QuestTravelDestination const* quest = dynamic_cast<QuestTravelDestination const*>(destination);
    if (!quest || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return;

    std::ostringstream key;
    key << event << ':' << reason << ':' << quest->GetEntry() << ':' << quest->GetQuestId();

    // Bounded: a bot touches only a few quest targets per minute.
    if (commitTrace.size() > 64 && commitTrace.find(key.str()) == commitTrace.end())
        commitTrace.clear();

    uint32 const now = WorldTimer::getMSTime();
    CommitTrace& trace = commitTrace[key.str()];
    if (trace.nextAt && now < trace.nextAt)
    {
        ++trace.repeats;
        return;
    }

    uint32 const repeats = trace.repeats;
    trace.repeats = 0;
    trace.nextAt = now + MINUTE * IN_MILLISECONDS;

    bool const relation = dynamic_cast<QuestRelationTravelDestination const*>(destination) != nullptr;
    float const distance = (destination == tDestination && wPosition) ? wPosition->distance(bot) : -1.0f;
    sLog.outBasic("[QuestCommit] bot=%u level=%u quest=%u target_entry=%d kind=%s event=%s reason=%s distance=%.0f repeats_since_last=%u",
        bot->GetGUIDLow(), bot->GetLevel(), quest->GetQuestId(), quest->GetEntry(), relation ? "giver_or_taker" : "objective",
        event, reason, distance, repeats);
}

bool TravelTarget::CopyTarget(TravelTarget* const target) {
    // Every applied choice (stock or BotBrain intent) passes here.
    // twow-repo#485: so the taker of a parked turn-in is refused here - a BotBrain
    // turn_in_quest intent, or a list fetched before the park. A group copy follows a
    // member's target and is taken up.
    QuestRelationTravelDestination const* taker = dynamic_cast<QuestRelationTravelDestination const*>(target->tDestination);
    if (taker && taker->GetRelation() && !target->IsGroupCopy() && IsTurnInParked(taker->GetQuestId()))
    {
        TraceQuestCommit(taker, "drop", "parked_intent");
        SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        return false;
    }

    // twow-repo#485: move retries belong to one destination. Carried over, they sent a new
    // turn-in into cooldown after one failed move and counted it as move_failed. Only with
    // turn-in parking on; 0 keeps the old counter.
    if (sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures && target->tDestination != tDestination)
        SetRetry(true, 0);

    QuestTravelDestination const* oldQuest = dynamic_cast<QuestTravelDestination const*>(tDestination);
    QuestTravelDestination const* newQuest = dynamic_cast<QuestTravelDestination const*>(target->tDestination);
    if (newQuest)
        TraceQuestCommit(newQuest, "commit", oldQuest == newQuest ? "same_target" : (oldQuest ? "switched_quest_target" : "new_quest_target"));
    else if (oldQuest)
        TraceQuestCommit(oldQuest, "drop", "replaced_by_non_quest_target");

    SetTarget(target->tDestination, target->wPosition);
    // twow-repo#601 S2 / S4 R2: the choice's window end (0 for every target but a GrindMode 1 gap grind trip); setNewTarget
    // sets READY next, which applies it.
    gapWindowEnd = target->gapWindowEnd;
    // twow-repo#601 S5a (BandLadder): the band step's trip end (0 for every other choice; a group copy builds a new target).
    bandTripEnd = target->bandTripEnd;
    // F4: a backoff walk stays a walk (0 for every other choice).
    bandWalkAt = target->bandWalkAt;
    bandWalkFromZone = target->bandWalkFromZone;
    groupMember = target->groupMember;
    forced = target->forced;
    relevance = target->relevance;
    extendRetryCount = target->extendRetryCount;
    return true;
}

uint32 TravelTarget::GetMaxTravelTime() const
{
    // twow-repo#295: a budget taken while mounted (up to 2.8x the run speed with riding
    // stages) ran out once the bot dismounted for a fight on the way.
    float const speed = riding_stages::TravelBudgetRunSpeed(sWorld.getConfig(CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED),
        bot->GetSpeed(MOVE_RUN), baseMoveSpeed[MOVE_RUN]);
    return (1000.0 * Distance(bot)) / speed;
}

void TravelTarget::SetStatus(TravelStatus status) {
    // twow-repo#594 / P3-b: a direct-target trip also ends where its target goes into cooldown, expires or is cleared on it
    // (destination inactive, work time out, a gathering purpose ended, a reset).
    if (ai::direct_targets::AnyBuilt())
        ai::direct_targets::OnTargetStatus(ai, tDestination, status);

    m_status = status;
    startTime = WorldTimer::getMSTime();

    switch (m_status) {
    case TravelStatus::TRAVEL_STATUS_NONE:
    case TravelStatus::TRAVEL_STATUS_PREPARE:
    case TravelStatus::TRAVEL_STATUS_EXPIRED:
        statusTime = 1;
        break;
    case TravelStatus::TRAVEL_STATUS_READY:
        statusTime = HOUR *  1000;
        break;
    case TravelStatus::TRAVEL_STATUS_TRAVEL:
        // Cross-map/transport turn-ins, and (#329) roster quest objectives,
        // are governed by observed progress, never by a generic
        // distance-derived overall wall clock that every fight on the way
        // used up.
        statusTime = IsProgressAwareQuestTravel() ? 0 : GetMaxTravelTime() * 2 + sPlayerbotAIConfig.maxWaitForMove;
        break;
    case TravelStatus::TRAVEL_STATUS_WORK:
        statusTime = tDestination->GetExpireDelay();
        break;
    case TravelStatus::TRAVEL_STATUS_COOLDOWN:
        statusTime = tDestination->GetCooldownDelay();
    default: break;
    }

    // twow-repo#601 S2 / S4 R2 (GrindMode 1, OB-10 rev. 2): a gap grind trip ends with its quest route backoff - READY,
    // TRAVEL and WORK get the time left until the window end (at most RouteBackoffMaxSeconds + 30 s), so the request gate
    // opens and the next quest search runs. gapWindowEnd is 0 for every other target: nothing changes there.
    if (gapWindowEnd && (m_status == TravelStatus::TRAVEL_STATUS_READY || m_status == TravelStatus::TRAVEL_STATUS_TRAVEL ||
            m_status == TravelStatus::TRAVEL_STATUS_WORK))
        statusTime = ai::quest_grind_mode::GapStatusMs(statusTime, gapWindowEnd, uint32(time(nullptr)),
            ai::quest_search::RouteBackoffMaxSeconds);
    // twow-repo#601 S2 / S4 R2 (review C-F1 / B-F1): a COOLDOWN of a gap grind trip (move retries, a group member's check) never
    // outlives the window either - the shorter of the cooldown and the time left. Counted as no expiry, no failure here.
    else if (gapWindowEnd && m_status == TravelStatus::TRAVEL_STATUS_COOLDOWN)
        statusTime = ai::quest_grind_mode::GapCooldownMs(statusTime, gapWindowEnd, uint32(time(nullptr)),
            ai::quest_search::RouteBackoffMaxSeconds);

    // twow-repo#601 S5a (BandLadder): a band trip lasts exactly until its fixed end - READY / TRAVEL / WORK get the time left,
    // a COOLDOWN at most that, 1 ms once ended (capped: a clock step back fails closed). bandTripEnd is 0 for every other target.
    if (bandTripEnd)
        statusTime = ai::quest_band::TripStatusMs(uint8(m_status), statusTime, bandTripEnd, uint32(time(nullptr)));
}

bool TravelTarget::IsProgressAwareTurnIn() const
{
    QuestRelationTravelDestination const* destination = dynamic_cast<QuestRelationTravelDestination const*>(tDestination);
    return destination && destination->GetRelation() &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        bot->GetQuestStatus(destination->GetQuestId()) == QUEST_STATUS_COMPLETE;
}

bool TravelTarget::IsProgressAwareQuestTravel() const
{
    if (IsProgressAwareTurnIn())
        return true;

    // #329: live, 1,214 quest targets in two hours were dropped by the
    // generic travel budget (2 x straight-line travel time), which every
    // fight on the way consumed; the bot then chose again and again.
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination);
    return objective && sPlayerbotAIConfig.questFirstProgressionProgressAwareObjectives &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) &&
        bot->GetQuestStatus(objective->GetQuestId()) == QUEST_STATUS_INCOMPLETE;
}

bool TravelTarget::IsTurnInRouteSuppressed(TravelDestination const* destination, WorldPosition const* position) const
{
    // Turn-ins (#117) and, since #329, stalled objective routes.
    QuestTravelDestination const* questDestination = dynamic_cast<QuestTravelDestination const*>(destination);
    QuestRelationTravelDestination const* relation = dynamic_cast<QuestRelationTravelDestination const*>(destination);
    bool const eligible = questDestination && (!relation || relation->GetRelation());
    return eligible && position &&
        turnin_recovery::IsSuppressed(turnInRecovery, WorldTimer::getMSTime(),
            uint32(questDestination->GetEntry()), position->getMapId());
}

turnin_recovery::RecoveryAction TravelTarget::ObserveTurnInProgress()
{
    // Turn-ins and (#329) objectives share the same progress observation.
    QuestTravelDestination const* destination = dynamic_cast<QuestTravelDestination const*>(tDestination);
    if (!destination || !wPosition)
        return turnin_recovery::RecoveryAction::None;

    // twow-repo#627 S7 hook (CLI-Deep): a help-group member is excluded, not paused - the group ends a stalled goal itself
    // (no_progress, turnin_wait, leader_stuck), so no stage, suppression or park here, and no stale window after the group.
    if (quest_groups::IsMember(ai))
    {
        turnInRecovery.ResetProgress();
        return turnin_recovery::RecoveryAction::None;
    }

    // These states are legitimate travel interruptions. They consume neither
    // a recovery stage nor a fixed overall duration.
    // twow-repo#602 X1 (AiPlayerbot.Rotation.RestGate): a resting bot stands still on purpose. Nothing observes while it rests (the move
    // action waits on "can move around"), so the first observation after an applied rest (OnRestEnded) is the pause.
    bool const restPause = restPausePending;
    restPausePending = false;
    bool const paused = bot->IsInCombat() || !bot->IsAlive() || bot->IsTaxiFlying() ||
        bot->GetTransport() || ai->HasRealPlayerMaster() ||
        restPause || RestGateValue::Resting(ai);
    turnin_recovery::Observation observation;
    observation.now = WorldTimer::getMSTime();
    observation.targetEntry = uint32(destination->GetEntry());
    observation.questId = destination->GetQuestId();
    observation.mapId = bot->GetMapId();
    observation.zoneId = bot->GetZoneId();
    observation.x = bot->GetPositionX();
    observation.y = bot->GetPositionY();
    observation.distance = Distance(bot);
    observation.paused = paused;
    // twow-repo#485: the suppression key is the target's map, the one that
    // IsTurnInRouteSuppressed is asked with; mapId above stays the bot's map.
    observation.targetMapId = wPosition->getMapId();
    // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall): near a finished quest's taker only getting closer is
    // progress (turnin_recovery::Observe). Objectives and the switch off: no near zone, as before.
    if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow()) && IsProgressAwareTurnIn())
        observation.nearRadius = sPlayerbotAIConfig.zoneExitTurnInNearRadius;
    // twow-repo#601 3a (AiPlayerbot.ZoneExit.TurnInTravelStallCountsCombat, with TurnInTravelStall): a fight is the only
    // pause on the way to a finished quest's taker - it no longer restarts the stall window, so a bot that keeps
    // fighting near the route and never gets closer still reaches the recovery. Death, flights, transports, a real
    // master and rest keep restarting it. Objectives and the switch off: as before.
    if (paused && sPlayerbotAIConfig.zoneExitTurnInTravelStallCountsCombat &&
        sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow()) && IsProgressAwareTurnIn() &&
        bot->IsInCombat() && bot->IsAlive() && !bot->IsTaxiFlying() && !bot->GetTransport() && !ai->HasRealPlayerMaster() &&
        !restPause && !RestGateValue::Resting(ai))
        observation.pauseKeepsWindow = true;

    // Hotfix 8.13 (twow-repo#497): for a target on another map only a shorter distance counts.
    uint32 const targetMapId = observation.targetMapId;
    transport_stall::Step const transportStep = transportStall.Observe(observation.targetEntry, observation.questId,
        targetMapId != observation.mapId, paused, observation.distance, observation.now);
    if (transportStep != transport_stall::Step::None)
    {
        sLog.outBasic("[Travel] transport_wait state=%s bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f target_map=%u target_entry=%u quest=%u distance=%.0f minutes=%u",
            transportStep == transport_stall::Step::Abandon ? "abandon" : "wait", bot->GetGUIDLow(), bot->GetLevel(),
            observation.mapId, observation.zoneId, observation.x, observation.y, targetMapId, observation.targetEntry,
            observation.questId, observation.distance, transportStall.minutes);
        if (transportStep == transport_stall::Step::Abandon)
        {
            if (sPlayerbotAIConfig.pathTransportEdgeTrace)   // twow-repo#342 (S1): fail reason=stall_abandon for a running episode
                MovementAction::TraceTransportEdgeStallAbandon(ai);
            turnInRecovery.suppressedEntry = observation.targetEntry;
            turnInRecovery.suppressedMapId = targetMapId;
            turnInRecovery.suppressUntil = observation.now + transport_stall::CooldownMs;
            turnInRecovery.ResetProgress();
            return turnin_recovery::RecoveryAction::SuppressTransportAndCooldown;
        }
    }

    return turnin_recovery::Observe(turnInRecovery, observation,
        sPlayerbotAIConfig.questFirstProgressionTurnInStallSeconds * IN_MILLISECONDS,
        sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds * IN_MILLISECONDS);
}

bool TravelTarget::IsActiveForDeathAttribution() const
{
    return m_status == TravelStatus::TRAVEL_STATUS_TRAVEL || m_status == TravelStatus::TRAVEL_STATUS_WORK;
}

void TravelTarget::OnDeathOnTurnInRoute()
{
    // #307: a dropped or abandoned target (bot now grinding elsewhere) is not
    // blamed for later deaths.
    if (!IsActiveForDeathAttribution())
        return;
    if (!IsProgressAwareTurnIn())
        return;

    QuestRelationTravelDestination const* destination = static_cast<QuestRelationTravelDestination const*>(tDestination);
    uint32 const entry = uint32(destination->GetEntry());
    uint32 const questId = destination->GetQuestId();
    uint32 const mapId = wPosition ? wPosition->getMapId() : bot->GetMapId();

    if (!turnin_recovery::RecordDeathOnRoute(turnInRecovery, entry, questId, mapId, WorldTimer::getMSTime(),
            sPlayerbotAIConfig.questFirstProgressionTurnInMaxDeathsOnRoute,
            sPlayerbotAIConfig.questFirstProgressionTurnInDeathRouteCooldownSeconds * IN_MILLISECONDS))
        return;

    // Always visible (BASIC): this is the acceptance signal for #307.
    sLog.outBasic("[QuestFirstRoute] state=death_suppressed bot=%u level=%u quest=%u target_entry=%u map=%u deaths=%u action=suppress_route_cooldown cooldown_seconds=%u",
        bot->GetGUIDLow(), bot->GetLevel(), questId, entry, mapId,
        sPlayerbotAIConfig.questFirstProgressionTurnInMaxDeathsOnRoute,
        sPlayerbotAIConfig.questFirstProgressionTurnInDeathRouteCooldownSeconds);

    // twow-repo#485: and a failed turn-in.
    NoteTurnInFailure(questId, "death");

    // Drop the route now; target selection skips it until the cooldown ends,
    // so the revived bot picks level-appropriate work instead.
    TraceQuestCommit(tDestination, "abandon", "death_suppressed");
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
}

void TravelTarget::OnDeathAtDestination()
{
    if (!IsActiveForDeathAttribution())
        return;

    // Completed turn-ins keep their own route rule above; everything else
    // (fishing, gathering, grind, quest objectives and givers) is counted here.
    if (!tDestination || dynamic_cast<NullTravelDestination const*>(tDestination) || IsProgressAwareTurnIn())
        return;

    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return;

    // Bounded: a bot dies at only a handful of distinct destinations per hour.
    if (destinationDeaths.size() > 64 && destinationDeaths.find(tDestination) == destinationDeaths.end())
        destinationDeaths.clear();

    if (!destination_death::RecordDeath(destinationDeaths[tDestination], WorldTimer::getMSTime(),
            sPlayerbotAIConfig.destinationDeathsMax, sPlayerbotAIConfig.destinationDeathsCooldownSeconds * IN_MILLISECONDS))
        return;

    std::string kind = "unknown";
    if (auto const it = TravelDestinationPurposeName.find(tDestination->GetPurpose()); it != TravelDestinationPurposeName.end())
        kind = it->second;

    // Always visible (BASIC): acceptance signal for the generalised #307 rule.
    sLog.outBasic("[DestinationDeath] state=death_suppressed bot=%u level=%u kind=%s title=\"%s\" entry=%d map=%u deaths=%u cooldown_seconds=%u",
        bot->GetGUIDLow(), bot->GetLevel(), kind.c_str(), tDestination->GetTitle().c_str(), tDestination->GetEntry(),
        wPosition ? wPosition->getMapId() : bot->GetMapId(), sPlayerbotAIConfig.destinationDeathsMax,
        sPlayerbotAIConfig.destinationDeathsCooldownSeconds);

    TraceQuestCommit(tDestination, "abandon", "death_suppressed");
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
}

void TravelTarget::OnWorkTimeout()
{
    // twow-repo#485: a turn-in whose work phase ran out is a failed turn-in (v24: quest 310
    // "Bitter Rivals", whose taker only a script spawns - 510 work timeouts, 0 turn-ins).
    if (IsProgressAwareTurnIn())
    {
        // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall, OB-10 rev. 2 Q-B4): the second work timeout of the
        // same turn-in parks it at once (soak: bot 216 at the Azora taker, no line of sight through the door). Not
        // abandonable: a finished quest is never dropped for it (the series of abandonable parks ends here, S1).
        // twow-repo#627 S7 hook: not for a help-group member (the group's turnin_wait ends a stalled turn-in phase).
        if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow()) && !quest_groups::IsMember(ai))
        {
            uint32 const questId = static_cast<QuestTravelDestination*>(tDestination)->GetQuestId();
            if (turnin_recovery::RecordWorkTimeout(workTimeoutTurnIns, questId))
            {
                bool abandonDue = false;
                ParkTurnInNow(questId, "structural_work_timeout", false, abandonDue);
            }
        }
        NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), "work_timeout");
        return;
    }

    // twow-repo#601 (QuestLoop.InactiveAfterProgress): an objective work phase that ran out after the bot got credit on the
    // objective is no failure either - neither QL-1 nor #405 counts it; the count is taken again. Off or another cohort: as before.
    if (dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination) &&
        ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveAfterProgress, bot->GetGUIDLow()) &&
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())
    {
        int32 const countNow = ObjectiveCountOf(bot, tDestination);
        bool const progressed = quest_loop::InactiveAfterProgress(objectiveCountAtStart, countNow);
        if (progressed)
            sLog.outBasic("[QuestLoop] state=work_after_progress bot=%u level=%u quest=%u target_entry=%d count_from=%d count_to=%d",
                bot->GetGUIDLow(), bot->GetLevel(), static_cast<QuestObjectiveTravelDestination const*>(tDestination)->GetQuestId(),
                tDestination->GetEntry(), objectiveCountAtStart, countNow);
        objectiveCountAtStart = countNow;
        if (progressed)
            return;
    }

    // twow-repo#601 F5b (AiPlayerbot.QuestFirstProgression.ContestedObjectiveWait): a contested objective is held back briefly and counts
    // neither for QL-1 nor for #405 below. Switch off: as before.
    if (sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWait && OnContestedObjectiveTimeout())
        return;

    // twow-repo#581 (QL-1): a work timeout of an objective also counts against its quest.
    if (dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))
        NoteQuestObjectiveFailure();

    // #405 (live 2026-09-27): 93 % of the quest-objective work phases at world
    // objects (lock 43 chests: Cactus Apple, Tirisfal Pumpkin, Water Pitcher...)
    // ran out without a loot, and the bot picked the same objective again: Juzika
    // (L9) kept going back to a level-1 quest 1100 yards away. After
    // QuestWorkTimeouts.Max such timeouts the objective is skipped by this bot for
    // QuestWorkTimeouts.CooldownSeconds, like a death-suppressed destination.
    if (!sPlayerbotAIConfig.questWorkTimeoutsMax || !dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))
        return;

    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return;

    // Bounded like the destination death map.
    if (workTimeouts.size() > 64 && workTimeouts.find(tDestination) == workTimeouts.end())
        workTimeouts.clear();

    uint32 const cooldownMs = sPlayerbotAIConfig.questWorkTimeoutsCooldownSeconds * IN_MILLISECONDS;
    if (!destination_death::RecordDeath(workTimeouts[tDestination], WorldTimer::getMSTime(),
            sPlayerbotAIConfig.questWorkTimeoutsMax, cooldownMs))
        return;

    TraceQuestCommit(tDestination, "abandon", "work_suppressed");
    SuppressCurrentDestination(cooldownMs);
    TagCurrentSuppression(destination_death::SourceWork);   // twow-repo#601 F5 (diagnostic)
}

void TravelTarget::SuppressCurrentDestination(uint32 durationMs)
{
    if (!tDestination || dynamic_cast<NullTravelDestination const*>(tDestination) || !durationMs)
        return;

    // Same bounded per-bot map as the destination death rule (#138).
    if (destinationDeaths.size() > 64 && destinationDeaths.find(tDestination) == destinationDeaths.end())
        destinationDeaths.clear();

    destination_death::Record& record = destinationDeaths[tDestination];
    record.deaths = 0;
    record.suppressUntil = WorldTimer::getMSTime() + durationMs;
}

void TravelTarget::TagCurrentSuppression(uint8 source)
{
    auto const it = destinationDeaths.find(tDestination);
    if (it != destinationDeaths.end())
        it->second.source = source;
}

uint8 TravelTarget::DestinationSuppressSource(TravelDestination const* destination) const
{
    auto const it = destinationDeaths.find(destination);
    return it != destinationDeaths.end() ? it->second.source : uint8(destination_death::SourceDeath);
}

bool TravelTarget::OnContestedObjectiveTimeout()
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination);
    if (!objective || !bot || ai->HasRealPlayerMaster() || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return false;

    contested_objective::Facts facts;
    facts.on = ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWait, bot->GetGUIDLow());
    facts.objective = true;
    facts.goObjective = objective->GetEntry() < 0;
    facts.killToo = sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWaitKill;
    Group* const group = bot->GetGroup();
    facts.inBotOnlyGroup = group && ai::adhoc_group::AllRosterBots(group);
    facts.minBots = sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWaitMinBots;
    if (!facts.on || (!facts.goObjective && (!facts.killToo || facts.inBotOnlyGroup)))
        return false;   // the count below cannot change the answer

    // Other roster bots around the objective (the bot stands at it): bounded by the sight list, positions only, no other bot's state.
    for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, "nearest friendly players"))
    {
        Player* const other = sObjectMgr.GetPlayer(guid);
        if (!other || other == bot || other->GetMapId() != bot->GetMapId() || !sRandomPlayerbotMgr.IsPersistentRosterMember(other->GetGUIDLow()))
            continue;
        if (bot->GetDistance2d(other) <= contested_objective::Radius)
            ++facts.otherBotsNear;
    }
    if (!contested_objective::Contested(facts))
        return false;

    uint32 const holdSeconds = sPlayerbotAIConfig.questFirstProgressionContestedObjectiveWaitSeconds;
    SuppressCurrentDestination(holdSeconds * IN_MILLISECONDS);
    TagCurrentSuppression(destination_death::SourceContested);
    sLog.outBasic("[ContestedObjective] bot=%u level=%u quest=%u target_entry=%d kind=%s bots_near=%u min_bots=%u hold_seconds=%u",
        bot->GetGUIDLow(), bot->GetLevel(), objective->GetQuestId(), objective->GetEntry(), facts.goObjective ? "go" : "kill",
        facts.otherBotsNear, facts.minBots, holdSeconds);
    TraceQuestCommit(tDestination, "abandon", "contested_wait");
    return true;
}

void TravelTarget::NoteQuestObjectiveFailure()
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination);
    if (!sPlayerbotAIConfig.questLoopMaxFailures || !objective || !bot || !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))
        return;

    uint32 const questId = objective->GetQuestId();
    // Bounded like the destination maps: a bot fails at only a few quests per window.
    if (questLoops.size() > 32 && questLoops.find(questId) == questLoops.end())
        questLoops.clear();

    uint32 const cooldownMs = sPlayerbotAIConfig.questLoopCooldownSeconds * IN_MILLISECONDS;
    if (!quest_loop::RecordFailure(questLoops[questId], WorldTimer::getMSTime(), sPlayerbotAIConfig.questLoopMaxFailures,
            sPlayerbotAIConfig.questLoopWindowSeconds * IN_MILLISECONDS, cooldownMs))
        return;

    sLog.outBasic("[QuestLoop] state=suppressed bot=%u level=%u quest=%u target_entry=%d failures=%u cooldown_seconds=%u",
        bot->GetGUIDLow(), bot->GetLevel(), questId, objective->GetEntry(), sPlayerbotAIConfig.questLoopMaxFailures,
        sPlayerbotAIConfig.questLoopCooldownSeconds);
    TraceQuestCommit(tDestination, "abandon", "quest_loop_suppressed");
}

bool TravelTarget::IsQuestLoopSuppressed(TravelDestination const* destination) const
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(destination);
    if (!objective || questLoops.empty())
        return false;
    auto const it = questLoops.find(objective->GetQuestId());
    return it != questLoops.end() && quest_loop::IsSuppressed(it->second, WorldTimer::getMSTime());
}

// twow-repo#601 (QuestLoop.EmptyCampHold): the respawn of the spawn point a travel target points at - creature spawntimesecsmin,
// game object spawntimesecsmin when positive (negative = spawned by a script only); 0 when the point is no spawn.
static uint32 SpawnRespawnSeconds(WorldPosition const* point)
{
    GuidPosition const* spawn = dynamic_cast<GuidPosition const*>(point);
    if (!spawn)
        return 0;
    if (CreatureData const* data = spawn->GetCreatureData())
        return data->spawntimesecsmin;
    if (GameObjectData const* data = spawn->GetGameObjectData())
        return data->spawntimesecsmin > 0 ? uint32(data->spawntimesecsmin) : 0;
    return 0;
}

bool TravelTarget::IsInactiveReselectMarked(TravelDestination const* destination) const
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(destination);
    return objective && quest_loop::EmptyCampLeftMs(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime()) > 0;
}

// twow-repo#601 (QuestLoop.InactiveReselect): the search right after an emptied camp (ChooseTravelTargetAction took the pending flag
// before it set the status to NONE) found no quest target - back to the camp in COOLDOWN for the time left on its mark, as without
// the switch. The destination is still the camp (no target was set); false when nothing is to restore.
bool TravelTarget::FallBackToInactiveCooldown()
{
    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination);
    if (!objective || !bot)
        return false;
    uint32 leftMs = quest_loop::EmptyCampLeftMs(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime());
    // twow-repo#601 (QuestLoop.EmptyCampHold): a held camp keeps the search away up to its respawn, but the COOLDOWN back at it
    // stays the old one (the cooldown just set), so the hold never makes the bot stand longer.
    if (leftMs && ai::cohort::Applies(sPlayerbotAIConfig.questLoopEmptyCampHold, bot->GetGUIDLow()))
        leftMs = quest_loop::FallbackCooldownMs(leftMs,
            quest_loop::InactiveCooldownMs(tDestination->GetCooldownDelay(), sPlayerbotAIConfig.questLoopInactiveCooldownSeconds));
    if (!leftMs)
        return false;
    SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
    SetExpireIn(leftMs);
    sLog.outBasic("[QuestLoop] state=inactive_reselect_fallback bot=%u level=%u quest=%u target_entry=%d cooldown_ms=%u",
        bot->GetGUIDLow(), bot->GetLevel(), objective->GetQuestId(), objective->GetEntry(), leftMs);
    return true;
}

bool TravelTarget::IsDestinationDeathSuppressed(TravelDestination const* destination) const
{
    auto const it = destinationDeaths.find(destination);
    return it != destinationDeaths.end() && destination_death::IsSuppressed(it->second, WorldTimer::getMSTime());
}

bool TravelTarget::NoteTurnInFailure(uint32 questId, char const* reason)
{
    uint32 const maxFailures = sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures;
    if (!maxFailures || !questId || !bot)
        return false;

    // Roster bots on their own only; a real master decides for his bot.
    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster())
        return false;

    // Only a finished quest not handed in yet: a handed-in quest stays QUEST_STATUS_COMPLETE
    // with m_rewarded set (Player::RewardQuest).
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId))
        return false;

    if (!turnin_park::RecordFailure(turnInParks, questId, WorldTimer::getMSTime(), maxFailures,
            sPlayerbotAIConfig.questFirstProgressionTurnInParkWindowSeconds * IN_MILLISECONDS,
            sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds * IN_MILLISECONDS))
        return false;

    // Always visible (BASIC): the acceptance signal for #485. One line per park, so at most
    // turnin_park::BookSize lines per bot and park duration.
    sLog.outBasic("[QuestFirstRoute] state=parked bot=%u level=%u quest=%u reason=%s park_seconds=%u parked=%u",
        bot->GetGUIDLow(), bot->GetLevel(), questId, reason, sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds,
        ParkedTurnInCount());
    return true;
}

bool TravelTarget::IsTurnInParked(uint32 questId) const
{
    // Off, or a real master leads the bot: no park applies. Nor once the quest is no longer
    // finished and open in the bot's log (dropped, or handed in: it then stays
    // QUEST_STATUS_COMPLETE with m_rewarded set, Player::RewardQuest).
    if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures || ai->HasRealPlayerMaster())
        return false;

    return turnin_park::IsParked(turnInParks, questId, WorldTimer::getMSTime()) &&
        bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(questId);
}

uint32 TravelTarget::ParkedTurnInCount() const
{
    // The parks that apply (IsTurnInParked), not every entry of the book.
    uint32 count = 0;
    for (turnin_park::Entry const& entry : turnInParks.entries)
        if (entry.questId && IsTurnInParked(entry.questId))
            ++count;
    return count;
}

bool TravelTarget::IsTurnInOpen(uint32 questId) const
{
    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
    if (!quest)
        return false;

    // Player::CanRewardQuest(.., false) still sends SMSG_QUESTGIVER_QUEST_INVALID when the bot
    // lacks the money a quest asks for: the same answer here, without the packet.
    int32 const money = quest->GetRewOrReqMoney();
    if (money < 0 && bot->GetMoney() < uint32(-money))
        return false;

    return bot->CanRewardQuest(quest, false) && !IsTurnInParked(questId);
}

bool TravelTarget::ParkTurnInNow(uint32 questId, char const* reason, bool abandonable, bool& abandonDue)
{
    abandonDue = false;
    // Same gates as NoteTurnInFailure: parking on, roster bots on their own, a finished quest not handed in yet.
    if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures || !questId || !bot)
        return false;

    if (!sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster())
        return false;

    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId))
        return false;

    turnin_park::ParkResult const result = turnin_park::ParkNow(turnInParks, questId, WorldTimer::getMSTime(),
        sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds * IN_MILLISECONDS, abandonable);
    if (!result.parked)
        return false;

    abandonDue = result.abandonDue;
    // The #485 park line with the structural reason (structural_cross_map, structural_zone_level, structural_route,
    // structural_no_destination). One line per park, as there.
    sLog.outBasic("[QuestFirstRoute] state=parked bot=%u level=%u quest=%u reason=%s park_seconds=%u parked=%u",
        bot->GetGUIDLow(), bot->GetLevel(), questId, reason, sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds,
        ParkedTurnInCount());
    return true;
}

void TravelTarget::OnMoveRetryCooldown()
{
    // twow-repo#485: this cooldown was silent (v24: 3,110 of 3,932 cooldown expiries had
    // no logged start).
    TraceQuestCommit(tDestination, "blocked", "move_retry_cooldown");

    // twow-repo#594 / P3-b: a direct-target destination the bot could not move to (failure memory, trip end).
    if (ai::direct_targets::AnyBuilt())
        ai::direct_targets::OnTravelFailure(ai, tDestination, "move_retry");

    if (!IsProgressAwareTurnIn())
        return;

    // A turn-in the bot cannot move to is a failed turn-in - unless the bot is held up for
    // a good reason (the pauses of ObserveTurnInProgress).
    if (bot->IsInCombat() || !bot->IsAlive() || bot->IsTaxiFlying() || bot->GetTransport() || ai->HasRealPlayerMaster())
        return;
    // twow-repo#602 X1 (OB-10 review 18.10.2026): a resting bot (AiPlayerbot.Rotation.RestGate) is held up for a good reason too.
    if (RestGateValue::Resting(ai))
        return;

    NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), "move_failed");
}

void TravelTarget::OnRestEnded(uint32 durMs)
{
    // Called only for an applied episode with the switch on (RestGateValue::Finish), so mode 0 never gets here.
    restPausePending = true;
    // The rest held the bot still: a WORK phase (60 s at a quest objective) or a timed TRAVEL budget would otherwise count it as lost
    // work - quest loop / work-timeout suppression, a turn-in park, a direct-target failure. Progress-aware TRAVEL has no clock.
    // Not for an S2 gap grind trip (gapWindowEnd): its clock is the quest-route backoff window (OB-10 review 18.10.2026).
    if (statusTime != 0 && !IsForced() && !gapWindowEnd &&
        (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK || GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL))
        SetExpireIn(uint32(std::max<int32>(GetTimeLeft(), 0)) + durMs);
}

void TravelTarget::DecRetry(bool isMove)
{
    // twow-repo#485: with no move retry left, a successful move took one off the refresh
    // retries instead (extendRetryCount), so RefreshTravelTargetAction never reached its
    // limit on a target the bot kept walking to. Corrected with turn-in parking on; 0 keeps
    // the old form for every bot.
    if (!sPlayerbotAIConfig.questFirstProgressionTurnInParkFailures)
    {
        if (isMove && moveRetryCount > 0) moveRetryCount--; else if (extendRetryCount > 0) extendRetryCount--;
        return;
    }

    if (isMove)
    {
        if (moveRetryCount)
            --moveRetryCount;
    }
    else if (extendRetryCount)
        --extendRetryCount;
}

bool TravelTarget::IsDestinationActive()
{
    Player* player = bot;
    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!ai->IsSafe(member)) //unknown
            return true;
        else
            player = member;
    }

    if (!GetBotAI(player)) //No ai so clear target.
        return false;

    // twow-repo#601 S2 R3 (review C-F2): the R3 window only for a gap grind trip (gapWindowEnd, GrindMode 1 only), never for a
    // quest trip to grind mobs.
    return tDestination->IsActive(player, PlayerTravelInfo(player, gapWindowEnd != 0));
}

bool TravelTarget::IsConditionsActive(bool clear)
{
    Player* player = bot;
    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!ai->IsSafe(member)) //unknown
            return true;
        else
            player = member;
    }

    if (!player || !GetBotAI(player)) //No ai so clear target.
        return false;
        
    AiObjectContext* playerContext = GetBotAI(player)->GetAiObjectContext();

    if (!playerContext)
        return false;

    if (clear)
        for (auto& condition : travelConditions)
            playerContext->ClearValues(condition);

    for (auto& condition : travelConditions)
    {
        auto* value = playerContext->GetValue<bool>(condition);
        if (!value || !value->Get())
            return false;
    }

    return true;
}

void TravelTarget::OnNearTurnInStall(uint32 questId)
{
    // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall, OB-10 rev. 2 Q-B3): the bot stalled inside the near
    // zone of the taker (Azora: every path ends at the tower wall, MoveTo stays untouched - PATH-02). The turn-in is
    // parked at once like an S1 structural class, so the choice skips the taker for TurnInParkSeconds and the rescue
    // reset does not send the bot back (the park book is not reset there). Not abandonable: a finished quest whose
    // taker is out of reach keeps its reward, it is only parked again (OB-10, design S3c 2).
    bool abandonDue = false;
    bool const parked = ParkTurnInNow(questId, "structural_unreachable_near", false, abandonDue);
    // The Z4 line in its format, so CLI-PATH gets the near takers in [UnreachableTop] as with stage1_recompute.
    float const dx = wPosition ? wPosition->getX() : 0.0f, dy = wPosition ? wPosition->getY() : 0.0f, dz = wPosition ? wPosition->getZ() : 0.0f;
    uint32 const dmap = wPosition ? wPosition->getMapId() : bot->GetMapId();
    // switch= stays the Z4 key (TurnInStallCounts) as on every [UnreachableTarget] line; travel_stall= is this S3c switch
    // (review HR-2: parked=1 with switch=0 is a near_stall park, not a Z4 one).
    sLog.outBasic("[UnreachableTarget] bot=%u level=%u quest=%u entry=%d kind=turn_in map=%u zone=%u dest_map=%u dest_x=%.1f dest_y=%.1f dest_z=%.1f reason=near_stall parked=%u switch=%u travel_stall=%u",
        bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), bot->GetMapId(), bot->GetZoneId(), dmap, dx, dy, dz,
        parked ? 1u : 0u, sPlayerbotAIConfig.zoneExitTurnInStallCounts ? 1u : 0u, sPlayerbotAIConfig.zoneExitTurnInTravelStall ? 1u : 0u);
    unreachable_target::Key const key = unreachable_target::KeyOf(tDestination->GetEntry(), dmap, dx, dy);
    unreachable_target::ShortWindow().Add(key, questId, bot->GetGUIDLow());
    unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());

    // Leave the target, no READY -> TRAVEL on the same taker. Parked: the choice skips it now. Not parked (parking
    // off, or not a roster bot on its own): the stage-2 route suppression and cooldown at once (review F3), so the bot
    // does not come straight back to the same taker.
    TraceQuestCommit(tDestination, "abandon", "near_stall");
    if (parked)
    {
        // A trip after the park starts with a fresh observation (no stage 2 straight away).
        turnInRecovery.ResetProgress();
        SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        return;
    }
    turnInRecovery.suppressedEntry = uint32(tDestination->GetEntry());
    turnInRecovery.suppressedMapId = dmap;
    turnInRecovery.suppressUntil = WorldTimer::getMSTime() + sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds * IN_MILLISECONDS;
    SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
    SetExpireIn(sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds * IN_MILLISECONDS);
}

bool TravelTarget::IsNearTurnInStall()
{
    // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall): switch first, then a finished quest's turn-in with the
    // bot inside TurnInNearRadius of the taker on its map. Asked for a stage-1 and (review F4) a stage-2 stall alike.
    return sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow()) && wPosition && IsProgressAwareTurnIn() &&
        turnin_recovery::InNearZone(Distance(bot), bot->GetMapId() == wPosition->getMapId(), sPlayerbotAIConfig.zoneExitTurnInNearRadius);
}

bool TravelTarget::ObservePosStuck()
{
    // twow-repo#594 (AiPlayerbot.Path.PosStuck, OB-00 / OB-10 23.10.2026): the position guard (PosStuckPolicy.h). The population of
    // QFO / PartialNoop: roster bots on their own, alive, not on a taxi or transport, no battleground or instance, not resting,
    // the target on the bot's map. Outside it the window does not run.
    if (!tDestination || !wPosition || dynamic_cast<NullTravelDestination const*>(tDestination))
        return false;

    QuestTravelDestination const* quest = dynamic_cast<QuestTravelDestination const*>(tDestination);
    uint32 const questId = quest ? quest->GetQuestId() : 0;
    Map const* map = bot->GetMap();
    pos_stuck::Observation observation;
    observation.now = WorldTimer::getMSTime();
    observation.key = pos_stuck::KeyOf(tDestination->GetEntry(), questId);
    observation.mapId = bot->GetMapId();
    observation.x = bot->GetPositionX();
    observation.y = bot->GetPositionY();
    observation.z = bot->GetPositionZ();
    observation.combat = bot->IsInCombat();
    observation.excluded = !sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) || ai->HasRealPlayerMaster() ||
        !bot->IsAlive() || bot->IsTaxiFlying() || bot->GetTransport() || !map || map->Instanceable() ||
        wPosition->getMapId() != bot->GetMapId() || RestGateValue::Resting(ai) ||
        quest_groups::IsFollower(ai);   // twow-repo#627 S7: a follower goes where its leader goes (the leader stays watched)
    observation.distance = observation.excluded ? 0.0f : Distance(bot);

    if (pos_stuck::Observe(posStuck, observation, sPlayerbotAIConfig.pathPosStuckMinutes * 60 * IN_MILLISECONDS,
            sPlayerbotAIConfig.pathPosStuckProgress) != pos_stuck::Verdict::Detect)
        return false;

    // twow-repo#627 S7 hook (CLI-GRP 24.10.): a help-group leader without a forced target (MaxForces reached, a failed force) is
    // the group's problem - the manager dissolves it with leader_stuck on its next pass (<= 60 s), then role 0 and a fresh
    // window. No park, unstuck or home bind here: a park would hit the leader's quest behind the manager's back.
    if (quest_groups::IsMember(ai) && !quest_groups::IsFollower(ai))
    {
        quest_groups::NoteLeaderStuck(ai);
        posStuck.Reset();
        return false;
    }

    // An episode: the series decides park / local unstuck / home bind - a move only if the bot itself stood (OB-10 review: a
    // long detour gets no closer either, it is parked only); a local unstuck needs a usable progress anchor.
    pos_stuck::Step step = pos_stuck::Escalate(posStuckEpisodes, observation.now,
        sPlayerbotAIConfig.pathPosStuckEpisodeMinutes * 60 * IN_MILLISECONDS, sPlayerbotAIConfig.pathPosStuckRateMinutes * 60 * IN_MILLISECONDS,
        pos_stuck::StoodStill(posStuck));
    if (step == pos_stuck::Step::Unstuck && !pos_stuck::AnchorUsable(posStuck, observation.mapId, observation.x, observation.y))
        step = pos_stuck::Step::Teleport;

    pos_stuck::Counters& counters = pos_stuck::SharedCounters();
    uint32 detects = 0, unstucks = 0, teleports = 0, logged = 0;
    if (counters.Roll(uint64(time(nullptr)), detects, unstucks, teleports, logged))
        sLog.outBasic("[PosStuck] summary detect=%u unstuck=%u teleport=%u logged=%u", detects, unstucks, teleports, logged);
    bool const lines = counters.Take(pos_stuck::DefaultLinesPerMinute);
    uint32 const purpose = uint32(tDestination->GetPurpose());
    if (lines)
        sLog.outBasic("[PosStuck] state=detect bot=%u level=%u quest=%u target_entry=%d purpose=%u dist=%.1f minutes=%u map=%u zone=%u x=%.1f y=%.1f z=%.1f displacement=%.1f episode=%u step=%s",
            bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), purpose, observation.distance,
            sPlayerbotAIConfig.pathPosStuckMinutes, observation.mapId, bot->GetZoneId(), observation.x, observation.y, observation.z,
            posStuck.detectDisplacement, uint32(posStuckEpisodes.count), pos_stuck::StepName(step));

    // Park, every episode: a finished quest's turn-in gets the #485 park (S1 structural, not abandonable), an objective one
    // quest loop failure (QL-1); every target is suppressed for this bot for TurnInParkSeconds (an hour when 0).
    bool parked = false;
    if (IsProgressAwareTurnIn())
    {
        bool abandonDue = false;
        parked = ParkTurnInNow(questId, "structural_pos_stuck", false, abandonDue);
    }
    else if (dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))
        NoteQuestObjectiveFailure();
    uint32 const parkSeconds = sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds ? sPlayerbotAIConfig.questFirstProgressionTurnInParkSeconds : 3600;
    SuppressCurrentDestination(parkSeconds * IN_MILLISECONDS);
    TagCurrentSuppression(destination_death::SourcePark);   // twow-repo#601 F5 (diagnostic)
    if (lines)
        sLog.outBasic("[PosStuck] state=park bot=%u level=%u quest=%u target_entry=%d purpose=%u dist=%.1f minutes=%u x=%.1f y=%.1f parked=%u suppress_seconds=%u",
            bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), purpose, observation.distance,
            sPlayerbotAIConfig.pathPosStuckMinutes, observation.x, observation.y, parked ? 1u : 0u, parkSeconds);

    // The move (2nd episode of a series: back to the progress anchor; later: the bot's home bind, only on its own map and zone -
    // never the start-zone rescue of #587). Fired only out of combat (Observe), only for a bot that stood, rate-limited per bot
    // (Escalate); executed by ApplyMove (PosStuckMove.cpp), so this file stays free of teleports.
    pos_stuck::MoveResult const moved = pos_stuck::ApplyMove(bot, step, posStuck.anchorX, posStuck.anchorY, posStuck.anchorZ);
    if (step == pos_stuck::Step::Unstuck)
    {
        counters.unstuck.fetch_add(1, std::memory_order_relaxed);
        if (lines)
            sLog.outBasic("[PosStuck] state=unstuck bot=%u level=%u quest=%u target_entry=%d dist=%.1f minutes=%u x=%.1f y=%.1f to_x=%.1f to_y=%.1f to_z=%.1f",
                bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), observation.distance, sPlayerbotAIConfig.pathPosStuckMinutes,
                observation.x, observation.y, posStuck.anchorX, posStuck.anchorY, posStuck.anchorZ);
    }
    else if (step == pos_stuck::Step::Teleport && moved == pos_stuck::MoveResult::Moved)
    {
        counters.teleport.fetch_add(1, std::memory_order_relaxed);
        if (lines)
            sLog.outBasic("[PosStuck] state=teleport bot=%u level=%u quest=%u target_entry=%d dist=%.1f minutes=%u x=%.1f y=%.1f to=homebind",
                bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), observation.distance, sPlayerbotAIConfig.pathPosStuckMinutes,
                observation.x, observation.y);
    }
    else if (step == pos_stuck::Step::Teleport)
    {
        if (lines)
            sLog.outBasic("[PosStuck] state=teleport_skipped bot=%u level=%u quest=%u target_entry=%d dist=%.1f minutes=%u x=%.1f y=%.1f reason=bind_far",
                bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), observation.distance, sPlayerbotAIConfig.pathPosStuckMinutes,
                observation.x, observation.y);
    }

    TraceQuestCommit(tDestination, "abandon", "pos_stuck");
    posStuck.Reset();
    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
    return true;
}

void TravelTarget::CheckStatus()
{
    if (!IsActive())
        return;

    if (groupMember)
    {
        Player* member = groupMember.GetPlayer();
        if (!member || (ai->IsSafe(member) && member->GetGroup() != bot->GetGroup()))
        {
            ai->TellDebug(ai->GetMaster(), "Travel target expired because it was from a player that is no longer in the group.", "debug travel");
            SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            return;
        }
    }

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT) && !ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
    {
        ai->TellDebug(ai->GetMaster(), "The target is clearing because it was a travel once destination.", "debug travel");
        sTravelMgr.SetNullTravelTarget(this);
        return;
    }

    // twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder): a band trip ends here and only here by its own fixed
    // end, a COOLDOWN (move retries, a group member's check), a lost scope or a hub without an end - before the rest-gate
    // hold, so no SetExpireIn (rest hold, rest give-back, leader wait) can extend it. No failure is counted.
    if (bandTripEnd || (tDestination && tDestination->IsBandHub()))
    {
        uint8 const verdict = CheckBandTrip();
        if (verdict != uint8(ai::quest_band::TripVerdict::Running))
        {
            EndBandTrip(verdict);
            return;
        }
    }

    // twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): the status clock does not run out while the bot rests (the travel action
    // still checks in WORK); OnRestEnded gives the rest time back when the episode ends. Not for an S2 gap grind trip (GrindMode 1,
    // gapWindowEnd): its status time is the quest-route backoff window, which ends while resting too (OB-10 review 18.10.2026).
    if (statusTime != 0 && !IsForced() && !gapWindowEnd && GetTimeLeft() <= 0 && RestGateValue::Resting(ai))
        SetExpireIn(5000);

    if (statusTime != 0 && GetTimeLeft() <= 0 && !IsForced())
    {
        ai->TellDebug(ai->GetMaster(), "Travel target expired because the status time was exceeded.", "debug travel");
        // #329: which budget ran out separates travel churn from a finished
        // work phase (WORK expiring after the objective area is legitimate).
        TraceQuestCommit(tDestination, "drop",
            GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL ? "status_time_exceeded_travel" :
            GetStatus() == TravelStatus::TRAVEL_STATUS_WORK ? "status_time_exceeded_work" :
            GetStatus() == TravelStatus::TRAVEL_STATUS_COOLDOWN ? "status_time_exceeded_cooldown" : "status_time_exceeded");
        // twow-repo#601 S2 / S4 R2 (OB-10 rev. 2 point 3): the end of a gap grind window is no failure - no work timeout,
        // so no turn-in, quest loop or work-timeout count (gapWindowEnd is 0 for every other target).
        if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK && !gapWindowEnd)
            OnWorkTimeout();
        // twow-repo#601 S2 / S4 [GrindWindow] expired= (OB-10 rev. 2): a gap grind trip ended by its window end (READY, TRAVEL
        // or WORK carry the window time; gapWindowEnd is set only for the GrindMode 1 population). A relaxed count, no failure.
        if (gapWindowEnd && (GetStatus() == TravelStatus::TRAVEL_STATUS_READY || GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL ||
                GetStatus() == TravelStatus::TRAVEL_STATUS_WORK))
            PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));
        // twow-repo#594 / P3-b: a direct-target trip out of travel time is a failure of its destination (out of work time
        // it ends in SetStatus).
        if (ai::direct_targets::AnyBuilt() && GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
            ai::direct_targets::OnTravelFailure(ai, tDestination, "expired");
        SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        ai->GetAiObjectContext()->ClearValues("no active travel destinations");
        return;
    }

    if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL)
    {
        bool HasArrived = tDestination->IsIn(bot);

        if (HasArrived)
        {
            if (ai->HasStrategy("travel once", BotState::BOT_STATE_NON_COMBAT))
            {
                ai->TellDebug(ai->GetMaster(), "The target is clearing because it was a travel once destination.", "debug travel");
                ai->ChangeStrategy("nc -travel once", BotState::BOT_STATE_NON_COMBAT);
                sTravelMgr.SetNullTravelTarget(this);
                return;
            }

            // twow-repo#601 S5a (BandLadder): a band trip ends at the hub (arrived) - no WORK phase at a hub; the quest search
            // starts afresh there (ResetBandStep).
            if (bandTripEnd)
            {
                EndBandTrip(ai::quest_band::EndArrived);
                return;
            }

            ai->TellDebug(ai->GetMaster(), "The target is starting to work because the destination has been reached.", "debug travel");
            TraceQuestCommit(tDestination, "progress", "arrived");
            // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall, review F1): the arrival ends this trip's
            // observation, so the next trip to the same taker (after a work timeout; #329 keeps the state on a re-pick)
            // starts a fresh stall window instead of a stale one that stalls it at once. Off: as before.
            if (sPlayerbotAIConfig.zoneExitTurnInTravelStall && sPlayerbotAIConfig.S3For(bot->GetGUIDLow()) && IsProgressAwareTurnIn())
                turnInRecovery.ResetProgress();
            // twow-repo#594 / P3-b: arrivals of the direct-target destinations (failure memory, hole_present for fishing).
            if (ai::direct_targets::AnyBuilt())
                ai::direct_targets::OnArrive(tDestination, ai, wPosition);
            SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
            return;
        }
        else if(IsForced()) return; //While traveling do not go into cooldown

        // twow-repo#594 (AiPlayerbot.Path.PosStuck, OB-00 23.10.2026): the position guard, switch first. An episode parks the
        // target and leaves it (and may move the bot); the turn-in recovery below does not run for it any more.
        if (sPlayerbotAIConfig.pathPosStuck && ObservePosStuck())
            return;

        if (IsProgressAwareQuestTravel())
        {
            switch (ObserveTurnInProgress())
            {
            case turnin_recovery::RecoveryAction::RecomputeRoute:
                if (sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)
                    sLog.outBasic("[QuestFirstRoute] state=recovery_stage1 bot=%u quest=%u target_entry=%d action=recompute_route",
                        bot->GetGUIDLow(), static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), tDestination->GetEntry());
                TraceQuestCommit(tDestination, "blocked", "stall_recompute_route");
                // twow-repo#587 (Z4, AiPlayerbot.ZoneExit.TurnInStallCounts): a stage-1 recovery of a finished quest's turn-in is a
                // failed turn-in too - live, Aurelith cycled commit -> move_retry_cooldown -> drop -> recompute for an
                // unreachable Stormwind taker and never reached stage 2. TurnInParkFailures of them park the turn-in (#485).
                // Always visible, switch or not: every unreachable turn-in target is logged and counted ([UnreachableTop]).
                if (IsProgressAwareTurnIn())
                {
                    uint32 const questId = static_cast<QuestTravelDestination*>(tDestination)->GetQuestId();
                    // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall): inside the near zone the stall ends the
                    // trip - structural park, [UnreachableTarget] reason=near_stall, the target is left. Off: as before.
                    if (IsNearTurnInStall())
                    {
                        OnNearTurnInStall(questId);
                        return;
                    }
                    bool const parked = sPlayerbotAIConfig.zoneExitTurnInStallCounts && NoteTurnInFailure(questId, "stall_recompute");
                    float const dx = wPosition ? wPosition->getX() : 0.0f, dy = wPosition ? wPosition->getY() : 0.0f, dz = wPosition ? wPosition->getZ() : 0.0f;
                    uint32 const dmap = wPosition ? wPosition->getMapId() : bot->GetMapId();
                    sLog.outBasic("[UnreachableTarget] bot=%u level=%u quest=%u entry=%d kind=turn_in map=%u zone=%u dest_map=%u dest_x=%.1f dest_y=%.1f dest_z=%.1f reason=stage1_recompute parked=%u switch=%u",
                        bot->GetGUIDLow(), bot->GetLevel(), questId, tDestination->GetEntry(), bot->GetMapId(), bot->GetZoneId(), dmap, dx, dy, dz,
                        parked ? 1u : 0u, sPlayerbotAIConfig.zoneExitTurnInStallCounts ? 1u : 0u);
                    unreachable_target::Key const key = unreachable_target::KeyOf(tDestination->GetEntry(), dmap, dx, dy);
                    unreachable_target::ShortWindow().Add(key, questId, bot->GetGUIDLow());
                    unreachable_target::LongWindow().Add(key, questId, bot->GetGUIDLow());
                }
                SetStatus(TravelStatus::TRAVEL_STATUS_READY);
                return;
            case turnin_recovery::RecoveryAction::SuppressRouteAndCooldown:
                if (sPlayerbotAIConfig.questFirstProgressionTraceTravelDecisions)
                    sLog.outBasic("[QuestFirstRoute] state=recovery_stage2 bot=%u quest=%u target_entry=%d action=suppress_route_cooldown cooldown_seconds=%u",
                        bot->GetGUIDLow(), static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), tDestination->GetEntry(),
                        sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds);
                // twow-repo#601 S3c (AiPlayerbot.ZoneExit.TurnInTravelStall, review F4): a near-zone stall that comes as
                // stage 2 (the stage was kept from a stall outside the zone) ends the trip the same way - structural
                // park, [UnreachableTarget] reason=near_stall. Off: as before.
                if (IsNearTurnInStall())
                {
                    OnNearTurnInStall(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId());
                    return;
                }
                TraceQuestCommit(tDestination, "abandon", "stall_suppressed");
                // twow-repo#485: a stalled turn-in (not an objective) is a failed turn-in.
                if (IsProgressAwareTurnIn())
                    NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), "stall");
                SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
                SetExpireIn(sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds * IN_MILLISECONDS);
                return;
            case turnin_recovery::RecoveryAction::SuppressTransportAndCooldown:
                TraceQuestCommit(tDestination, "abandon", "transport_stall");
                // twow-repo#485: an abandoned cross-map turn-in (not an objective) is a failed turn-in.
                if (IsProgressAwareTurnIn())
                    NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), "transport_stall");
                SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
                SetExpireIn(sPlayerbotAIConfig.questFirstProgressionTurnInRouteCooldownSeconds * IN_MILLISECONDS);
                return;
            case turnin_recovery::RecoveryAction::None:
                break;
            }
        }
    }

    if (GetStatus() != TravelStatus::TRAVEL_STATUS_COOLDOWN)
    {
        bool destinationInactive = !IsDestinationActive() && !IsForced();
        bool conditionsInactive = !destinationInactive && !IsConditionsActive(); // Only check conditions if destination is still active

        if (destinationInactive || conditionsInactive)
        {
            // twow-repo#601 S2 / S4 R2 (OB-10 rev. 2 point 3, review C-F1 / B-F1): a gap grind trip ends here at once - no COOLDOWN
            // (its 60 s would hold every request past the window), no failure. Its condition is R1 itself ("need travel
            // purpose::Grind"), so a condition end is the window end (the backoff ended or was reset): expired=. An
            // inactive destination (a level-up out of the window, should sell) ends silently. gapWindowEnd: GrindMode 1 only.
            if (gapWindowEnd)
            {
                ai->TellDebug(ai->GetMaster(), "The gap grind trip ends because its destination or its window is no longer active.", "debug travel");
                if (conditionsInactive)
                    PlayerbotAI::NoteGrindWindow(uint8(ai::quest_grind_mode::WindowEvent::Expired));
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                ai->GetAiObjectContext()->ClearValues("no active travel destinations");
                return;
            }
            // twow-repo#601 S5a (BandLadder, OB-10 Q1 19.10.2026): a quest giver trip that the band giver filter made inactive
            // because its quest is now in the log or done ends at once (EXPIRED, giver_done=) - a success, no 60 s COOLDOWN,
            // no failure. GiverTripDoneFor reads the switch first: BandLadder 0 never gets past it.
            if (destinationInactive && ai::quest_band::GiverTripDoneFor(ai, bot, tDestination))
            {
                ai->TellDebug(ai->GetMaster(), "The quest giver trip ends because its quest was taken.", "debug travel");
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                ai->GetAiObjectContext()->ClearValues("no active travel destinations");
                return;
            }
            // twow-repo#601 (QuestLoop.GiverTakerDone): a quest giver trip whose quest is now taken, or a taker trip whose quest is turned
            // in, went inactive because it succeeded - it ends at once (EXPIRED, drop reason=relation_done): a success, no 60 s
            // COOLDOWN, no failure. A roster bot on its own only (a group trip judges a member's quest log: as before); off, another
            // cohort or no success: the COOLDOWN path below as before.
            if (destinationInactive && ai::cohort::Applies(sPlayerbotAIConfig.questLoopGiverTakerDone, bot->GetGUIDLow()) &&
                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() && !groupMember &&
                GiverTakerTripDone(bot, tDestination))
            {
                ai->TellDebug(ai->GetMaster(), "The quest giver or taker trip ends because its quest was taken or turned in.", "debug travel");
                TraceQuestCommit(tDestination, "drop", "relation_done");
                SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                ai->GetAiObjectContext()->ClearValues("no active travel destinations");
                return;
            }
            ai->TellDebug(ai->GetMaster(), "The target is cooling down because the destination was no longer active or the conditions are no longer true.", "debug travel");
            TraceQuestCommit(tDestination, "drop", destinationInactive ? "destination_inactive" : "conditions_inactive");
            forced = false;
            SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
            // twow-repo#601 (QuestLoop.InactiveAfterProgress): no living target in sight after the bot got credit on this objective is
            // progress, not an emptied camp - a short cooldown, no QL-2, no QL-1; the count is taken again, so the next inactive
            // drop without new credit is an emptied camp. A roster bot on its own only; off or another cohort: as before.
            bool const progressRule = destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination) &&
                ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveAfterProgress, bot->GetGUIDLow()) &&
                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
            int32 const objectiveCountNow = progressRule ? ObjectiveCountOf(bot, tDestination) : -1;
            if (progressRule && quest_loop::InactiveAfterProgress(objectiveCountAtStart, objectiveCountNow))
            {
                QuestObjectiveTravelDestination const* objective = static_cast<QuestObjectiveTravelDestination const*>(tDestination);
                sLog.outBasic("[QuestLoop] state=inactive_after_progress bot=%u level=%u quest=%u target_entry=%d count_from=%d count_to=%d cooldown_ms=%u",
                    bot->GetGUIDLow(), bot->GetLevel(), objective->GetQuestId(), objective->GetEntry(), objectiveCountAtStart, objectiveCountNow,
                    quest_loop::ProgressCooldownMs);
                SetExpireIn(quest_loop::ProgressCooldownMs);
            }
            // twow-repo#581: an emptied quest camp - QL-2 longer cooldown, QL-1 counted against the quest.
            else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))
            {
                if (sPlayerbotAIConfig.questLoopInactiveCooldownSeconds)
                    SetExpireIn(quest_loop::InactiveCooldownMs(tDestination->GetCooldownDelay(), sPlayerbotAIConfig.questLoopInactiveCooldownSeconds));
                NoteQuestObjectiveFailure();
                // twow-repo#601 (QuestLoop.InactiveReselect): the emptied camp is remembered for the cooldown just set and the target ends,
                // so the next search runs now and skips it (as does the refresh). A roster bot on its own only; off or another cohort: as
                // before. No quest target found -> FallBackToInactiveCooldown (ChooseTravelTargetAction) restores this COOLDOWN.
                if (ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveReselect, bot->GetGUIDLow()) &&
                    sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())
                {
                    QuestObjectiveTravelDestination const* objective = static_cast<QuestObjectiveTravelDestination const*>(tDestination);
                    uint32 const markMs = quest_loop::InactiveCooldownMs(tDestination->GetCooldownDelay(), sPlayerbotAIConfig.questLoopInactiveCooldownSeconds);
                    quest_loop::MarkEmptyCamp(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime(), markMs);
                    sLog.outBasic("[QuestLoop] state=inactive_reselect bot=%u level=%u quest=%u target_entry=%d mark_ms=%u",
                        bot->GetGUIDLow(), bot->GetLevel(), objective->GetQuestId(), objective->GetEntry(), markMs);
                    // twow-repo#601 (QuestLoop.EmptyCampHold): the camp stays marked until its spawn point respawns (at most 900 s),
                    // not only for the cooldown - its own slot again. Off, another cohort or a respawn within the cooldown: the mark
                    // above as before.
                    if (ai::cohort::Applies(sPlayerbotAIConfig.questLoopEmptyCampHold, bot->GetGUIDLow()))
                    {
                        uint32 const respawnSeconds = SpawnRespawnSeconds(wPosition);
                        uint32 const holdMs = quest_loop::EmptyCampHoldMs(markMs, respawnSeconds);
                        if (holdMs > markMs)
                        {
                            quest_loop::MarkEmptyCamp(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime(), holdMs);
                            sLog.outBasic("[QuestLoop] state=empty_camp_hold bot=%u level=%u quest=%u target_entry=%d respawn_s=%u base_ms=%u hold_ms=%u",
                                bot->GetGUIDLow(), bot->GetLevel(), objective->GetQuestId(), objective->GetEntry(), respawnSeconds, markMs, holdMs);
                        }
                    }
                    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
                    inactiveReselectPending = true;
                    ai->GetAiObjectContext()->ClearValues("no active travel destinations");
                }
            }
            if (progressRule)
                objectiveCountAtStart = objectiveCountNow;
            return;
        }
    }
}

bool TravelTarget::IsActive() {
    if (m_status == TravelStatus::TRAVEL_STATUS_NONE || m_status == TravelStatus::TRAVEL_STATUS_EXPIRED || m_status == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    return true;
};

TravelState TravelTarget::GetTravelState() {
    if (!tDestination || typeid(*tDestination) == typeid(NullTravelDestination))
        return TravelState::TRAVEL_STATE_IDLE;

    if (typeid(*tDestination) == typeid(QuestRelationTravelDestination))
    {
        if (((QuestRelationTravelDestination*)tDestination)->GetRelation() == 0)
        {
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                return TravelState::TRAVEL_STATE_TRAVEL_PICK_UP_QUEST;
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                return TravelState::TRAVEL_STATE_WORK_PICK_UP_QUEST;
        }
        else
        {
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
                return TravelState::TRAVEL_STATE_TRAVEL_HAND_IN_QUEST;
            if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
                return TravelState::TRAVEL_STATE_WORK_HAND_IN_QUEST;
        }
    }
    else if (typeid(*tDestination) == typeid(QuestObjectiveTravelDestination))
    {
        if (GetStatus() == TravelStatus::TRAVEL_STATUS_TRAVEL || GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
            return TravelState::TRAVEL_STATE_TRAVEL_DO_QUEST;
        if (GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
            return TravelState::TRAVEL_STATE_WORK_DO_QUEST;
    }
    else if (typeid(*tDestination) == typeid(RpgTravelDestination))
    {
        return TravelState::TRAVEL_STATE_TRAVEL_RPG;
    }
    else if (typeid(*tDestination) == typeid(ExploreTravelDestination))
    {
        return TravelState::TRAVEL_STATE_TRAVEL_EXPLORE;
    }

    return TravelState::TRAVEL_STATE_IDLE;
}

void TravelMgr::Clear()
{
#ifndef MANGOSBOT_ZERO
    sObjectAccessor.ExecuteOnAllPlayers([this](Player* plr) { TravelMgr::SetNullTravelTarget(plr); });
#else
    HashMapHolder<Player>::ReadGuard g(HashMapHolder<Player>::GetLock());
    HashMapHolder<Player>::MapType& m = sObjectAccessor.GetPlayers();
    for (HashMapHolder<Player>::MapType::iterator itr = m.begin(); itr != m.end(); ++itr)
        TravelMgr::SetNullTravelTarget(itr->second);
#endif
    for (auto& [purpose, entries] : destinationMap)
        for (auto& [id, dests] : entries)
            for (auto& dest : dests)
                delete dest;
    destinationMap.clear();
    pointsMap.clear();
    fishPoints.clear();
    directPoints.clear();
}

int32 TravelMgr::GetAreaLevel(uint32 area_id)
{
    std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);

    // #307: configured levels for areas the generated table misses (Turtle
    // zones such as the high elf start 5225 Thalassian Highlands).
    auto const configured = sPlayerbotAIConfig.areaLevelOverrides.find(area_id);
    if (configured != sPlayerbotAIConfig.areaLevelOverrides.end())
        return configured->second;

    auto lev = areaLevels.find(area_id);

    if (lev != areaLevels.end())
        return lev->second;

    AreaTableEntry const* area = GetAreaEntryByAreaID(area_id);

    if (!area)
    {
        areaLevels[area_id] = -2;
        return -2;
    }

    //Get exploration level
    if (area->area_level) 
    {
        areaLevels[area_id] = area->area_level;
        return area->area_level;
    }


    int32 level = 0;
    uint32 cnt = 0;

    //Get sub-area's
    // #416 (7.3): ids go past the record count of the SQL store.
    for (uint32 i = 0; i < ai::area_level::AreaIdEnd(sAreaStore.GetMaxEntry(), sAreaStore.GetNumRows()); i++)
    {
        AreaTableEntry const* subArea = GetAreaEntryByAreaID(i);

        if (!subArea || subArea->zone != area->ID)
            continue;

        int32 subLevel = GetAreaLevel(subArea->ID);

        if (!subLevel)
            continue;

        level += subLevel;

        cnt++;
    }

    if (cnt)
    {
        areaLevels[area_id] = std::max(uint32(1), level / cnt);
        return areaLevels[area_id];
    }

    //Get units avarage
    // #416 (7.3): this was a scan over every creature of the world, with a
    // terrain lookup each, per area - 17-20 s on the map thread when a bot first
    // touched such an area. Now one pass fills all areas while LoadAreaLevels
    // runs at startup; at runtime an uncached area falls through to its zone.
    if (ai::area_level::MayUseCreatureLevels(loadingAreaLevels))
    {
        if (!creatureAreaLevels.loaded)
            LoadCreatureAreaLevels();

        int32 const unitLevel = creatureAreaLevels.Average(area->ID);
        if (unitLevel > 0)
        {
            areaLevels[area_id] = unitLevel;
            return areaLevels[area_id];
        }
    }

    //Use parent zone value.
    if (area->zone)
    {
        areaLevels[area_id] = 0; //Set a temporary value so it wont be counted.
        level = GetAreaLevel(area->zone);
        areaLevels[area_id] = level;        
        return areaLevels[area_id];
    }

    areaLevels[area_id] = -1;

    return areaLevels[area_id];
}

void TravelMgr::LoadCreatureAreaLevels()
{
    std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);
    creatureAreaLevels.loaded = true;

    uint32 const start = WorldTimer::getMSTime();
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    for (auto& creaturePair : WorldPosition().getCreaturesNear())
    {
        CreatureData const& cData = creaturePair->second;
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);

        if (!cInfo)
            continue;

        FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry);

        if (reactionHum > REP_NEUTRAL || reactionOrc > REP_NEUTRAL)
            continue;

        if (AreaTableEntry const* creatureArea = WorldPosition(creaturePair).GetArea())
            creatureAreaLevels.Add(creatureArea->ID, cInfo->MaxLevel);
    }

    sLog.outString(">> Area levels: creature levels for %u areas in one pass (%u ms).",
        creatureAreaLevels.Size(), WorldTimer::getMSTimeDiffToNow(start));
}

void TravelMgr::LoadAreaLevels()
{
    std::lock_guard<std::recursive_mutex> lock(areaLevelMutex);

    if (!areaLevels.empty())
        return;

    // #416 (7.3): the creature levels may only be used during this load.
    struct LoadingFlag
    {
        bool& flag;
        explicit LoadingFlag(bool& f) : flag(f) { flag = true; }
        ~LoadingFlag() { flag = false; }
    } const loading(loadingAreaLevels);

    WorldDatabase.PExecute("CREATE TABLE IF NOT EXISTS `ai_playerbot_zone_level` (`id` bigint(20) NOT NULL ,`level` bigint(20) NOT NULL,PRIMARY KEY(`id`))");

    std::string query = "SELECT id, level FROM ai_playerbot_zone_level";

    {
        auto result = WorldDatabase.PQuery("%s", query.c_str());

        std::vector<uint32> loadedAreas;

        if (result)
        {
            BarGoLink bar(result->GetRowCount());            

            do
            {
                Field* fields = result->Fetch();
                bar.step();

                areaLevels[fields[0].GetUInt32()] = fields[1].GetInt32();

                loadedAreas.push_back(fields[0].GetUInt32());
            } while (result->NextRow());

            sLog.outString(">> Loaded " SIZEFMTD " area levels.", areaLevels.size());
        }

        // #416 (7.3): GetNumRows() is the record count of the SQL store (1481); the
        // ids go up to GetMaxEntry() (Turtle areas up to 5735).
        uint32 const areaIdEnd = ai::area_level::AreaIdEnd(sAreaStore.GetMaxEntry(), sAreaStore.GetNumRows());
        BarGoLink bar(areaIdEnd);
        WorldDatabase.BeginTransaction();
        for (uint32 i = 0; i < areaIdEnd; ++i)
        {
            bar.step();
            if (AreaTableEntry const* area = sAreaStore.LookupEntry<AreaEntry>(i))
            {
                if (std::find(loadedAreas.begin(), loadedAreas.end(), area->ID) == loadedAreas.end())
                {
                    int32 level = sTravelMgr.GetAreaLevel(area->ID);

                    WorldDatabase.PExecute("INSERT INTO `ai_playerbot_zone_level` (`id`, `level`) VALUES ('%d', '%d')", area->ID, level);
                }
            }
        }
        WorldDatabase.CommitTransaction();
        if(areaLevels.size() > loadedAreas.size())
            sLog.outString(">> Generated " SIZEFMTD " areas.", areaLevels.size()- loadedAreas.size());
    }
}

void TravelMgr::SetMobAvoidArea()
{
    sLog.outString("-Apply mob avoidance maps");

    std::vector<std::future<void>> calculations;

    BarGoLink bar(sMapStore.GetNumRows());

    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;
        
        uint32 mapId = sMapStore.LookupEntry(i)->MapID;
        calculations.push_back(std::async([this, mapId] { SetMobAvoidAreaMap(mapId); }));
        bar.step();
    }

    BarGoLink bar2(calculations.size());
    for (uint32 i = 0; i < calculations.size(); i++)
    {
        calculations[i].wait();
        bar2.step();
    }

    sLog.outString(">> Modified navmap areas for %d maps.", sMapStore.GetNumRows());
}

void TravelMgr::SetMobAvoidAreaMap(uint32 mapId) 
{
    PathFinder path(mapId, 0);
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    std::vector<CreatureDataPair const*> creatures = WorldPosition(mapId, 1,1).getCreaturesNear();

    for (auto& creaturePair : creatures)
    {
        CreatureData const cData = creaturePair->second;
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);

        if (!cInfo)
            continue;

        WorldPosition point = WorldPosition(cData.position.mapid, cData.position.coord_x, cData.position.coord_y, cData.position.coord_z, cData.position.orientation);

        if (cInfo->NpcFlags > 0)
            continue;

        FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry);

        if (reactionHum >= REP_NEUTRAL || reactionOrc >= REP_NEUTRAL)
            continue;

        if (!point.getTerrain())
            continue;

        if (!point.loadMapAndVMap(0))
            continue;

        path.setArea(point.getMapId(), point.getX(), point.getY(), point.getZ(), 12, 50.0f);
        path.setArea(point.getMapId(), point.getX(), point.getY(), point.getZ(), 13, 20.0f);
    }
}

// Custom player-only starting zones with no MMAP support: Blackstone Island (Goblin, map 1)
// and Thalassian Highlands (High Elf, map 0). Bots are spawned in Durotar/Elwynn instead
// (RandomPlayerbotFactory::CreateRandomBot) and RandomPlayerbotMgr::RandomTeleport already
// excludes these by zone ID for teleport destinations. This is a plain coordinate check
// (bounds taken from the zones' own NPC spawns) rather than a zone-ID lookup because this
// function runs once at startup over every quest-relevant spawn in the game, and
// GetZoneAndAreaId can force-load grid data per call - doing that here deadlocked the server.
// twow-repo#575 (PATH-01 #4, AiPlayerbot.Path.StartZoneExactBounds): the boxes above left 8 Blackstone and 10
// Thalassian quest givers outside, so Horde bots in Durotar chose island targets without a route. 1 = the zone
// boxes in PathFailPolicy.h (from the zone cells of the map files, no Plaguelands spawn inside); 0 = the old boxes.
static bool IsInBlackstoneOrThalassian(uint32 mapId, float x, float y)
{
    return ai::path_fail::InCustomStartZone(mapId, x, y, sPlayerbotAIConfig.pathStartZoneExactBounds);
}

// twow-repo#575 (PATH-01 E4, AiPlayerbot.CustomRaceStartZones): on, an entry whose spawns all lie in one start zone
// keeps them, and its destinations are tagged with that zone's faction (E5 in GetDestinations); an entry with spawns
// inside and outside keeps only the outside ones. Off: None, and the old exclusion above applies.
template <class Points, class Dests>
static ai::start_zone::StartTeam StartZoneTeamOf(Points const& points, Dests const& dests)
{
    if (!sPlayerbotAIConfig.customRaceStartZones)
        return ai::start_zone::StartTeam::None;

    ai::start_zone::TeamFold fold;
    for (auto const& guidP : points)
        fold.Add(ai::start_zone::TeamOfPoint(guidP.getMapId(), guidP.getX(), guidP.getY()));

    ai::start_zone::StartTeam const team = fold.Uniform();
    if (team != ai::start_zone::StartTeam::None)
        for (auto dest : dests)
            dest->SetStartZoneTeam(uint8(team));
    return team;
}

template <class Point>
static bool SkipStartZonePoint(Point const& guidP, ai::start_zone::StartTeam uniform)
{
    if (!sPlayerbotAIConfig.customRaceStartZones)
        return IsInBlackstoneOrThalassian(guidP.getMapId(), guidP.getX(), guidP.getY());
    return !ai::start_zone::KeepStartZonePoint(true, ai::start_zone::TeamOfPoint(guidP.getMapId(), guidP.getX(), guidP.getY()), uniform);
}

// twow-repo#510: [ShimBounds] exposure lines, once per boot at the end of LoadQuestTravelTable and the same in
// both modes. They read the template maps, the spawn map and the drop map and change nothing. quest_loot counts
// the quests whose required item drops only from spawned templates above the caps (from the full template maps,
// so independent of the mode) and how many of those the bot drop map resolves to such a template.
static void LogShimBounds(EntryGuidps const& guidpMap)
{
    uint32 const mode = cmangos_shim::g_idSets.load(std::memory_order_relaxed) ? 1 : 0;
    uint32 const creatureCap = cmangos_shim::kLegacyCreatureCap;
    uint32 const goCap = cmangos_shim::kLegacyGOCap;
    auto spawned = [&guidpMap](int32 entry)
    {
        auto const it = guidpMap.find(entry);
        return it != guidpMap.end() && !it->second.empty();
    };

    {
        uint32 maxId = 0, aboveCap = 0, aboveCapSpawned = 0, iterated = 0, iteratedAboveCap = 0;
        for (auto const& kv : sObjectMgr.GetCreatureInfoMap())
        {
            maxId = std::max(maxId, kv.first);
            if (kv.first >= creatureCap)
            {
                ++aboveCap;
                if (spawned(int32(kv.first)))
                    ++aboveCapSpawned;
            }
        }
        uint32 const started = WorldTimer::getMSTime();
        for (uint32 entry : sCreatureStorage.Ids())
            if (sCreatureStorage.LookupEntry<CreatureInfo>(entry))
            {
                ++iterated;
                if (entry >= creatureCap)
                    ++iteratedAboveCap;
            }
        sLog.outString("[ShimBounds] mode=%u type=creature cap=%u max=%u total=%u above_cap=%u above_cap_spawned=%u iterated=%u iterated_above_cap=%u cost_ms=%u",
            mode, creatureCap, maxId, uint32(sObjectMgr.GetCreatureInfoMap().size()), aboveCap, aboveCapSpawned, iterated, iteratedAboveCap, WorldTimer::getMSTimeDiffToNow(started));
    }

    {
        uint32 maxId = 0, aboveCap = 0, aboveCapSpawned = 0, iterated = 0, iteratedAboveCap = 0;
        for (auto const& kv : sObjectMgr.GetGameObjectInfoMap())
        {
            maxId = std::max(maxId, kv.first);
            if (kv.first >= goCap)
            {
                ++aboveCap;
                if (spawned(-int32(kv.first)))
                    ++aboveCapSpawned;
            }
        }
        uint32 const started = WorldTimer::getMSTime();
        for (uint32 entry : sGOStorage.Ids())
            if (sGOStorage.LookupEntry<GameObjectInfo>(entry))
            {
                ++iterated;
                if (entry >= goCap)
                    ++iteratedAboveCap;
            }
        sLog.outString("[ShimBounds] mode=%u type=go cap=%u max=%u total=%u above_cap=%u above_cap_spawned=%u iterated=%u iterated_above_cap=%u cost_ms=%u",
            mode, goCap, maxId, uint32(sObjectMgr.GetGameObjectInfoMap().size()), aboveCap, aboveCapSpawned, iterated, iteratedAboveCap, WorldTimer::getMSTimeDiffToNow(started));
    }

    {
        // A2 reads its switch live (no prebuilt set); with 1 the loops reach every reputation faction.
        uint32 const factionMode = sPlayerbotAIConfig.shimFactionIdSet ? 1 : 0;
        uint32 maxId = 0, aboveCap = 0, repTotal = 0, aboveCapRep = 0;
        for (auto const& kv : sObjectMgr.GetFactionMap())
        {
            maxId = std::max(maxId, kv.first);
            bool const rep = kv.second.CanHaveReputation();
            if (rep)
                ++repTotal;
            if (kv.first >= cmangos_shim::kLegacyFactionCap)
            {
                ++aboveCap;
                if (rep)
                    ++aboveCapRep;
            }
        }
        sLog.outString("[ShimBounds] mode=%u type=faction cap=%u max=%u total=%u above_cap=%u rep_total=%u above_cap_rep=%u iterated_above_cap=%u",
            factionMode, cmangos_shim::kLegacyFactionCap, maxId, uint32(sObjectMgr.GetFactionMap().size()), aboveCap, repTotal, aboveCapRep, factionMode ? aboveCapRep : 0);
    }

    {
        uint32 const started = WorldTimer::getMSTime();
        // item -> number of spawned loot sources below / above the caps
        std::unordered_map<uint32, std::pair<uint32, uint32>> sources;
        auto addLoot = [&sources](ObjectGuid guid, bool above)
        {
            LootTemplateAccess const* lTemplateA = DropMapValue::GetLootTemplate(guid, LOOT_CORPSE);
            if (!lTemplateA)
                return;
            auto add = [&sources, above](LootStoreItem const& lItem)
            {
                auto& counts = sources[lItem.itemid];
                ++(above ? counts.second : counts.first);
            };
            for (LootStoreItem const& lItem : lTemplateA->Entries)
                add(lItem);
            for (LootLootGroupAccess const& group : lTemplateA->Groups)
            {
                for (LootStoreItem const& lItem : group.ExplicitlyChanced)
                    add(lItem);
                for (LootStoreItem const& lItem : group.EqualChanced)
                    add(lItem);
            }
        };
        for (auto const& kv : sObjectMgr.GetCreatureInfoMap())
            if (spawned(int32(kv.first)))
                addLoot(ObjectGuid(HIGHGUID_UNIT, kv.first, uint32(1)), kv.first >= creatureCap);
        for (auto const& kv : sObjectMgr.GetGameObjectInfoMap())
            if (spawned(-int32(kv.first)))
                addLoot(ObjectGuid(HIGHGUID_GAMEOBJECT, kv.first, uint32(1)), kv.first >= goCap);

        DropMap* dropMap = GAI_VALUE(DropMap*, "drop map");
        uint32 capOnlyQuests = 0, capOnlyMinLevelLe29 = 0, capOnlyResolved = 0, capOnlyItems = 0;
        for (auto const& [questId, questPtr] : sObjectMgr.GetQuestTemplates())
        {
            uint32 questItems = 0, firstItem = 0;
            bool resolved = true;
            for (uint32 objective = 0; objective < QUEST_ITEM_OBJECTIVES_COUNT; ++objective)
            {
                uint32 const itemId = questPtr->ReqItemId[objective];
                if (!itemId)
                    continue;
                auto const it = sources.find(itemId);
                if (it == sources.end() || it->second.first || !it->second.second)
                    continue;
                if (!questItems++)
                    firstItem = itemId;
                ++capOnlyItems;
                bool found = false;
                if (dropMap)
                {
                    auto const range = dropMap->equal_range(itemId);
                    for (auto r = range.first; r != range.second && !found; ++r)
                        found = r->second >= int32(creatureCap) || r->second <= -int32(goCap);
                }
                if (!found)
                    resolved = false;
            }
            if (!questItems)
                continue;
            ++capOnlyQuests;
            if (questPtr->GetMinLevel() <= 29)
                ++capOnlyMinLevelLe29;
            if (resolved)
                ++capOnlyResolved;
            // one line per quest (about 200, once per boot) for the cross-check against a world DB dump
            sLog.outString("[ShimBounds] mode=%u type=cap_only quest=%u min_level=%u item=%u items=%u resolved=%u",
                mode, questId, questPtr->GetMinLevel(), firstItem, questItems, resolved ? 1 : 0);
        }
        sLog.outString("[ShimBounds] mode=%u type=quest_loot cap_only_quests=%u cap_only_min_level_le29=%u cap_only_items=%u cap_only_resolved=%u cost_ms=%u",
            mode, capOnlyQuests, capOnlyMinLevelLe29, capOnlyItems, capOnlyResolved, WorldTimer::getMSTimeDiffToNow(started));
    }
}

void TravelMgr::LoadQuestTravelTable()
{
    if (!sTravelMgr.destinationMap.empty())
        return;

    // Clearing store (for reloading case)
    Clear();

    sLog.outString("Loading trainable spells.");
    GAI_VALUE(trainableSpellMap*, "trainable spell map");
    GAI_VALUE(std::vector<MountValue>, "full mount list");
    
    sLog.outString("Loading object locations.");

    EntryGuidps guidpMap = GAI_VALUE(EntryGuidps, "entry guidps");

    sLog.outString("Finding possible travel destinations.");

    EntryQuestRelationMap eMap = GAI_VALUE(EntryQuestRelationMap, "entry quest relation");

    sLog.outString("Creating travel destinations.");

    BarGoLink bar(eMap.size());

    for (auto& [entry, relation] : eMap)
    {


        bar.step();
        for (auto& [questId, flag] : relation)
        {
            if (guidpMap.find(entry) == guidpMap.end())
            {
                sLog.outDebug("Entry %d for quest %d has no valid location.", entry, questId);
                continue;
            }

            std::vector<QuestTravelDestination*> locs;

            for (uint32 purposeFlagNr = 0; purposeFlagNr < 6; purposeFlagNr++)
            {
                TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << purposeFlagNr);
                if (flag & (uint32)purposeFlag)
                {
                    QuestTravelDestination* loc = nullptr;

                    if (purposeFlag == TravelDestinationPurpose::QuestGiver || purposeFlag == TravelDestinationPurpose::QuestTaker)
                        loc = AddDestination<QuestRelationTravelDestination>(entry, purposeFlag, questId);
                    else
                        loc = AddDestination<QuestObjectiveTravelDestination>(entry, purposeFlag, questId);

                    locs.push_back(loc);
                }
            }

            if (!locs.empty())
            {
                // twow-repo#485: a game object that is not spawned by default
                // (spawntimesecsmin < 0) only appears when a script summons it - GO 270,
                // the taker of quest 310, stands for 60 s after the end script of 308.
                // As a quest giver or taker it is no travel target: bots arrived at an
                // empty spot until the work phase ran out. Objectives keep every point.
                bool const skipScriptOnly = sPlayerbotAIConfig.questFirstProgressionSkipScriptOnlyQuestTakers && entry < 0;
                uint32 scriptOnlyPoints = 0;
                ai::start_zone::StartTeam const startTeam = StartZoneTeamOf(guidpMap.at(entry), locs);

                for (auto& guidP : guidpMap.at(entry))
                {
                    // Never send bots to custom player-only starting zones (no MMAP support).
                    // Same exclusion as RandomPlayerbotMgr::RandomTeleport, but done as a plain
                    // coordinate check here: this loop runs over every quest-relevant spawn in
                    // the game, and GetZoneAndAreaId can force-load grid data per call, which
                    // deadlocked the server when tried at this stage of the boot sequence.
                    if (SkipStartZonePoint(guidP, startTeam))
                        continue;

                    pointsMap.insert(std::make_pair(guidP.GetRawValue(), guidP));

                    GameObjectData const* goData = skipScriptOnly ? guidP.GetGameObjectData() : nullptr;
                    bool const scriptOnly = goData && goData->spawntimesecsmin < 0;
                    bool skipped = false;

                    for (auto tLoc : locs)
                    {
                        if (scriptOnly && dynamic_cast<QuestRelationTravelDestination*>(tLoc))
                        {
                            skipped = true;
                            continue;
                        }

                        tLoc->AddPoint(&pointsMap.at(guidP.GetRawValue()));
                    }

                    if (skipped)
                        ++scriptOnlyPoints;
                }

                // Startup only: one line per affected quest and game object.
                if (scriptOnlyPoints)
                    sLog.outBasic("[QuestFirstRoute] state=taker_script_only quest=%u entry=%d skipped_points=%u",
                        questId, entry, scriptOnlyPoints);
            }
        }
    }

    sLog.outString("Loading all travel locations.");

    for (auto& [entry, purpose] : GAI_VALUE(EntryTravelPurposeMap, "entry travel purpose"))
    {
        std::vector<TravelDestination*> dests;

        if (guidpMap.find(entry) == guidpMap.end())
            continue;

        for (uint32 purposeFlagNr = 6; purposeFlagNr < 18; purposeFlagNr++)
        {
            TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << purposeFlagNr);
            if (purpose & (uint32)purposeFlag)
            {
                switch (purposeFlag) {
                case TravelDestinationPurpose::GenericRpg:
                case TravelDestinationPurpose::Trainer:
                case TravelDestinationPurpose::Repair:
                case TravelDestinationPurpose::Vendor:
                case TravelDestinationPurpose::AH:
                case TravelDestinationPurpose::Mail:
                    dests.push_back(AddDestination<RpgTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::GatherSkinning:
                case TravelDestinationPurpose::GatherMining:
                case TravelDestinationPurpose::GatherHerbalism:
                    dests.push_back(AddDestination<GatherTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::Grind:
                    dests.push_back(AddDestination<GrindTravelDestination>(entry, purposeFlag));
                    break;
                case TravelDestinationPurpose::Boss:
                    dests.push_back(AddDestination<BossTravelDestination>(entry, purposeFlag));
                    break;
                default:
                    break;
                };
            }
        }

        if (dests.empty())
            continue;

        ai::start_zone::StartTeam const startTeam = StartZoneTeamOf(guidpMap.at(entry), dests);

        for (auto& guidP : guidpMap.at(entry))
        {
            // Never send bots to custom player-only starting zones (no MMAP support).
            if (SkipStartZonePoint(guidP, startTeam))
                continue;

            pointsMap.insert(std::make_pair(guidP.GetRawValue(), guidP));

            for (auto tLoc : dests)
            {
                tLoc->AddPoint(&pointsMap.at(guidP.GetRawValue()));
            }
        }
    }

    sLog.outString("Loading Explore locations.");

    //Explore points
    for (auto& creatureDataPair : WorldPosition().getCreaturesNear())
    {
        ExploreTravelDestination* loc;

        AsyncGuidPosition point(creatureDataPair);

        if (!point.isValid())
            continue;

        AreaTableEntry const* area = point.GetArea();
       
        if (!area)
            continue;

        if (!area->exploreFlag)
            continue;

        point.FetchArea();

        pointsMap.insert_or_assign(point.GetRawValue(), point);

        loc = AddDestination<ExploreTravelDestination>(area->ID, TravelDestinationPurpose::Explore);
        loc->AddPoint(&pointsMap.at(point.GetRawValue()));
    }

    GetPopulatedGrids();

    //Analyse log files
    if (sPlayerbotAIConfig.hasLog("log_analysis.csv"))
    {
        sLog.outString("Running analysis.");
        LogAnalysis::RunAnalysis();
    }

    sLog.outString("Clearing log files.");

     //Clear these logs files
    sPlayerbotAIConfig.openLog("zones.csv", "w");
    sPlayerbotAIConfig.openLog("creatures.csv", "w");
    sPlayerbotAIConfig.openLog("gos.csv", "w");
    sPlayerbotAIConfig.openLog("bot_movement.csv", "w");
    sPlayerbotAIConfig.openLog("bot_pathfinding.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_attempt.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_attempt_point.csv", "w");
    sPlayerbotAIConfig.openLog("pathfind_result.csv", "w");
    sPlayerbotAIConfig.openLog("load_map_grid.csv", "w");
    sPlayerbotAIConfig.openLog("strategy.csv", "w");

    sPlayerbotAIConfig.openLog("unload_grid.csv", "w");
    sPlayerbotAIConfig.openLog("unload_obj.csv", "w");
    sPlayerbotAIConfig.openLog("bot_events.csv", "w");
    sPlayerbotAIConfig.openLog("travel_map.csv", "w");
    sPlayerbotAIConfig.openLog("quest_map.csv", "w");
    sPlayerbotAIConfig.openLog("activity_pid.csv", "w");
    sPlayerbotAIConfig.openLog("deaths.csv", "w");
    sPlayerbotAIConfig.openLog("player_paths.csv", "w");
    sPlayerbotAIConfig.openLog("travel_destinations.csv", "w");
    sPlayerbotAIConfig.openLog("deadzone.csv", "w"); 
    sPlayerbotAIConfig.openLog("bot_test_results.log", "w", true);
    

    if (sPlayerbotAIConfig.hasLog("activity_pid.csv"))
    {
        std::ostringstream out;
        out << "Timestamp,";

        out << "sWorld.GetCurrentDiff(),";
        out << "sWorld.GetAverageDiff(),";
        out << "sWorld.GetMaxDiff(),";
        out << "virtualMemUsedByMe" << ",";
        out << "activityPercentage,";
        out << "activityPercentageMod,";
        out << "activeBots,";
        out << "playerBots.size(),";
        out << "totalLevel,";
        out << "avarageLevel1-9,";
        out << "avarageLevel10-19,";
        out << "avarageLevel20-29,";
        out << "avarageLevel30-39,";
        out << "avarageLevel40-49,";
        out << "avarageLevel50-59,";
#ifdef MANGOSBOT_ZERO
        out << "avarageLevel60,";
#else
        out << "avarageLevel60-69,";
#ifdef MANGOSBOT_ONE
        out << "avarageLevel70,";
#else
        out << "avarageLevel70-79,";
        out << "avarageLevel80,";
#endif
#endif

        out << "totalGold,";
        out << "avarageGold,";
        out << "totalavarageGearScore,";
        out << "avarageGearScore";

        sPlayerbotAIConfig.log("activity_pid.csv", out.str().c_str());
    }

    sLog.outString("Loading travel nodes.");

    sTravelNodeMap.loadNodeStore();

    sTravelNodeMap.generateAll();

    sTravelNodeMap.printMap();
    sTravelNodeMap.printNodeStore();
    sTravelNodeMap.saveNodeStore();

    LoadFishLocations();

    // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.*, default 0): boot only, after the fish locations.
    LoadDirectTargets(guidpMap);

    // twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder, default 0): boot only, after the travel nodes are final.
    ai::quest_band::LoadLadder();
   
    //Creature/gos/zone export.
    if (sPlayerbotAIConfig.hasLog("creatures.csv"))
    {
        sLog.outString("Create creature overlay exports.");

        for (auto& creaturePair : WorldPosition().getCreaturesNear())
        {
            CreatureData const cData = creaturePair->second;
            CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(cData.creature_id[0]);

            if (!cInfo)
                continue;

            WorldPosition point = WorldPosition(cData.position.mapid, cData.position.coord_x, cData.position.coord_y, cData.position.coord_z, cData.position.orientation);

            std::string name = cInfo->name;
            name.erase(remove(name.begin(), name.end(), ','), name.end());
            name.erase(remove(name.begin(), name.end(), '\"'), name.end());

            std::ostringstream out;
            out << name << ",";
            point.printWKT(out);
            out << cInfo->MaxLevel << ",";
            out << cInfo->Rank << ",";
            out << cInfo->Faction << ",";
            out << cInfo->NpcFlags << ",";
            out << point.getAreaName() << ",";
            out << std::fixed;

            sPlayerbotAIConfig.log("creatures.csv", out.str().c_str());
        }
    }

    if (sPlayerbotAIConfig.hasLog("vmangoslines.csv"))
    {

        uint32 mapId = 0;
        std::vector<WorldPosition> pos;

            static float const topNorthSouthLimit[] = {
                2032.048340f, -6927.750000f,
                1634.863403f, -6157.505371f,
                1109.519775f, -5181.036133f,
                1315.204712f, -4096.020508f,
                1073.089233f, -3372.571533f,
                 825.833191f, -3125.778809f,
                 657.343994f, -2314.813232f,
                 424.736145f, -1888.283691f,
                 744.395813f, -1647.935425f,
                1424.160645f,  -654.948181f,
                1447.065308f,  -169.751358f,
                1208.715454f,   189.748703f,
                1596.240356f,   998.616699f,
                1577.923706f,  1293.419922f,
                1458.520264f,  1727.373291f,
                1591.916138f,  3728.139404f
            };

            pos.clear();

# define my_sizeof(type) ((char *)(&type+1)-(char*)(&type))

            int size = my_sizeof(topNorthSouthLimit) / my_sizeof(topNorthSouthLimit[0]);

            for (int32 i = 0; i < size-1; i=i+2)
            {
                if (topNorthSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, topNorthSouthLimit[i], topNorthSouthLimit[i + 1], 0));
            }

            std::ostringstream out;
            out << "topNorthSouthLimit" << ",";
            WorldPosition().printWKT(pos,out,1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const ironforgeAreaSouthLimit[] = {
                -7491.33f,  3093.74f,
                -7472.04f,  -391.88f,
                -6366.68f,  -730.10f,
                -6063.96f, -1411.76f,
                -6087.62f, -2190.21f,
                -6349.54f, -2533.66f,
                -6308.63f, -3049.32f,
                -6107.82f, -3345.30f,
                -6008.49f, -3590.52f,
                -5989.37f, -4312.29f,
                -5806.26f, -5864.11f
            };

            pos.clear();

            size = my_sizeof(ironforgeAreaSouthLimit) / my_sizeof(ironforgeAreaSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (ironforgeAreaSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, ironforgeAreaSouthLimit[i], ironforgeAreaSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();
            
            out << "ironforgeAreaSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const stormwindAreaNorthLimit[] = {
                 -8004.25f,  3714.11f,
                 -8075.00f, -179.00f,
                 -8638.00f, 169.00f,
                 -9044.00f, 35.00f,
                 -9068.00f, -125.00f,
                 -9094.00f, -147.00f,
                 -9206.00f, -290.00f,
                 -9097.00f, -510.00f,
                 -8739.00f, -501.00f,
                 -8725.50f, -1618.45f,
                 -9810.40f, -1698.41f,
                -10049.60f, -1740.40f,
                -10670.61f, -1692.51f,
                -10908.48f, -1563.87f,
                -13006.40f, -1622.80f,
                -12863.23f, -4798.42f
            };

            pos.clear();

            size = my_sizeof(stormwindAreaNorthLimit) / my_sizeof(stormwindAreaNorthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (stormwindAreaNorthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, stormwindAreaNorthLimit[i], stormwindAreaNorthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "stormwindAreaNorthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const stormwindAreaSouthLimit[] = {
                 -8725.337891f,  3535.624023f,
                 -9525.699219f,   910.132568f,
                 -9796.953125f,   839.069580f,
                 -9946.341797f,   743.102844f,
                -10287.361328f,   760.076477f,
                -10083.828125f,   380.389893f,
                -10148.072266f,    80.056450f,
                -10014.583984f,  -161.638519f,
                 -9978.146484f,  -361.638031f,
                 -9877.489258f,  -563.304871f,
                 -9980.967773f, -1128.510498f,
                 -9991.717773f, -1428.793213f,
                 -9887.579102f, -1618.514038f,
                -10169.600586f, -1801.582031f,
                 -9966.274414f, -2227.197754f,
                 -9861.309570f, -2989.841064f,
                 -9944.026367f, -3205.886963f,
                 -9610.209961f, -3648.369385f,
                 -7949.329590f, -4081.389404f,
                 -7910.859375f, -5855.578125f
            };

            pos.clear();

            size = my_sizeof(stormwindAreaSouthLimit) / my_sizeof(stormwindAreaSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (stormwindAreaSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, stormwindAreaSouthLimit[i], stormwindAreaSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "stormwindAreaSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());
            
            mapId = 1;

            static float const teldrassilSouthLimit[] = {
            7916.0f,   3475.0f,
            7916.0f,   1000.0f,
            8283.0f,   -501.0f,
            8804.0f,   -1098.0f
            };

            pos.clear();

            size = my_sizeof(teldrassilSouthLimit) / my_sizeof(teldrassilSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (teldrassilSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, teldrassilSouthLimit[i], teldrassilSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "teldrassilSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());
       
            static float const northMiddleLimit[] = {
                  -2280.00f,  4054.00f,
                  -2401.00f,  2365.00f,
                  -2432.00f,  1338.00f,
                  -2286.00f,   769.00f,
                  -2137.00f,   662.00f,
                  -2044.54f,   489.86f,
                  -1808.52f,   436.39f,
                  -1754.85f,   504.55f,
                  -1094.55f,   651.75f,
                   -747.46f,   647.73f,
                   -685.55f,   408.43f,
                   -311.38f,   114.43f,
                   -358.40f,  -587.42f,
                   -377.92f,  -748.70f,
                   -512.57f,  -919.49f,
                   -280.65f, -1008.87f,
                    -81.29f,  -930.89f,
                    284.31f, -1105.39f,
                    568.86f,  -892.28f,
                   1211.09f, -1135.55f,
                    879.60f, -2110.18f,
                    788.96f, -2276.02f,
                    899.68f, -2625.56f,
                   1281.54f, -2689.42f,
                   1521.82f, -3047.85f,
                   1424.22f, -3365.69f,
                   1694.11f, -3615.20f,
                   2373.78f, -4019.96f,
                   2388.13f, -5124.35f,
                   2193.79f, -5484.38f,
                   1703.57f, -5510.53f,
                   1497.59f, -6376.56f,
                   1368.00f, -8530.00f
            };

            pos.clear();

            size = my_sizeof(northMiddleLimit) / my_sizeof(northMiddleLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (northMiddleLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, northMiddleLimit[i], northMiddleLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "northMiddleLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const durotarSouthLimit[] = {
                    2755.00f, -3766.00f,
                    2225.00f, -3596.00f,
                    1762.00f, -3746.00f,
                    1564.00f, -3943.00f,
                    1184.00f, -3915.00f,
                     737.00f, -3782.00f,
                     -75.00f, -3742.00f,
                    -263.00f, -3836.00f,
                    -173.00f, -4064.00f,
                     -81.00f, -4091.00f,
                     -49.00f, -4089.00f,
                     -16.00f, -4187.00f,
                      -5.00f, -4192.00f,
                     -14.00f, -4551.00f,
                    -397.00f, -4601.00f,
                    -522.00f, -4583.00f,
                    -668.00f, -4539.00f,
                    -790.00f, -4502.00f,
                   -1176.00f, -4213.00f,
                   -1387.00f, -4674.00f,
                   -2243.00f, -6046.00f
            };

            pos.clear();

            size = my_sizeof(durotarSouthLimit) / my_sizeof(durotarSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (durotarSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, durotarSouthLimit[i], durotarSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "durotarSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const valleyoftrialsSouthLimit[] = {
                    -324.00f, -3869.00f,
                    -774.00f, -3992.00f,
                    -965.00f, -4290.00f,
                    -932.00f, -4349.00f,
                    -828.00f, -4414.00f,
                    -661.00f, -4541.00f,
                    -521.00f, -4582.00f
            };

            pos.clear();

            size = my_sizeof(valleyoftrialsSouthLimit) / my_sizeof(valleyoftrialsSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (valleyoftrialsSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, valleyoftrialsSouthLimit[i], valleyoftrialsSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "valleyoftrialsSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const middleToSouthLimit[] = {
                        -2402.01f,      4255.70f,
                    -2475.933105f,  3199.568359f, // Desolace
                    -2344.124023f,  1756.164307f,
                    -2826.438965f,   403.824738f, // Mulgore
                    -3472.819580f,   182.522476f, // Feralas
                    -4365.006836f, -1602.575439f, // the Barrens
                    -4515.219727f, -1681.356079f,
                    -4543.093750f, -1882.869385f, // Thousand Needles
                        -4824.16f,     -2310.11f,
                    -5102.913574f, -2647.062744f,
                    -5248.286621f, -3034.536377f,
                    -5246.920898f, -3339.139893f,
                    -5459.449707f, -4920.155273f, // Tanaris
                        -5437.00f,     -5863.00f
            };

            pos.clear();

            size = my_sizeof(middleToSouthLimit) / my_sizeof(middleToSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (middleToSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, middleToSouthLimit[i], middleToSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "middleToSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const orgrimmarSouthLimit[] = {
                    2132.5076f, -3912.2478f,
                    1944.4298f, -3855.2583f,
                    1735.6906f, -3834.2417f,
                    1654.3671f, -3380.9902f,
                    1593.9861f, -3975.5413f,
                    1439.2548f, -4249.6923f,
                    1436.3106f, -4007.8950f,
                    1393.3199f, -4196.0625f,
                    1445.2428f, -4373.9052f,
                    1407.2349f, -4429.4145f,
                    1464.7142f, -4545.2875f,
                    1584.1331f, -4596.8764f,
                    1716.8065f, -4601.1323f,
                    1875.8312f, -4788.7187f,
                    1979.7647f, -4883.4585f,
                    2219.1562f, -4854.3330f
            };

            pos.clear();

            size = my_sizeof(orgrimmarSouthLimit) / my_sizeof(orgrimmarSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (orgrimmarSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, orgrimmarSouthLimit[i], orgrimmarSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "orgrimmarSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const feralasThousandNeedlesSouthLimit[] = {
                    -6495.4995f, -4711.981f,
                    -6674.9995f, -4515.0019f,
                    -6769.5717f, -4122.4272f,
                    -6838.2651f, -3874.2792f,
                    -6851.1314f, -3659.1179f,
                    -6624.6845f, -3063.3843f,
                    -6416.9067f, -2570.1301f,
                    -5959.8466f, -2287.2634f,
                    -5947.9135f, -1866.5028f,
                    -5947.9135f,  -820.4881f,
                    -5876.7114f,    -3.5138f,
                    -5876.7114f,   917.6407f,
                    -6099.3603f,  1153.2884f,
                    -6021.8989f,  1638.1809f,
                    -6091.6176f,  2335.8892f,
                    -6744.9946f,  2393.4855f,
                    -6973.8608f,  3077.0281f,
                    -7068.7241f,  4376.2304f,
                    -7142.1211f,  4808.4331f
            };


            pos.clear();

            size = my_sizeof(feralasThousandNeedlesSouthLimit) / my_sizeof(feralasThousandNeedlesSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (feralasThousandNeedlesSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, feralasThousandNeedlesSouthLimit[i], feralasThousandNeedlesSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "feralasThousandNeedlesSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            mapId = 530;

            static float const ShattrathAreaSouthLimit[] = {
            -2493.8823f,  5761.6894f,
            -2593.7438f,  4768.7978f,
            -1831.5280f,  3383.5705f
            };

            pos.clear();

            size = my_sizeof(ShattrathAreaSouthLimit) / my_sizeof(ShattrathAreaSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (ShattrathAreaSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, ShattrathAreaSouthLimit[i], ShattrathAreaSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "ShattrathAreaSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const HellfireZangarSouthLimit[] = {

            -531.47265f,  8697.5830f,
            -514.56945f,  7291.2763f,
            -404.92804f,  6976.7958f,
            -593.56475f,  6646.0634f,
            -856.75695f,  6318.5507f,
            -1166.2729f,  5799.7817f,
            -1007.9321f,  4761.1352f,
            -1831.5280f,  3383.5705f,
            -2135.1586f,  2335.4426f,
            -2179.3974f,  896.0285f,
            };

            pos.clear();

            size = my_sizeof(HellfireZangarSouthLimit) / my_sizeof(HellfireZangarSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (HellfireZangarSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, HellfireZangarSouthLimit[i], HellfireZangarSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "HellfireZangarSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());

            static float const BladeEdgeNetherSouthLimit[] = {
            2074.6831f,  8216.6113f,
            1248.3884f,  7472.7592f,
            1118.4877f,  6972.6821f,
            1212.2004f,  6106.2861f,
            1175.4729f,  5633.375f,
            1543.8314f,  3961.8886f,
            };

            pos.clear();

            size = my_sizeof(BladeEdgeNetherSouthLimit) / my_sizeof(BladeEdgeNetherSouthLimit[0]);

            for (int32 i = 0; i < size - 1; i = i + 2)
            {
                if (BladeEdgeNetherSouthLimit[i] == 0)
                    break;
                pos.push_back(WorldPosition(mapId, BladeEdgeNetherSouthLimit[i], BladeEdgeNetherSouthLimit[i + 1], 0));
            }

            out.str("");
            out.clear();

            out << "BladeEdgeNetherSouthLimit" << ",";
            WorldPosition().printWKT(pos, out, 1);
            out << std::fixed;

            sPlayerbotAIConfig.log("vmangoslines.csv", out.str().c_str());
    }

    if (sPlayerbotAIConfig.hasLog("gos.csv"))
    {
        sLog.outString("Create go overlay exports.");
        for (auto& gameObjectPair : WorldPosition().getGameObjectsNear())
        {
            GameObjectData const gData = gameObjectPair->second;
            auto data = sGOStorage.LookupEntry<GameObjectInfo>(gData.id);

            if (!data)
                continue;

            WorldPosition point = WorldPosition(gData.position.mapid, gData.position.coord_x, gData.position.coord_y, gData.position.coord_z, gData.position.orientation);

            std::string name = data->name;
            name.erase(remove(name.begin(), name.end(), ','), name.end());
            name.erase(remove(name.begin(), name.end(), '\"'), name.end());

            std::ostringstream out;
            out << name << ",";
            point.printWKT(out);
            out << data->type << ",";
            out << point.getAreaName() << ",";
            out << std::fixed;

            sPlayerbotAIConfig.log("gos.csv", out.str().c_str());
        }
    }

    if (sPlayerbotAIConfig.hasLog("zones.csv"))
    {
        sLog.outString("Create zone overlay exports.");

        std::unordered_map<std::string, std::vector<WorldPosition>> zoneLocs;

        std::vector<WorldPosition> Locs = {};
        
        for (auto& cdp : WorldPosition().getCreaturesNear())
        {
            WorldPosition point(cdp);
            std::string name = std::to_string(point.getMapId()) + point.getAreaName();

            if (zoneLocs.find(name) == zoneLocs.end())
                zoneLocs.insert_or_assign(name, Locs);

            zoneLocs.at(name).push_back(point);            
        }        

        for (auto& loc : zoneLocs)
        {
            if (loc.second.empty())
                continue;

            if (!sTravelNodeMap.getMapOffset(loc.second.front().getMapId()) && loc.second.front().getMapId() != 0)
                continue;

            std::vector<WorldPosition> points = loc.second;;
           
            std::ostringstream out; 

            WorldPosition pos = WorldPosition(points, WP_MEAN_CENTROID);

            out << "\"center\"" << ",";
            out << points.begin()->getMapId() << ",";
            out << points.begin()->getAreaName() << ",";
            out << points.begin()->getAreaName(true, true) << ",";

            pos.printWKT(out);

            if(points.begin()->GetArea())
                out << std::to_string(points.begin()->getAreaLevel());
            else
                out << std::to_string(-1);

            out << "\n";
            
            out << "\"area\"" << ",";
            out << points.begin()->getMapId() << ",";
            out << points.begin()->getAreaName() << ",";
            out << points.begin()->getAreaName(true, true) << ",";

            WorldPosition().printWKT(points, out, 0);

            if (points.begin()->GetArea())
                out << std::to_string(points.begin()->getAreaLevel());
            else
                out << std::to_string(-1);

            sPlayerbotAIConfig.log("zones.csv", out.str().c_str());
        }
    }    

    if (sPlayerbotAIConfig.hasLog("telecache.csv"))
    {
        sLog.outString("Create telecache overlay exports.");

        sRandomPlayerbotMgr.PrintTeleportCache();
    }

    if (sPlayerbotAIConfig.hasLog("travel_destinations.csv"))
    {
        sLog.outString("Create travel destinations export.");

        for (auto& [purpose, entryDestinations] : destinationMap)
        {
            for (auto& [entry, destinations] : entryDestinations)
            {
                for (auto& destination : destinations)
                {
                    std::ostringstream out;

                    out << TravelDestinationPurposeName.at(purpose) << ",";
                    out << entry << ",";
                    out << destination->GetShortName() << ",";
                    out << "\"" << destination->GetTitle() << "\",";
                    out << destination->GetSize() << ",";
                    out << "\"MultiPoint(";
                    destination->printWKT(out, false);
                    out.seekp(-1, out.cur);
                    out << ")\"";
                    sPlayerbotAIConfig.log("travel_destinations.csv", out.str().c_str());
                }
            }
        }
    }

    LogShimBounds(guidpMap);  // twow-repo#510

#ifndef MANGOSBOT_TWO    
    sTerrainMgr.Update(60 * 60 * 24);
#else
    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->MapID;

        if (WorldPosition(mapId, 0, 0).getMap(0))
            continue;

        WorldPosition::unloadMapAndVMaps(mapId);
    }
#endif     
}

void TravelMgr::GetPopulatedGrids()
{
    sLog.outString("Finding populated grids.");

    BarGoLink bar(sMapStore.GetNumRows());      

    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        bar.step();

        if (!sMapStore.LookupEntry(i))
            continue;

        uint32 mapId = sMapStore.LookupEntry(i)->MapID;

        GetPopulatedGrids(mapId);
    }
}

void TravelMgr::GetPopulatedGrids(uint32 mapId)
{
    EntryGuidps guidpMap = GAI_VALUE(EntryGuidps, "entry guidps");

    for (auto& [entry, guidPs] : guidpMap)
    {
        for (auto& guidP : guidPs)
        {
            if (guidP.getMapId() == mapId)
            {
                populatedGrids[guidP.getMapId()][guidP.getGridPair().x_coord][guidP.getGridPair().y_coord] = true;
            }
        }
    }
}

uint32 TravelMgr::GetFishZone(const AsyncGuidPosition& pos) const
{
    uint32 zone, subzone;
    int32 areaFlag = pos.getAreaFlag();

    TerrainManager::GetZoneAndAreaIdByAreaFlag(zone, subzone, areaFlag, pos.getMapId());

    if (sObjectMgr.GetFishingBaseSkillLevel(subzone))
        return subzone;

    if (sObjectMgr.GetFishingBaseSkillLevel(zone))
        return zone;

    return 0;
}

void TravelMgr::LoadFishLocations()
{
    sLog.outString("Loading Fish locations.");

    fishPoints.clear();
    fishMap.clear();
    destinationMap[TravelDestinationPurpose::GatherFishing].clear();

    auto result = WorldDatabase.Query("SELECT `name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `description` FROM `ai_playerbot_named_location` WHERE `name` LIKE 'FISH_LOCATION%'");

    if (!result)
    {
        sTravelNodeMap.setHasToGen();
        sTravelNodeMap.setGeneratorRunning(true);   // twow-repo#592: route steps may be rebuilt (TravelNodeRoute::buildPath)
        GetFishLocations();
        sTravelNodeMap.setGeneratorRunning(false);
        sTravelNodeMap.setHasToGen(false);
        SaveFishLocations();
        return;
    }

    uint32 count = 0;
    do
    {
        ++count;

        Field* fields = result->Fetch();

        std::string name = fields[0].GetCppString();
        uint32 mapId = fields[1].GetUInt32();
        float positionX = fields[2].GetFloat();
        float positionY = fields[3].GetFloat();
        float positionZ = fields[4].GetFloat();
        float orientation = fields[5].GetFloat();
        std::string description = fields[6].GetCppString();

        AsyncGuidPosition* point = &fishPoints.emplace_back(GuidPosition(0, WorldPosition(mapId, positionX, positionY, positionZ, orientation)));

        int32 areaFlag = stoi(description);
        point->setAreaFlag(areaFlag);
        uint32 zone = GetFishZone(*point);

        if (!zone)
            continue;

        TravelDestination* dest = AddDestination<GatherTravelDestination>(zone, TravelDestinationPurpose::GatherFishing);                

        dest->AddPoint(point);
        fishMap.AddPoint(point);

    } while (result->NextRow());

    sLog.outString(">> Loaded %u fish locations", count);
    sLog.outString();
}

void TravelMgr::GetFishLocations()
{
    sLog.outString("Generating Fish locations.");

    // twow-repo#592 (PATH-02, AiPlayerbot.Perf.PathGenUnitless): the node route part of the jobs (getRoute, getNodes,
    // getFullPath - unitless pathfinding, node graph writes, no m_nMapMtx) only runs with the unitless generation gate
    // open, and then serially; with the switch on there is one job per map in this thread instead of one std::async per
    // map. Gate closed: candidates that would need a route are dropped and counted ([PathGen] skipped reason=fish).
    char const* const routeSkipCause = sTravelNodeMap.unitlessGenSkipCause();
    bool const routeAllowed = !routeSkipCause;
    bool const serial = sPlayerbotAIConfig.perfPathGenUnitless;

    BarGoLink bar(1000);

    std::vector<std::future<void>> calculations;

    for (int32 mapId = 1000; mapId >= 0; mapId--)
    {       
        bool hashFishing = false;
        for (uint32 i = 0; i < sAreaStore.GetNumRows(); ++i)    // areaflag numbered from 0
        {
            AreaTableEntry const* area = sAreaStore.LookupEntry<AreaEntry>(i);

            if (!area)
                continue;

            if (area->MapId != mapId)
                continue;

            if (!sObjectMgr.GetFishingBaseSkillLevel(area->ID))
                continue;

            hashFishing = true;
            break;
        }

        if (!hashFishing)
            continue;

        if (serial)
            GetFishLocations(mapId, routeAllowed);
        else
            calculations.push_back(std::async([this, mapId, routeAllowed] { GetFishLocations(mapId, routeAllowed); }));

        bar.step();
    }

    BarGoLink bar1(calculations.size());   

    for (uint32 i = 0; i < calculations.size(); i++)
    {
        calculations[i].wait();
        bar1.step();
    }

    if (uint64 const skipped = ai::path_unitless_bot::fishSkipped.exchange(0, std::memory_order_relaxed))
        sLog.outString("[PathGen] skipped reason=fish n=%llu cause=%s", (unsigned long long)skipped, routeSkipCause ? routeSkipCause : "none");
}

void TravelMgr::GetFishLocations(uint32 mapId, bool routeAllowed)
{
    WorldPosition ironForge(0, -4832.27, -1069.64, 502.268);
    TravelNode* ironForgeNode = sTravelNodeMap.getNode(ironForge);
    WorldPosition orgrimmar(1, 1845.49, -4395.95, 5.19264);

    PathFinder path(mapId,0);

    const int8 subCellPerGrid = 64;
    const float sizeOfSubCell = SIZE_OF_GRIDS / subCellPerGrid;

    std::unordered_map<uint32, std::vector<AsyncGuidPosition>> fishSpots;

    for (uint8 gridx = 0; gridx < 64; ++gridx)
    {
        for (uint8 gridy = 0; gridy < 64; ++gridy)
        {
            if (!populatedGrids[mapId][gridx][gridy])
                continue;

            GridPair grid(gridx, gridy);

            WorldPosition gridPos(mapId, grid);

            for (uint8 cellx = 0; cellx < subCellPerGrid; cellx++)
            {
                for (uint8 celly = 0; celly < subCellPerGrid; celly++)
                {
                    //Get spot in water.
                    WorldPosition waterSpot = gridPos + WorldPosition(mapId, sizeOfSubCell * cellx, sizeOfSubCell * celly);

                    waterSpot.setZ(waterSpot.getHeight(true));

                    if (!waterSpot.isInWater())
                        continue;

                    //Get nearby location on ground.
                    WorldPosition fishSpot;
                    float botAngle = urand(0, 360);
                    bool inWater = true;
                    for (float angle = 0.0f; angle < 360.0f; angle += 45.0f)
                    {
                        fishSpot = waterSpot + WorldPosition(mapId, cos((angle + botAngle) / 180.0f * M_PI_F) * 10, sin((angle + botAngle) / 180.0f * M_PI_F) * 10);

                        fishSpot.setZ(fishSpot.getHeight(false));

                        if (!fishSpot.getTerrain())
                            continue;

                        if (fabs(fishSpot.getZ() - waterSpot.getZ()) > 10.0f)
                            continue;

                        uint32 area = path.getArea(mapId, fishSpot.getX(), fishSpot.getY(), fishSpot.getZ());

                        if (!area || area == NAV_AREA_GROUND_STEEP)
                        {
                        //    sLog.outError("no area %d %f %f %f", mapId, fishSpot.getX(), fishSpot.getY(), fishSpot.getZ());
                            continue;
                        }
                   
                        inWater = fishSpot.isInWater();

                        if (!inWater)
                            break;
                    }

                    if (inWater)
                        continue;

                    AsyncGuidPosition fishPos(GuidPosition(0, fishSpot));
                    fishPos.setO(fishPos.getAngleTo(waterSpot));

                    fishPos.FetchArea();
                    uint32 zone = GetFishZone(fishPos);

                    if (!zone)
                        continue;

                    // twow-repo#592 (PATH-02): the route part below runs only with the unitless generation gate open
                    // (see GetFishLocations()); else the candidate is dropped and counted. Unreachable today:
                    // PathInfo::getArea is a stub returning 0, so every candidate already left at "if (!area ...)".
                    if (!routeAllowed)
                    {
                        ai::path_unitless_bot::fishSkipped.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    std::vector<WorldPosition> startPath;
                    std::vector<WorldPosition> endPath;

                    if (fishPos.getMapId() == 0 && sTravelNodeMap.getRoute(ironForge, fishPos, startPath, endPath, nullptr).isEmpty())
                        continue;

                    if (fishPos.getMapId() == 1 && sTravelNodeMap.getRoute(orgrimmar, fishPos, startPath, endPath, nullptr).isEmpty())
                        continue;

                    if (fishPos.getMapId() > 1)
                    {
                        bool noPath = true;
                        for (auto& node : sTravelNodeMap.getNodes(fishPos))
                        {
                            if (!node->hasRouteTo(ironForgeNode))
                                continue;

                            if (!sTravelNodeMap.getFullPath(*node->getPosition(), fishPos).empty())
                            {
                                noPath = false;
                                break;
                            }
                        }

                        if (noPath)
                            continue;
                    }

                    fishSpots[zone].push_back(fishPos);                    
                }
            }
        }
    }

    getDestinationMutex.lock();
    for (auto& [zone, spots] : fishSpots)
    {
        TravelDestination* dest = AddDestination<GatherTravelDestination>(zone, TravelDestinationPurpose::GatherFishing);

        for (auto& spot : spots)
        {
            WorldPosition* point = &fishPoints.emplace_back(spot);
            dest->AddPoint(point);
            fishMap.AddPoint(point);
        }
    }
    getDestinationMutex.unlock();
}

void TravelMgr::SaveFishLocations()
{
    sLog.outString("Saving Fish locations.");

    //Save fishpoints sorted by mapId, areaFlag, X, Y.
    fishPoints.sort([](AsyncGuidPosition i, AsyncGuidPosition j) {
        if (i.getMapId() != j.getMapId()) return i.getMapId() > j.getMapId();
        if (i.getAreaFlag() != j.getAreaFlag()) return i.getAreaFlag() > j.getAreaFlag();
        if (i.getX() != j.getX()) return i.getX() > j.getX();
        return i.getY() > j.getY(); });

    WorldDatabase.BeginTransaction();

    BarGoLink bar(fishPoints.size());

    uint32 count = 0;    

    for (auto& fishPoint : fishPoints)
    {
        std::string name = "FISH_LOCATION_" + std::to_string(fishPoint.getMapId()) + "_" + std::to_string(fishPoint.getAreaFlag()) + "_" + std::to_string(count);
        std::string description = std::to_string(fishPoint.getAreaFlag());
        name.erase(remove(name.begin(), name.end(), '\''), name.end());

        WorldDatabase.PExecute("INSERT INTO `ai_playerbot_named_location` (`name`, `map_id`, `position_x`, `position_y`, `position_z`, `orientation`, `description`) VALUES ('%s', '%d', '%f', '%f', '%f', '%f', '%s')"
            , name.c_str(), fishPoint.getMapId(), fishPoint.getX(), fishPoint.getY(), fishPoint.getZ(), fishPoint.getO(), description.c_str());

        bar.step();
        count++;
    }

    WorldDatabase.CommitTransaction();
}

WorldPosition* TravelMgr::GetFishSpot(WorldPosition start, bool onlyNearestGrid)
{
    //0% Different map, 25% different grid, 25% different cell, 100% different point (inside cell)
    std::list<uint8> chances = { 0, 25, 25, 100 };

    if (onlyNearestGrid)
        chances = { 0, 0, 0, 100 };

    return fishMap.GetNextPoint(start, chances);
}

DestinationList TravelMgr::GetDestinations(const PlayerTravelInfo& info, uint32 purposeFlag, const std::vector<int32>& entries, bool onlyPossible, float maxDistance) const
{
    WorldPosition center = info.GetPosition();
    DestinationList retDests;

    // twow-repo#541 (audit A10, AiPlayerbot.Perf.TravelInfoReuse): the inner map is keyed by quest id (or entry).
    // Exactly one asked entry is looked up instead of walking every key, and DistanceTo is computed once per
    // destination. Same list in the same order; several entries keep the key walk (map order).
    bool const travelInfoReuse = sPlayerbotAIConfig.perfTravelInfoReuse;
    auto const appendDestinations = [&](DestinationList const& dests)
    {
        for (TravelDestination* dest : dests)
        {
            // twow-repo#575 (PATH-01 E5): a start-zone destination only for its own faction (tagged at boot when on).
            if (!ai::start_zone::DestinationAllows(ai::start_zone::StartTeam(dest->GetStartZoneTeam()), info.GetTeam() == ALLIANCE))
                continue;

            if (onlyPossible && !dest->IsPossible(info))
                continue;

            float const distance = dest->DistanceTo(center);

            if (maxDistance > 0 && distance > maxDistance)
                continue;

            if (distance == FLT_MAX) //Do not return destinations on maps you can't path to.
                continue;

            retDests.push_back(dest);
        }
    };

    for (auto& [purpose, entryDests] : destinationMap)
    {

        if (purposeFlag != (uint32)TravelDestinationPurpose::None && !((uint32)purpose & (uint32)purposeFlag))
            continue;

        if (travelInfoReuse)
        {
            if (entries.size() == 1)
            {
                auto const found = entryDests.find(entries.front());
                if (found != entryDests.end())
                    appendDestinations(found->second);
            }
            else
            {
                for (auto const& [destEntry, dests] : entryDests)
                    if (entries.empty() || std::find(entries.begin(), entries.end(), destEntry) != entries.end())
                        appendDestinations(dests);
            }
            continue;
        }

        for (auto& [destEntry, dests] : entryDests)
        {
            if (entries.size() && std::find(entries.begin(), entries.end(), destEntry) == entries.end())
                continue;

            for (auto& dest : dests)
            {
                if (!ai::start_zone::DestinationAllows(ai::start_zone::StartTeam(dest->GetStartZoneTeam()), info.GetTeam() == ALLIANCE))
                    continue;

                if (onlyPossible && !dest->IsPossible(info))
                    continue;

                if (maxDistance > 0 && dest->DistanceTo(center) > maxDistance)
                    continue;

                if (dest->DistanceTo(center) == FLT_MAX) //Do not return destinations on maps you can't path to.
                    continue;
                
                retDests.push_back(dest);
            }
        }
    }

    return retDests;
}

void TravelMgr::GetPartitionsLock(bool getLock)
{
    std::unique_lock<std::mutex> lock(sTravelMgr.getDestinationMutex);
    if (getLock)
    {
        sTravelMgr.getDestinationVar.wait(lock, [&] { return sTravelMgr.availableDestinationWorkers; });
        sTravelMgr.availableDestinationWorkers--;

        return;
    }
    
    sTravelMgr.availableDestinationWorkers++;
    sTravelMgr.getDestinationVar.notify_one();
}

bool TravelMgr::IsLocationLevelValid(const WorldPosition& position, const PlayerTravelInfo& info, uint32 purposeFlag, bool gapWindowApplies)
{
    bool canFightElite = info.GetBoolValue("can fight elite");
    int32 botLevel = (int32)info.GetLevel();

    if (position.getMapId() == 530 && info.GetLevel() < 58) //Outland
        return false;

    if (position.getMapId() == 571 && info.GetLevel() < 68) //Northrend
        return false;

    // #333 (owner test video): gathering is not a fight. Raid bots of a real
    // player counted as "can fight elite/boss" below and were sent to fish in
    // Maraudon and to skin in Blackrock Depths at level 6-11. Gathering points
    // are open world only and compare the plain bot level with the area level
    // (a sub-area without a level of its own takes its parent zone's).
    uint32 const gatherPurposes = (uint32)TravelDestinationPurpose::GatherSkinning | (uint32)TravelDestinationPurpose::GatherMining |
        (uint32)TravelDestinationPurpose::GatherHerbalism | (uint32)TravelDestinationPurpose::GatherFishing;
    if ((purposeFlag & gatherPurposes) && !(purposeFlag & ~gatherPurposes))
    {
        if (!position.isOverworld())
            return false;
        int32 const gatherAreaLevel = position.getAreaLevelOrParent();
        return gatherAreaLevel > 0 && !homebind::IsZoneClearlyAboveLevel(uint32(gatherAreaLevel), info.GetLevel());
    }

    // twow-repo#594 / P3-b (AiPlayerbot.DirectTargets.*): a station is not a fight either - open world, an area level,
    // not clearly above the bot (as gathering). An entrance point is in the open world; its level fit is the
    // destination's (IsPossible). These bits exist only with a class built at boot.
    uint32 const directPurposes = (uint32)TravelDestinationPurpose::Station | (uint32)TravelDestinationPurpose::DungeonEntrance |
        (uint32)TravelDestinationPurpose::RaidEntrance;
    if ((purposeFlag & directPurposes) && !(purposeFlag & ~directPurposes))
    {
        if (!position.isOverworld())
            return false;
        if (!(purposeFlag & (uint32)TravelDestinationPurpose::Station))
            return true;
        int32 const stationAreaLevel = position.getAreaLevelOrParent();
        return stationAreaLevel > 0 && !homebind::IsZoneClearlyAboveLevel(uint32(stationAreaLevel), info.GetLevel());
    }

    if (info.GetBoolValue("can fight boss"))
        botLevel += 5;
    else if (canFightElite)
        botLevel += 2;
    else if (!info.GetBoolValue("can fight equal"))
    {
        botLevel -= (int32)(2 + info.GetUint32Value("death count"));
    }

    if (botLevel < 1)
        botLevel = 1;

    uint32 areaLevel = position.getAreaLevel();

    if (!position.isOverworld() && !canFightElite)
        areaLevel += 10;

    // Handing a quest in is not a difficulty decision. The bot has already done
    // the work; whether the giver happens to stand in a neighbourhood rated above
    // its level says nothing about whether it should walk back. And the gate was
    // firing for a reason that has nothing to do with level at all: getAreaFlag
    // returns 0 whenever the vmap for that spot is not loaded, which inside the
    // asynchronous destination search is most of the time, and an area level of 0
    // was read here as "too high for you". Measured on a live realm before the
    // fix: of 350 rejected quest-taker points, 299 had no resolvable area
    // whatsoever.
    //
    // Elite and dungeon turn-ins are still held back by
    // QuestRelationTravelDestination::IsPossible, which is where that belongs.

    // #335: quest givers and objectives are not grind targets. The grind gate
    // below (level - 2 - deaths, minimum 1) refused a level 1 bot that had died
    // once every quest point around Dolanaar (area level 5), so seven roster
    // bots stayed at level 1 with no quest route at all. For a destination that
    // is only about quests, "clearly above the bot" is the limit, the same rule
    // as the route danger; the quest level itself was checked on acceptance.
    uint32 const questPurposes = (uint32)TravelDestinationPurpose::QuestGiver | (uint32)TravelDestinationPurpose::QuestAllObjective;
    if ((purposeFlag & questPurposes) && !(purposeFlag & ~(questPurposes | (uint32)TravelDestinationPurpose::QuestTaker)) &&
        sPlayerbotAIConfig.questFirstProgressionQuestAreaLevelMargin >= 0)
    {
        int32 questAreaLevel = position.getAreaLevel();
        if (questAreaLevel > 0 && !position.isOverworld() && !canFightElite)
            questAreaLevel += 10;

        return quest_area_level::IsQuestLocationLevelValid(questAreaLevel, info.GetLevel(),
            sPlayerbotAIConfig.questFirstProgressionQuestAreaLevelMargin);
    }

    // twow-repo#601 S2 R3 (GrindMode 1, OB-10 rev. 2 point 1): a pure grind point of the gap filler's population - its area
    // level inside the same window [max(gray + 1, L - 5), L + 1] as the template (IsPossible) replaces the area gate (L - 2 -
    // deaths) and the zone floor below, so the two cannot disagree; an unknown area (0) stays rejected. The window is a
    // value of the snapshot (bot thread); every other bot and purpose: unchanged. gapWindowApplies false: a fetch without
    // IsPossible (guild order objective / kill mobs, GetPartitions onlyPossible false) keeps the old gate, as it keeps the
    // old template test. OB-10 review #552: the death term stays - at the window's upper end, never below its low end
    // (GapAreaHighLevel), for the bots the old gate applied it to (no boss / elite / equal fight); the mob window (R4) is
    // unchanged.
    if (info.HasGapWindow() && gapWindowApplies && purposeFlag == (uint32)TravelDestinationPurpose::Grind)
    {
        uint32 const gapDeaths = (info.GetBoolValue("can fight boss") || canFightElite || info.GetBoolValue("can fight equal")) ? 0 :
            info.GetUint32Value("death count");
        return ai::quest_grind_mode::InGapWindow(info.GetGapWindowLow(),
            ai::quest_grind_mode::GapAreaHighLevel(info.GetGapWindowLow(), info.GetGapWindowHigh(), gapDeaths), areaLevel);
    }

    if (!(purposeFlag & (uint32)TravelDestinationPurpose::QuestTaker))
    {
        if (!areaLevel || (uint32)botLevel < areaLevel) //Skip points that are in a area that is too high level.
            return false;
    }

    // Grind-specific: don't grind in a zone whose overall level is at/below the bot's
    // own floor for acceptable mobs - mirrors the per-mob minLevel window already used
    // in GrindTravelDestination::IsPossible(), applied to the zone as a whole so a
    // starting zone's own top-tier mobs can't keep a wildly over-leveled bot latched
    // there with no reason to ever travel farther. Scoped to Grind only (not Rpg/Quest/
    // Gather/Explore) so vendor/quest-turn-in/gather trips to a bot's home zone are
    // unaffected.
    if (purposeFlag & (uint32)TravelDestinationPurpose::Grind)
    {
        int32 rawLevel = (int32)info.GetLevel();
        uint8 botPowerLevel = info.GetUint8Value("durability");
        float levelMod = botPowerLevel / 500.0f;
        float levelBoost = botPowerLevel / 50.0f;
        int32 grindMinLevel = std::max(rawLevel * (0.4f + levelMod), rawLevel - 12.0f + levelBoost);
        if ((int32)areaLevel <= grindMinLevel)
            return false;
    }

    return true;
}

PartitionedTravelList TravelMgr::GetPartitions(const WorldPosition& center, const std::vector<uint32>& distancePartitions, const PlayerTravelInfo& info, uint32 purposeFlag, const std::vector<int32>& entries, bool onlyPossible, float maxDistance) const
{
    sTravelMgr.GetPartitionsLock();

    PartitionedTravelList pointMap;
    DestinationList destinations = GetDestinations(info, purposeFlag, entries, onlyPossible, maxDistance);



    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(destinations.begin(), destinations.end(), std::default_random_engine(seed));

    // TEMPORARY counters. Quest takers are offered to a bot and nothing comes
    // back: 79 destinations across ten bots in 42 minutes produced two journeys,
    // and every failure ended with an empty list. Four things here can discard a
    // destination and from outside they are indistinguishable. Remove once the
    // answer is in.
    uint32 probeTotal = destinations.size(), probeNoPartition = 0, probeNoPoint = 0;
    uint32 probeRejectLevel = 0, probeRejectDistance = 0, probeFarthest = 0;

    for (auto& dest : destinations)
    {
        TravelPoint point(dest, sTravelMgr.nullWorldPosition, 0.0f);

        std::pair<uint32, std::vector<WorldPosition*>> pointRange = dest->GetClosestPartition(center, distancePartitions);

        if (!pointRange.first)
        {
            probeNoPartition++;
            continue;
        }

        MANGOS_ASSERT(pointRange.second.size());
        std::vector<WorldPosition*> points = pointRange.second;
        std::shuffle(points.begin(), points.end(), std::default_random_engine(seed));

        for (auto& position : points)
        {
            if (!IsLocationLevelValid(*position, info, purposeFlag, onlyPossible))   // twow-repo#601 S2 R3: window only with IsPossible
            {
                probeRejectLevel++;
                continue;
            }

            float distance = position->distance(center);

            if (distance > maxDistance)
            {
                probeRejectDistance++;
                if (distance > probeFarthest)
                    probeFarthest = uint32(distance);
                continue;
            }
            
            point = TravelPoint(dest, position, distance);
        }

        if (std::get<2>(point) > 0)
            pointMap[pointRange.first].push_back(point);
        else
            probeNoPoint++;
    }

    if (pointMap.empty() && probeTotal && (purposeFlag & (uint32)TravelDestinationPurpose::QuestTaker))
        sLog.outBasic("PARTPROBE: level %u, %u taker destinations, none survived - %u had no partition, %u no usable point; points rejected: %u by level, %u by distance (max allowed %.0f, farthest seen %u)",
            info.GetLevel(), probeTotal, probeNoPartition, probeNoPoint,
            probeRejectLevel, probeRejectDistance, maxDistance, probeFarthest);

    sTravelMgr.GetPartitionsLock(false);

    return pointMap;
}

void TravelMgr::ShuffleTravelPoints(std::vector<TravelPoint>&points)
{
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::shuffle(points.begin(), points.end(), std::default_random_engine(seed));
    /*
    std::vector<uint32> weights;
    std::transform(points.begin(), points.end(), std::back_inserter(weights), [](TravelPoint point) { return 200000 / (1 + std::get<2>(point)); });

    //If any weight is 0 add 1 to all weights.
    for (auto& w : weights)
    {
        if (w > 0)
            continue;

        std::for_each(weights.begin(), weights.end(), [](uint32& d) { d += 1; });
        break;
    }

    std::mt19937 gen(time(0));

    WeightedShuffle(points.begin(), points.end(), weights.begin(), weights.end(), gen);
    */
}

void TravelMgr::SetNullTravelTarget(TravelTarget* target) const
{
    if (target)
    {
        target->SetTarget(nullTravelDestination, nullWorldPosition);
        target->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
    }
}

void TravelMgr::SetNullTravelTarget(Player* player) const
{
    if (!player)
        return;

    if (!GetBotAI(player))
        return;

    TravelTarget* target = GetBotAI(player)->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();

    SetNullTravelTarget(target);
}

void TravelMgr::AddMapTransfer(WorldPosition start, WorldPosition end, float portalDistance, bool makeShortcuts)
{
    uint32 sMap = start.getMapId();
    uint32 eMap = end.getMapId();

    if (sMap == eMap)
        return;
    
    //Calculate shortcuts.
    if(makeShortcuts)
        for (auto& mapTransfers : mapTransfersMap)
        {
            uint32 sMapt = mapTransfers.first.first;
            uint32 eMapt = mapTransfers.first.second;

            for (auto& mapTransfer : mapTransfers.second)
            {
                if (eMapt == sMap && sMapt != eMap) // [S1 >MT> E1 -> S2] >THIS> E2
                {
                    float newDistToEnd = MapTransDistance(mapTransfer.GetPointFrom(), start) + portalDistance;
                    if (MapTransDistance(mapTransfer.GetPointFrom(), end) > newDistToEnd)
                        AddMapTransfer(mapTransfer.GetPointFrom(), end, newDistToEnd, false);
                }

                if (sMapt == eMap && eMapt != sMap) // S1 >THIS> [E1 -> S2 >MT> E2]
                {
                    float newDistToEnd = portalDistance + MapTransDistance(end, mapTransfer.GetPointTo());
                    if (MapTransDistance(start, mapTransfer.GetPointTo()) > newDistToEnd)
                        AddMapTransfer(start, mapTransfer.GetPointTo(), newDistToEnd, false);
                }
            }
        }

    //Add actual transfer.
    auto mapTransfers = mapTransfersMap.find(std::make_pair(start.getMapId(), end.getMapId()));
    
    if (mapTransfers == mapTransfersMap.end())
        mapTransfersMap.insert({ { sMap, eMap }, {MapTransfer(start, end, portalDistance)} });
    else
        mapTransfers->second.push_back(MapTransfer(start, end, portalDistance));        
};

void TravelMgr::LoadMapTransfers()
{
    for (auto& startNode : sTravelNodeMap.getNodes())
    {
        for (auto& [endNode, path] : *startNode->getLinks())
        {
            AddMapTransfer(*startNode->getPosition(), *endNode->getPosition(), path->getDistance());
        }
    }
}

std::vector<std::pair<WorldPosition, float>> TravelMgr::sqMapTransDistances(const WorldPosition& start, uint32 endMapId) const
{
    uint32 startMapId = start.getMapId();
    if (startMapId == endMapId)
        return { { start, 0.0f } };

    auto mapTransfers = mapTransfersMap.find({ startMapId, endMapId });   

    if (mapTransfers == mapTransfersMap.end())
        return {};

    std::vector<std::pair<WorldPosition, float>> retPortals;

    for (auto& mapTrans : mapTransfers->second)
    {
        WorldPosition portalOnEndMap = mapTrans.GetPointTo();

        float sqDist = mapTrans.sqDist(start, portalOnEndMap);

        retPortals.push_back(std::make_pair(portalOnEndMap, sqDist));
    }

    return retPortals;
}

float TravelMgr::MapTransDistance(const WorldPosition& start, const WorldPosition& end, bool toMap) const
{
    //These are the portals on the end map that start can reach.
    std::vector<std::pair<WorldPosition, float>> portals = sqMapTransDistances(start, end.getMapId());

    if (portals.empty())
        return FLT_MAX;

    float minsqDist = FLT_MAX;

    for (auto& [portal, sqDistanceFromStartToPortal] : portals)
    {
        float sqDist = sqDistanceFromStartToPortal + portal.sqDistance2d(end);
        if (sqDist < minsqDist)
            minsqDist = sqDist;
    }

    return sqrt(minsqDist);
}
