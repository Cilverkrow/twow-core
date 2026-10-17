#pragma once
#include "MovementActions.h"
#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "Battlegrounds/BattleGround.h"
#include "Battlegrounds/BattleGroundMgr.h"
#include "Battlegrounds/BattleGroundWS.h"
#include "ChooseTargetActions.h"
#include "CheckMountStateAction.h"
#include "G3D/Vector3.h"
#include "Objects/GameObject.h"

using namespace ai;


class BGJoinAction : public Action
{
public:
    BGJoinAction(PlayerbotAI* ai, std::string name = "bg join") : Action(ai, name) {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "bg join"; }
        virtual std::string GetHelpDescription()
        {
            return "This action will make bots join a bg queue that has a player waiting to join.\n"
                   "Bots will automatically fill up the bg to make sure the player has enough opponents.\n"
                   "Bots are able to join from anywhere in the world.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    virtual bool Execute(Event& event) override;
    virtual bool isUseful() override;
    virtual bool canJoinBg(Player* player, BattleGroundQueueTypeId queueTypeId, BattleGroundBracketId bracketId);
    virtual bool shouldJoinBg(BattleGroundQueueTypeId queueTypeId, BattleGroundBracketId bracketId);
#ifndef MANGOSBOT_ZERO
    virtual bool gatherArenaTeam(ArenaType type);
#endif
    // twow-repo#517 stage a: the non-arena battlemaster join JoinQueue sends ("bg type" reset, bypass guid), shared with
    // RandomPlayerbotMgr::CheckBgFill, which queues roster bots from the world thread. Stage b: instanceId = the client
    // instance id a pair bot is queued for (DesiredInstanceId), 0 = first available.
    static void SendBattlemasterJoin(PlayerbotAI* ai, Player* bot, BattleGroundTypeId bgTypeId, bool joinAsGroup, uint32 instanceId = 0);
protected:
    bool JoinQueue(uint32 type);
    std::vector<uint32> bgList;
    std::vector<uint32> ratedList;
};

class FreeBGJoinAction : public BGJoinAction
{
public:
    FreeBGJoinAction(PlayerbotAI* ai, std::string name = "free bg join") : BGJoinAction(ai, name) {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "free bg join"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot join the battlegroun queue of the battlemaster it was just talking to.\n"
                   "This action results from a rpg action with an actual npc.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    virtual bool shouldJoinBg(BattleGroundQueueTypeId queueTypeId, BattleGroundBracketId bracketId);
};

class BGLeaveAction : public Action
{
public:
    BGLeaveAction(PlayerbotAI* ai, std::string name = "bg leave") : Action(ai) {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "bg leave"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot leave the current battleground.\n"
                   "This action is normally used after the battleground ends.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    virtual bool Execute(Event& event) override;
};

class BGStatusAction : public Action
{
public:
    BGStatusAction(PlayerbotAI* ai) : Action(ai, "bg status") {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "bg status"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot check the battleground queue status.\n"
                   "It will make the bot teleport to the battleground when it is ready.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    virtual bool Execute(Event& event) override;
    virtual bool isUseful() override;
    // twow-repo#517 stage a: what BGStatusAction does for the bot once it accepted an invite (strategies, +pvp, role,
    // objectives); shared with the manager's accept for fill bots.
    static void ApplyPortState(PlayerbotAI* ai);
    // twow-repo#517 stage a: the manager accepts the invite of a fill bot on the world thread (port action 1, then
    // ApplyPortState). True when the bot is now bound for that battleground.
    static bool AcceptInvite(PlayerbotAI* ai, Player* bot, BattleGroundTypeId bgTypeId);
    // twow-repo#517 stage a: the manager takes a fill bot out of its queue again (port action 0), on the world thread.
    static void LeaveQueue(Player* bot, BattleGroundTypeId bgTypeId);
    // twow-repo#517 stage b: what BGLeaveAction does for the bot after it left a battleground (strategies back, bg values
    // cleared); for the manager's slot release and bot-only cleanup, on the world thread.
    static void ApplyLeaveState(PlayerbotAI* ai, Player* bot);
private:
    static void SendPort(Player* bot, BattleGroundTypeId bgTypeId, uint8 action);
};

class BGStatusCheckAction : public Action
{
public:
    BGStatusCheckAction(PlayerbotAI* ai, std::string name = "bg status check") : Action(ai, name) {}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "bg status check"; }
        virtual std::string GetHelpDescription()
        {
            return "This action makes the bot ask for the current battleground queue status.";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif 
    virtual bool Execute(Event& event) override;
    virtual bool isUseful() override;
};
