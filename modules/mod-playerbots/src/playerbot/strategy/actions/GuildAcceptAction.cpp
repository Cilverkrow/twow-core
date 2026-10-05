
#include "playerbot/playerbot.h"
#include "GuildAcceptAction.h"
#include "GuildCreateActions.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/GuildPoachPolicy.h"
#include "playerbot/RosterGuildPolicy.h"
#include "Guild/GuildMgr.h"
#include "GuildCreateActions.h"
#include <mutex>
#include <unordered_map>

using namespace ai;

char const* RosterGuildPoach::Refusal(Player* inviter, ObjectGuid const& invitee, uint32 targetGuildId, bool charterOfInviter, char const* path)
{
    guild_poach::PoachInput in;
    in.enabled = sPlayerbotAIConfig.enabled && sPlayerbotAIConfig.rosterGuildAllowPoaching;
    in.rosterBot = !invitee.IsEmpty() && sRandomPlayerbotMgr.IsPersistentRosterMember(invitee.GetCounter());
    if (!in.enabled || !in.rosterBot || !inviter)
        return guild_poach::PoachDecisionName(guild_poach::DecidePoach(in));

    // Copies only: the invitee may be updated on another map thread.
    in.inviterRealPlayer = IsRealPlayer(inviter);
    PlayerCacheData const* inviteeData = sObjectMgr.GetPlayerDataByGUID(invitee.GetCounter());
    PlayerCacheData const* inviterData = sObjectMgr.GetPlayerDataByGUID(inviter->GetGUIDLow());
    in.sameFaction = inviteeData && inviterData &&
        Player::TeamForRace(uint8(inviteeData->uiRace)) == Player::TeamForRace(uint8(inviterData->uiRace));

    in.currentGuildId = sGuildMgr.GetPlayerGuildId(invitee.GetCounter());
    GuildSummary current;
    if (in.currentGuildId && sGuildMgr.GetGuildSummary(in.currentGuildId, current))
    {
        in.currentGuildIsBotGuild = sRandomPlayerbotMgr.IsPersistentRosterMember(current.leaderGuid.GetCounter());
        in.leadsCurrentGuild = current.leaderGuid == invitee;
    }

    in.targetGuildId = targetGuildId;
    GuildSummary target;
    if (targetGuildId && sGuildMgr.GetGuildSummary(targetGuildId, target))
        in.targetIsBotGuild = sRandomPlayerbotMgr.IsPersistentRosterMember(target.leaderGuid.GetCounter());
    in.charterOfInviter = charterOfInviter;

    in.now = time(nullptr);
    in.lastSwitch = sGuildMgr.GetLastGuildSwitch(invitee.GetCounter());
    in.cooldownSeconds = guild_poach::PoachCooldown(sPlayerbotAIConfig.rosterGuildPoachCooldownSeconds);

    guild_poach::PoachDecision const decision = guild_poach::DecidePoach(in);
    if (decision == guild_poach::PoachDecision::Allow)
        return nullptr;

    if (guild_poach::IsTracedRefusal(decision))
    {
        // At most one line a minute per roster bot (the map is bounded by the roster size); only
        // invitations of players reach this point.
        static std::mutex traceLock;
        static std::unordered_map<uint32, time_t> lastTrace;
        bool due = false;
        {
            std::lock_guard<std::mutex> guard(traceLock);
            time_t& last = lastTrace[invitee.GetCounter()];
            if (!last || in.now >= last + 60)
            {
                last = in.now;
                due = true;
            }
        }
        if (due)
            sLog.outBasic("[RosterGuild] event=poach_refused bot=%u inviter=%u from=%u to=%u path=%s reason=%s",
                invitee.GetCounter(), inviter->GetGUIDLow(), in.currentGuildId, targetGuildId, path,
                guild_poach::PoachDecisionName(decision));
    }

    return guild_poach::PoachDecisionName(decision);
}

uint32 RosterGuildPoach::KeptCharter(ObjectGuid const& bot, ObjectGuid const& offeredCharter)
{
    // Only a bot that switched since the server start can keep a charter; the stamp is read first,
    // so every other bot skips the petition scan.
    time_t const lastSwitch = sGuildMgr.GetLastGuildSwitch(bot.GetCounter());
    if (!lastSwitch)
        return 0;

    PetitionSummary signedCharter;
    bool const signsCharter = sGuildMgr.GetPetitionSummaryBySigner(bot, signedCharter);
    bool const offeredIsSigned = signsCharter && !offeredCharter.IsEmpty() && signedCharter.charterGuid == offeredCharter;
    if (!guild_poach::KeepsPoachedCharter(signsCharter, offeredIsSigned, time(nullptr), lastSwitch,
            guild_poach::PoachCooldown(sPlayerbotAIConfig.rosterGuildPoachCooldownSeconds)))
        return 0;
    return signedCharter.id;
}

