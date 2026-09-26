#ifndef _PlayerbotSecurity_H
#define _PlayerbotSecurity_H

#include "RosterControlPolicy.h"

enum class PlayerbotSecurityLevel : uint8
{
    PLAYERBOT_SECURITY_DENY_ALL = 0,
    PLAYERBOT_SECURITY_TALK = 1,
    PLAYERBOT_SECURITY_GUILD = 2, //Intermediate level: Bot can't be invited to group but can accept guild invites.
    PLAYERBOT_SECURITY_INVITE = 3,
    PLAYERBOT_SECURITY_ALLOW_ALL = 4
};

enum class DenyReason : uint8
{
    PLAYERBOT_DENY_NONE,
    PLAYERBOT_DENY_LOW_LEVEL,
    PLAYERBOT_DENY_GEARSCORE,
    PLAYERBOT_DENY_NOT_YOURS,
    PLAYERBOT_DENY_IS_BOT,
    PLAYERBOT_DENY_OPPOSING,
    PLAYERBOT_DENY_DEAD,
    PLAYERBOT_DENY_FAR,
    PLAYERBOT_DENY_INVITE,
    PLAYERBOT_DENY_FULL_GROUP,
    PLAYERBOT_DENY_NOT_LEADER,
    PLAYERBOT_DENY_IS_LEADER,
    PLAYERBOT_DENY_BG,
    PLAYERBOT_DENY_LFG
};

class PlayerbotSecurity
{
    public:
        PlayerbotSecurity(Player* const bot);

	public:
        PlayerbotSecurityLevel LevelFor(Player* from, DenyReason* reason = NULL, bool ignoreGroup = false);
        bool CheckLevelFor(PlayerbotSecurityLevel level, bool silent, Player* from, bool ignoreGroup = false);

    private:
        Player* const bot;
        uint32 account;
        std::map<uint64, std::map<std::string, time_t> > whispers;
};

// #354/#292: may `issuer` direct `bot` without GM rank? Fills the pure
// RosterControlPolicy request from live state; the decision itself lives there.
ai::roster_control::Decision DecideRosterControl(Player* issuer, Player* bot);

// #292: may a directed bot be summoned right now (safe places, cooldown)?
// MarkRosterSummon starts the cooldown after a summon went through.
ai::roster_control::SummonBlock CheckRosterSummon(Player* issuer, Player* bot);
void MarkRosterSummon(Player* bot);

#endif
