
#include "playerbot/playerbot.h"
#include "PetitionSignAction.h"
#include "GuildCreateActions.h"
#include "playerbot/RosterGuildPolicy.h"
#include "Guild/GuildMgr.h"
#ifndef MANGOSBOT_ZERO
#ifdef CMANGOS
#include "Arena/ArenaTeam.h"
#endif
#ifdef MANGOS
#include "ArenaTeam.h"
#endif
#endif

using namespace ai;

bool PetitionSignAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid petitionGuid;
    ObjectGuid inviter;
    uint8 unk = 0;
    bool isArena = false;
    p >> petitionGuid >> inviter;
    uint32 type = 9;

#ifndef MANGOSBOT_ZERO
    auto result = CharacterDatabase.PQuery("SELECT `type` FROM `petition` WHERE `petitionguid` = '%u'", petitionGuid.GetCounter());
    if (!result)
    {
        return false;
    }

    Field* fields = result->Fetch();
    type = fields[0].GetUInt32();
#endif

    bool accept = true;

    if (type != 9)
    {
#ifndef MANGOSBOT_ZERO
        isArena = true;
        uint8 slot = ArenaTeam::GetSlotByType(ArenaType(type));
        if (bot->GetArenaTeamId(slot))
        {
            // player is already in an arena team
            ai->TellError(requester, "Sorry, I am already in such team");
            accept = false;
        }
#endif
    }
    else
    {
        if (bot->GetGuildId())
        {
            ai->TellError(requester, "Sorry, I am in a guild already");
            accept = false;
        }

        if (bot->GetGuildIdInvited())
        {
            ai->TellError(requester, "Sorry, I am invited to a guild already");
            accept = false;
        }

        // check for same acc id
        /*QueryResult* result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = '%u' AND petitionguid = '%u'", bot->GetSession()->GetAccountId(), petitionGuid.GetCounter());

        if (result)
        {
            ai->TellError("Sorry, I already signed this pettition");
            accept = false;
        }
        delete result;*/
    }

    Player* _inviter = sObjectMgr.GetPlayer(inviter);
    if (!_inviter)
        return false;

    if (_inviter == bot)
        return false;

    // twow-repo#485 (new path): a roster bot on its own signs only towards the faction's guild
    // target, and its signature moves only to a fuller charter (88 % of the 3,122 v24 signatures
    // were moves between bot charters). Counts are copies taken under the petition lock.
    if (accept && !isArena && RosterGuildPlan::UsesRosterPath(ai))
    {
        PetitionSummary offered;
        if (!sGuildMgr.GetPetitionSummaryByCharterGuid(petitionGuid, offered))
        {
            // Critic B5.2: the charter is gone (v24: 7x "No petition exists for charter").
            accept = false;
            sLog.outBasic("[RosterGuild] event=sign_declined bot=%u charter=%u reason=no_petition", bot->GetGUIDLow(), petitionGuid.GetCounter());
        }
        else
        {
            PetitionSummary current;
            int const currentSigned = sGuildMgr.GetPetitionSummaryBySigner(bot->GetObjectGuid(), current) ? int(current.signatureCount) : -1;
            roster_guild::SignDecision const decision = roster_guild::DecideSign(IsRealPlayer(_inviter), offered.team == bot->GetTeam(),
                !RosterGuildPlan::MayFound(bot), currentSigned, offered.signatureCount);
            if (decision != roster_guild::SignDecision::Accept)
            {
                accept = false;
                if (RosterGuildPlan::IsDue(ai, "roster guild sign trace", HOUR))
                    sLog.outBasic("[RosterGuild] event=sign_declined bot=%u charter=%u reason=%s offered=%u current=%d",
                        bot->GetGUIDLow(), petitionGuid.GetCounter(), roster_guild::SignDecisionName(decision), uint32(offered.signatureCount), currentSigned);
            }
        }
    }

    if (!accept || !ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, _inviter, true))
    {
        WorldPacket data(MSG_PETITION_DECLINE);
        data << petitionGuid;
        bot->GetSession()->HandlePetitionDeclineOpcode(data);
        sLog.outDetail("Bot #%d <%s> declines %s invite", bot->GetGUIDLow(), bot->GetName(), isArena ? "Arena" : "Guild");
        return false;
    }
    if (accept)
    {
        WorldPacket data(CMSG_PETITION_SIGN, 20);
        data << petitionGuid << unk;
        bot->GetSession()->HandlePetitionSignOpcode(data);
        // twow-repo#485/#478 (owner 02.10., point 7): no /say towards bots, and only with InviteChat.
        if (sPlayerbotAIConfig.inviteChat && IsRealPlayer(_inviter))
            bot->Say("Thanks for the invite!", LANG_UNIVERSAL);
        sLog.outDetail("Bot #%d <%s> accepts %s invite", bot->GetGUIDLow(), bot->GetName(), isArena ? "Arena" : "Guild");
        return true;
    }
    return false;
}