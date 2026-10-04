#pragma once

#include <cstdint>
#include <ctime>

namespace ai::guild_poach
{
// twow-repo#485, owner decision 5 (03.10.): "Abwerb-Hook: Ein Bot nimmt die Einladung bzw. Urkunde
// eines Spielers an und wechselt". With 45 bots per guild every roster bot is in a bot guild, so a
// real player could neither invite a bot nor collect signatures for a charter (the core answers
// ERR_ALREADY_IN_GUILD_S). Draft v2 section 5.1 "Spieler gruenden immer, Abwerben":
//   - only a real player of the same faction invites;
//   - the player's guild is no bot guild (its guild master is not a roster bot);
//   - the bot is not the guild master of a bot guild;
//   - cooldown per bot (24 h); PlayerbotSecurity stays;
//   - a bot never leaves a player guild on its own.
// The core asks the module (PlayerScript::CanSwitchGuild) before "already in a guild", the bot
// decides again when the invitation or charter arrives (GuildAcceptAction, PetitionSignAction), and
// GuildMgr::RequestGuildSwitch leaves and joins on the world thread. Plain values only; the game
// code fills them from copies (GuildMgr::GetGuildSummary, GetPlayerGuildId, the player cache).

struct PoachInput
{
    bool enabled = false;                   // AiPlayerbot.RosterGuild.AllowPoaching
    bool rosterBot = false;                 // the invitee is a persistent roster bot
    bool inviterRealPlayer = false;         // a person at a client, not a bot
    bool sameFaction = false;               // invitee and inviter (charter: the charter's team)
    std::uint32_t currentGuildId = 0;       // the invitee's guild, 0 = none
    bool currentGuildIsBotGuild = false;    // its guild master is a roster bot
    bool leadsCurrentGuild = false;         // the invitee is that guild master
    std::uint32_t targetGuildId = 0;        // the inviting guild; 0 = a charter
    bool targetIsBotGuild = false;          // invitation: its guild master is a roster bot
    bool charterOfInviter = false;          // charter: the inviter owns it (a new player guild)
    std::time_t now = 0;
    std::time_t lastSwitch = 0;             // GuildMgr::GetLastGuildSwitch, 0 = none
    std::uint32_t cooldownSeconds = 0;      // as used (PoachCooldown)
};

enum class PoachDecision
{
    Allow,
    Disabled,           // AllowPoaching = 0: the core's answer ("already in a guild")
    NotRosterBot,
    NotInGuild,         // no switch needed: the normal invite / sign path applies
    InviterNotReal,     // bots never poach
    Faction,
    PlayerGuild,        // the bot is in a player guild and never leaves it on its own
    GuildMaster,        // the guild master of a bot guild stays
    SameGuild,
    TargetBotGuild,     // from one bot guild to another: the roster plan decides that, not a player
    NotOwnCharter,      // a charter the inviter does not own
    Cooldown,
};

// AiPlayerbot.RosterGuild.PoachCooldownSeconds as used: 1 h to 30 days. 0 would allow ping-pong
// between two players, a negative config value arrives as 4294967295.
inline std::uint32_t PoachCooldown(std::uint32_t configuredSeconds)
{
    std::uint32_t const minimum = 3600;
    std::uint32_t const maximum = 30u * 86400u;
    return configuredSeconds < minimum ? minimum : (configuredSeconds > maximum ? maximum : configuredSeconds);
}

inline PoachDecision DecidePoach(PoachInput const& in)
{
    if (!in.enabled)
        return PoachDecision::Disabled;
    if (!in.rosterBot)
        return PoachDecision::NotRosterBot;
    if (!in.currentGuildId)
        return PoachDecision::NotInGuild;
    if (!in.inviterRealPlayer)
        return PoachDecision::InviterNotReal;
    if (!in.sameFaction)
        return PoachDecision::Faction;
    if (!in.currentGuildIsBotGuild)
        return PoachDecision::PlayerGuild;
    if (in.leadsCurrentGuild)
        return PoachDecision::GuildMaster;
    if (in.targetGuildId)
    {
        if (in.targetGuildId == in.currentGuildId)
            return PoachDecision::SameGuild;
        if (in.targetIsBotGuild)
            return PoachDecision::TargetBotGuild;
    }
    else if (!in.charterOfInviter)
        return PoachDecision::NotOwnCharter;
    if (in.lastSwitch && in.now < in.lastSwitch + std::time_t(in.cooldownSeconds))
        return PoachDecision::Cooldown;
    return PoachDecision::Allow;
}

inline char const* PoachDecisionName(PoachDecision decision)
{
    switch (decision)
    {
        case PoachDecision::Allow: return "allow";
        case PoachDecision::Disabled: return "disabled";
        case PoachDecision::NotRosterBot: return "not_roster_bot";
        case PoachDecision::NotInGuild: return "not_in_guild";
        case PoachDecision::InviterNotReal: return "inviter_not_real";
        case PoachDecision::Faction: return "faction";
        case PoachDecision::PlayerGuild: return "player_guild";
        case PoachDecision::GuildMaster: return "guild_master";
        case PoachDecision::SameGuild: return "same_guild";
        case PoachDecision::TargetBotGuild: return "target_bot_guild";
        case PoachDecision::NotOwnCharter: return "not_own_charter";
        case PoachDecision::Cooldown: return "cooldown";
    }
    return "unknown";
}

// Refusals worth a trace line: the switch is on and a roster bot in a guild was asked. Disabled,
// other characters and guildless bots (normal path) stay silent.
inline bool IsTracedRefusal(PoachDecision decision)
{
    return decision != PoachDecision::Allow && decision != PoachDecision::Disabled &&
        decision != PoachDecision::NotRosterBot && decision != PoachDecision::NotInGuild;
}

// No ping-pong on the charter path: after a switch by charter the bot is guildless and its only tie
// is its signature on the player's charter, so DecidePoach (guild members only) no longer guards it.
// Within the cooldown that signature stays: the bot signs no other charter (the roster sign path,
// roster_guild::DecideSign, accepts every real player's charter and moves to fuller bot charters),
// takes no guild invitation and buys no charter of its own. Neutral for every bot that has not
// switched since the server start (lastSwitch 0) or signs no charter; the same charter again is no
// move. signsCharter: the bot's signature is on an open charter; offeredIsSigned: the offered
// charter is that one.
inline bool KeepsPoachedCharter(bool signsCharter, bool offeredIsSigned, std::time_t now, std::time_t lastSwitch,
    std::uint32_t cooldownSeconds)
{
    if (!signsCharter || offeredIsSigned || !lastSwitch)
        return false;
    return now < lastSwitch + std::time_t(cooldownSeconds);
}
}
