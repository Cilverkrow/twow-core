
#include "playerbot/playerbot.h"
#include "GroupValues.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

namespace
{
    // twow-repo#563 (X3b site 2, AiPlayerbot.X3b.PublishedConditions): the condition of one group member.
    // 1 = true, 0 = false, -1 = no fresh answer from that member yet (the caller skips the member, as it
    // skips members without bot AI). Off, or for this bot itself: evaluated on the member's context as before.
    int MemberCondition(PlayerbotAI* ai, PlayerbotAI* memberAi, std::string const& condition)
    {
        if (memberAi != ai && sPlayerbotAIConfig.x3bPublishedConditions)
        {
            std::optional<bool> const answer = memberAi->GetConditionBoard().Read(condition, WorldTimer::getMSTime());
            if (!answer)
                return -1;
            return *answer ? 1 : 0;
        }
        return memberAi->GetAiObjectContext()->GetValue<bool>("and", condition)->Get() ? 1 : 0;
    }
}

std::list<ObjectGuid> GroupMembersValue::Calculate()
{
    std::list<ObjectGuid> members;

    Group* group = bot->GetGroup();
    if (group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            members.push_back(ref->getSource()->GetObjectGuid());
        }
    }
    else
        members.push_back(bot->GetObjectGuid());

    return members;
}


bool IsFollowingPartyValue::Calculate()
{
    if (ai->GetGroupMaster() == bot)
        return true;

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
        return true;

    return false;
}

bool IsNearLeaderValue::Calculate()
{
    Player* groupMaster = ai->GetGroupMaster();

    if (!groupMaster)
        return false;

    if (groupMaster == bot)
        return true;

    return sServerFacade.GetDistance2d(bot, ai->GetGroupMaster()) < sPlayerbotAIConfig.reactDistance;
}

uint32 GroupBoolCountValue::Calculate()
{
    uint32 count = 0;

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!GetBotAI(player))
            continue;

        int const condition = MemberCondition(ai, GetBotAI(player), getQualifier());
        if (condition < 0)
            continue;

        if (condition)
            return count++;
    }

    return count;
};

bool GroupBoolANDValue::Calculate()
{
    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!GetBotAI(player))
            continue;

        int const condition = MemberCondition(ai, GetBotAI(player), getQualifier());
        if (condition < 0)
            continue;

        if (!condition)
            return false;
    }

    return true;
};

bool GroupBoolORValue::Calculate()
{
    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!GetBotAI(player))
            continue;

        int const condition = MemberCondition(ai, GetBotAI(player), getQualifier());
        if (condition < 0)
            continue;

        if (condition)
            return true;
    }

    return false;
};

bool GroupReadyValue::Calculate()
{
    bool inDungeon = !WorldPosition(bot).isOverworld();

    for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, "group members"))
    {
        Player* member = sObjectMgr.GetPlayer(guid);

        if (!member)
            continue;

        if (inDungeon) // In dungeons all following members need to be alive before continuing.
        {
            PlayerbotAI* memberAi = GetBotAI(member);

            bool isFollowing = memberAi
                ? (memberAi->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                    memberAi->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
                : true;

            if (!member->IsAlive() && isFollowing)
                return false;
        }
        //We only wait for members that are in range otherwise we might be waiting for bots stuck in dead loops forever.
        if (ai->GetGroupMaster() && sServerFacade.GetDistance2d(member, ai->GetGroupMaster()) > sPlayerbotAIConfig.sightDistance)
            continue;        

        bool hasAttackers = AI_VALUE_LAZY(bool, "has attackers") || AI_VALUE_LAZY(bool, "has enemy player targets") || AI_VALUE_LAZY(Unit*, "dps target");

        //Wait for members to recover health/mana.
        if (hasAttackers && member->GetHealthPercent() < sPlayerbotAIConfig.almostFullHealth && !member->IsInCombat())
            return false;

        if (!member->GetPower(POWER_MANA))
            continue;

        float mana = (static_cast<float> (member->GetPower(POWER_MANA)) / member->GetMaxPower(POWER_MANA)) * 100;

        if (mana < sPlayerbotAIConfig.mediumMana && !member->IsInCombat())
            return false;
    }

    return true;
};