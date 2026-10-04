
#include "playerbot/playerbot.h"
#include "GuildTriggers.h"
#include "Guild/GuildMgr.h"
#include "playerbot/RosterGuildPolicy.h"

using namespace ai;

bool LeaveLargeGuildTrigger::IsActive()
{
	if (!bot->GetGuildId())
		return false;

	// twow-repo#485 (critic B5.6): with roster guilds (BotsPerGuild > 0) a roster bot on its own never
	// leaves a guild by itself (GetMaxPreferedGuildSize: 0 for GuilderType SOLO, 30 for TINY - a bot
	// guild of 45 is wanted).
	if (roster_guild::UsesRosterPath(sPlayerbotAIConfig.rosterGuildBotsPerGuild, sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), ai->HasRealPlayerMaster()))
		return false;

	if (ai->HasActivePlayerMaster())
		return false;

	if (ai->IsAlt())
		return false;

	Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());

	if (guild->GetMemberSize() >= 1000) //Try to prevent guild overflow.
		return true;

	if (!sPlayerbotAIConfig.randomBotGuildNearby)
		return false;

	if (ai->IsInRealGuild())
		return false;

	Player* leader = sObjectMgr.GetPlayer(guild->GetLeaderGuid());

	//Only leave the guild if we know the leader is not a real player.
	if (!leader || !GetBotAI(leader) || GetBotAI(leader)->IsRealPlayer())
		return false;

	uint32 members = guild->GetMemberSize();
	uint32 maxMembers = ai->GetMaxPreferedGuildSize();

	return members > maxMembers;
}

bool RosterGuildNoteTrigger::IsActive()
{
	if (!roster_guild::UsesGuildNote(sPlayerbotAIConfig.rosterGuildNote, sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), bot->GetGuildId() != 0))
		return false;

	time_t const now = time(nullptr);
	if (!roster_guild::IsDue(now, lastNoteCheck, roster_guild::NoteRefreshInterval(sPlayerbotAIConfig.rosterGuildNoteRefreshSeconds)))
		return false;

	lastNoteCheck = now;
	return true;
}

