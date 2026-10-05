
#include "playerbot/playerbot.h"
#include "MoveToTravelTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/LootObjectStack.h"
#include "Maps/PathFinder.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/strategy/values/FreeMoveValues.h"
#include "playerbot/StartupTravelPolicy.h"
#include <chrono>
#include <iomanip>

using namespace ai;

namespace
{
// twow-repo#540: one [StartupTravel] line a minute while anything was deferred.
void LogStartupTravel()
{
    startup_travel::Counters& counters = startup_travel::SharedCounters();
    if (!counters.LogDue(uint64(time(nullptr))))
        return;
    uint32 const jitter = counters.jitter.exchange(0, std::memory_order_relaxed);
    uint32 const budget = counters.budget.exchange(0, std::memory_order_relaxed);
    if (jitter || budget)
        sLog.outBasic("[StartupTravel] uptime=%u deferred_jitter=%u deferred_budget=%u", sWorld.GetUptime(), jitter, budget);
}
}

bool MoveToTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
    {
        ai->TellDebug(ai->GetMaster(), "The target is ready to travel start now.", "debug travel");
        target->SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
    }

    target->CheckStatus();

    if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return true;

    WorldPosition botLocation(bot);
    WorldPosition location = *target->GetPosition();
    
    Group* group = bot->GetGroup();
    if (ai->IsGroupLeader() && !urand(0, 1) && !bot->IsInCombat())
    {        
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member == bot)
                continue;

            if (!member->IsAlive())
                continue;

            if (!member->IsMoving())
                continue;

            if (GetBotAI(member) &&
                !(GetBotAI(member)->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || GetBotAI(member)->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                continue;

            WorldPosition memberPos(member);
            WorldPosition targetPos = *target->GetPosition();

            float memberDistance = std::min(botLocation.distance(memberPos), location.distance(memberPos));

            if (memberDistance < 50.0f)
                continue;
            if (memberDistance > sPlayerbotAIConfig.reactDistance * 20)
                continue;

           // float memberAngle = botLocation.getAngleBetween(targetPos, memberPos);

           // if (botLocation.getMapId() == targetPos.getMapId() && botLocation.getMapId() == memberPos.getMapId() && memberAngle < M_PI_F / 2) //We are heading that direction anyway.
           //     continue;

            if (!urand(0, 5))
            {
                std::ostringstream out;
                if ((ai->GetMaster() && !bot->GetGroup()->IsMember(ai->GetMaster()->GetObjectGuid())) || !ai->HasActivePlayerMaster())
                    out << "Waiting a bit for ";
                else
                    out << "Please hurry up ";

                out << member->GetName();

                if (GetBotAI(bot) && !ai->HasActivePlayerMaster())
                {
                    out << " who is " << round(memberDistance) << "y away";
                    if (!memberPos.getAreaName().empty())
                        out << " in " << memberPos.getAreaName();
                }

                ai->TellPlayerNoFacing(GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            // Introduce a random delay between 80% and 120% of maxWaitForMove to make waiting more natural
            uint32 randomDelay = sPlayerbotAIConfig.maxWaitForMove * (urand(80, 120) / 100.0f);
            target->SetExpireIn(target->GetTimeLeft() + randomDelay);

            SetDuration(randomDelay);

            // Occasionally face the member and perform an emote
            if (urand(0, 3) == 0) { // 25% chance to emote
                bot->SetFacingToObject(member);
                uint32 emoteChoice = urand(0, 2);
                switch (emoteChoice) {
                    case 0:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        break;
                    case 1:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        break;
                    case 2:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                        break;
                }
            }

            return true;
        }
    }

    float x = location.getX();
    float y = location.getY();
    float z = location.getZ();
    float mapId = location.getMapId();

    if (botLocation.getMapId() == location.getMapId() && botLocation.sqDistance2d(location) < 10000.0f)
    {
        float maxDistance = target->GetDestination()->GetRadiusMin();

        float angle = 2 * M_PI * urand(0, 100) / 100.0;
        float mod = urand(50, 100) / 100.0;

        x += cos(angle) * maxDistance * mod;
        y += sin(angle) * maxDistance * mod;

        if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
        {
            std::ostringstream out;
            out << "Moving to ";
            out << target->GetDestination()->GetTitle();
            if (!(*target->GetPosition() == WorldPosition()))
            {
                out << " at " << uint32(target->GetPosition()->distance(bot)) << "y";
            }
            if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
                out << " for " << (target->GetTimeLeft() / 1000) << "s";
            if (target->GetRetryCount(true))
                out << " (move retry: " << target->GetRetryCount(true) << ")";
            else if (target->GetRetryCount(false))
                out << " (retry: " << target->GetRetryCount(false) << ")";
            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    // twow-repo#540: in the first minutes after a server start only a few long moves without a
    // cached route (node route / path finder) start per 100 ms slot; the rest wait for a later slot
    // without a retry or cooldown.
    if (sPlayerbotAIConfig.startupTravelMaxLongMovesPerSlot &&
        startup_travel::InWindow(sWorld.GetUptime(), sPlayerbotAIConfig.startupTravelWindowSeconds))
    {
        LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
        bool const sameMap = botLocation.getMapId() == location.getMapId();
        bool const cached = !lastMove.lastPath.empty() && lastMove.lastPath.getBack().distance(location) < 20.0f;
        uint64 const nowMs = uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (startup_travel::IsLongMove(sameMap, sameMap ? botLocation.distance(location) : 0.0f, cached) &&
            !startup_travel::SharedBudget().TryTake(nowMs, sPlayerbotAIConfig.startupTravelMaxLongMovesPerSlot))
        {
            startup_travel::SharedCounters().budget.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    bool canMove = MoveTo(mapId, x, y, z, false, false);

    if (!canMove)
    {
        target->IncRetry(true);

        if (target->IsMaxRetry(true))
        {
            ai->TellDebug(ai->GetMaster(), "The target is cooling down because we failed to move to it a few times in a row.", "debug travel");
            target->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);      
            target->SetForced(false);
        }
    }
    else
        target->DecRetry(true);

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        WorldPosition* pos = target->GetPosition();
        GuidPosition* guidP = dynamic_cast<GuidPosition*>(pos);

        std::string name = (guidP && guidP->GetWorldObject(bot->GetInstanceId())) ? chat->formatWorldobject(guidP->GetWorldObject(bot->GetInstanceId())) : "travel target";

        if (mapId == bot->GetMapId())
        {
            ai->Poi(x, y, name);
        }
        else
        {
            LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
            if (!lastMove.lastPath.empty() && lastMove.lastPath.getBack().distance(location) < 20.0f)
            {
                for (auto& p : lastMove.lastPath.getPointPath())
                {
                    if (p.getMapId() == bot->GetMapId())
                        ai->Poi(p.getX(), p.getY(), name);
                }
            }
        }
    }
     
    return canMove;
}

bool MoveToTravelTargetAction::isUseful()
{
    LogStartupTravel();

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "travel target traveling") && AI_VALUE(TravelTarget*, "travel target")->GetStatus() != TravelStatus::TRAVEL_STATUS_READY)
        return false;

    if (bot->IsTaxiFlying())
        return false;

    if (MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 10)
#ifndef MANGOSBOT_ZERO
        if (bot->IsMovingIgnoreFlying())
            return false;
#else
        if (bot->IsMoving())
            return false;
#endif

    if (!AI_VALUE(bool, "can move around"))
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
    {
        auto conditions = travelTarget->GetConditions();
        for (auto& cond : conditions)
        {
            if (cond == "should travel named::guild order")
                return false;
        }
    }

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            if (!travelTarget->IsForced())
                return false;

    WorldPosition travelPos(*travelTarget->GetPosition());

    if (travelPos.isDungeon() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetObjectGuid()) && sTravelMgr.MapTransDistance(bot, travelPos, true) < sPlayerbotAIConfig.sightDistance && !AI_VALUE2(bool, "group and", "near leader"))
        return false;
     
    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    if (!travelTarget->IsForced())
        if (!CanFreeMoveValue::CanFreeMoveTo(ai, *travelTarget->GetPosition()))
            return false;

    return true;
}