bool GuildAcceptAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    WorldPacket p(event.getPacket());
    p.rpos(0);
    Player* inviter = nullptr;
    std::string Invitedname;
    p >> Invitedname;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    std::map<std::string, std::string> placeholders;
    placeholders["%name"] = inviter->GetName();

    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    // twow-repo#485 (owner decision 5, poaching): a roster bot of a bot guild takes a real player's
    // invitation (the core let it through, PlayerScript::CanSwitchGuild); the same rule again here.
    uint32 const fromGuildId = bot->GetGuildId();
    uint32 const invitedGuildId = bot->GetGuildIdInvited();
    bool const poach = guildId && fromGuildId && invitedGuildId &&
        !RosterGuildPoach::Refusal(inviter, bot->GetObjectGuid(), invitedGuildId, false, "accept");
    if (!guildId)
    {
        ai->TellError(requester, "You are not in a guild!");

        if(sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && GetBotAI(inviter))
            bot->Say(BOT_TEXT2("You are not in a guild %name!", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (fromGuildId && !poach)
    {
        ai->TellError(requester, "Sorry, I am in a guild already");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && GetBotAI(inviter))
            bot->Say(BOT_TEXT2("Sorry, I am in a guild already %name.", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (!fromGuildId && RosterGuildPoach::KeptCharter(bot->GetObjectGuid(), ObjectGuid()))
    {
        // twow-repo#485 (poaching, no ping-pong): within the cooldown a bot that switched by charter
        // keeps its signature on that charter and joins no other guild.
        ai->TellError(requester, "Sorry, I signed a charter already");
        accept = false;
        if (RosterGuildPlan::IsDue(ai, "roster guild keep charter trace", HOUR))
            sLog.outBasic("[RosterGuild] event=invite_declined bot=%u inviter=%u guild=%u reason=poached_keeps_charter",
                bot->GetGUIDLow(), inviter->GetGUIDLow(), guildId);
    }
    else if (!roster_guild::JoinAllowed(bot->GetLevel(), sPlayerbotAIConfig.rosterGuildMinLevel,
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster(), IsRealPlayer(inviter)))
    {
        // Owner 05.10.2026: no bot guild below AiPlayerbot.RosterGuild.MinLevel (players reserve first).
        ai->TellError(requester, "Sorry, I am too young for a guild");
        accept = false;
        if (RosterGuildPlan::IsDue(ai, "roster guild min level trace", HOUR))
            sLog.outBasic("[RosterGuild] event=join_deferred bot=%u level=%u inviter=%u guild=%u path=accept reason=min_level",
                bot->GetGUIDLow(), bot->GetLevel(), inviter->GetGUIDLow(), guildId);
    }
    else if (!ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, inviter, true))
    {
        ai->TellError(requester, "Sorry, I don't want to join your guild :(");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && GetBotAI(inviter))
            bot->Say(BOT_TEXT2("Sorry, I don't want to join your guild %name :(.", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }

    Guild* guild = sGuildMgr.GetGuildById(guildId);

    if(guild && guild->GetMemberSize() > 1000)
    {
        ai->TellError(requester, "This guild has over 1000 members. To stop it from reaching the 1064 member limit I refuse to join it.");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && GetBotAI(inviter))
            bot->Say(BOT_TEXT2("%name, your guild has over 1000 members. To stop it from reaching the 1064 member limit I refuse to join it.", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }

    // twow-repo#485 / #518 (role fill, RosterGuild.Tanks/Healers/Dps or PlanFile): a roster bot on its
    // own accepts a bot's invite only into the guild it is dealt to (its plan guild, or the role deal
    // with the spread switches). Invites of real players stay as they are.
    bool const roleFill = accept && !IsRealPlayer(inviter) && RosterGuildPlan::UsesRoleFill(ai);
    if (roleFill)
    {
        RosterGuildPlan::ReportOwnRole(bot);
        uint32 const assigned = RosterGuildPlan::AssignedGuild(bot->GetGUIDLow(), bot->GetTeam());
        if (assigned != guildId)
        {
            accept = false;
            if (RosterGuildPlan::IsDue(ai, "roster guild accept trace", HOUR))
                sLog.outBasic("[RosterGuild] event=invite_declined bot=%u guild=%u dealt_to=%u", bot->GetGUIDLow(), guildId, assigned);
        }
    }

    if (accept && sPlayerbotAIConfig.inviteChat && sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && GetBotAI(inviter) && (sRandomPlayerbotMgr.IsFreeBot(bot) || !ai->HasActivePlayerMaster()))
    {
        if (urand(0, 3))
            bot->Say(BOT_TEXT2("Sounds good %name sign me up!", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        else
            bot->Say(BOT_TEXT2("I would love to join!", placeholders), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
    }

    WorldPacket packet;
    if (accept && poach)
    {
        // Leave the bot guild and join on the world thread (GuildMgr::Update), never from this map
        // thread: guild opcodes and roster packets run there. The result is traced there
        // (event=poached / poach_failed).
        sGuildMgr.RequestGuildSwitch(bot->GetObjectGuid(), fromGuildId, invitedGuildId, 0);
        sLog.outBasic("[RosterGuild] event=poach_accepted bot=%u inviter=%u from=%u to=%u via=invite",
            bot->GetGUIDLow(), inviter->GetGUIDLow(), fromGuildId, invitedGuildId);
    }
    else if (accept)
    {
        bot->GetSession()->HandleGuildAcceptOpcode(packet);

        if (roleFill && bot->GetGuildId() == guildId)
            RosterGuildPlan::NoteJoined(bot, guildId);

        TalentSpec::SetPublicNote(bot);

        sPlayerbotAIConfig.logEvent(ai, "GuildAcceptAction", guild->GetName(), std::to_string(guild->GetMemberSize()));
    }
    else if (fromGuildId)
    {
        // HandleGuildDeclineOpcode ignores guild members, so an invitation the core let through for
        // poaching would block every later one ("already invited"): drop it here.
        if (invitedGuildId)
            bot->SetGuildIdInvited(0);
    }
    else
    {
        bot->GetSession()->HandleGuildDeclineOpcode(packet);
    }
    return true;
}
