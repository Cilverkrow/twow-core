#pragma once

#include <set>
#include <string>

namespace ai::roster_control
{
// #354/#292: who may direct a bot without GM rank. One decision for every
// player-facing bot command, so the checks cannot drift apart per command.
// GM rank is a separate path and never a fallback of this one.

enum class Decision
{
    ALLOW,
    DENY_NOT_REAL_PLAYER,   // issuer is a bot or has no session
    DENY_BOT_OFFLINE,       // bot not in world or without AI
    DENY_NOT_ELIGIBLE,      // neither a persistent roster bot nor a random bot
    DENY_NO_MASTER,         // ownership unknown: fail closed
    DENY_NOT_YOUR_BOT,      // bot follows someone else
    DENY_NOT_IN_GROUP,      // master, but not grouped with the issuer
};

struct Request
{
    bool issuerIsRealPlayer = false;
    bool botOnline = false;
    bool botIsRosterOrRandom = false;
    bool botHasMaster = false;
    bool masterIsIssuer = false;
    bool sameGroup = false;
};

inline Decision Decide(Request const& r)
{
    if (!r.issuerIsRealPlayer)
        return Decision::DENY_NOT_REAL_PLAYER;
    if (!r.botOnline)
        return Decision::DENY_BOT_OFFLINE;
    if (!r.botIsRosterOrRandom)
        return Decision::DENY_NOT_ELIGIBLE;
    if (!r.botHasMaster)
        return Decision::DENY_NO_MASTER;
    if (!r.masterIsIssuer)
        return Decision::DENY_NOT_YOUR_BOT;
    if (!r.sameGroup)
        return Decision::DENY_NOT_IN_GROUP;
    return Decision::ALLOW;
}

inline char const* ReasonCode(Decision d)
{
    switch (d)
    {
        case Decision::ALLOW:                return "ok";
        case Decision::DENY_NOT_REAL_PLAYER: return "not_real_player";
        case Decision::DENY_BOT_OFFLINE:     return "bot_offline";
        case Decision::DENY_NOT_ELIGIBLE:    return "not_roster";
        case Decision::DENY_NO_MASTER:       return "ambiguous";
        case Decision::DENY_NOT_YOUR_BOT:    return "not_your_bot";
        case Decision::DENY_NOT_IN_GROUP:    return "not_in_group";
    }
    return "ambiguous";
}

inline char const* ReasonText(Decision d)
{
    switch (d)
    {
        case Decision::ALLOW:                return "ok";
        case Decision::DENY_NOT_REAL_PLAYER: return "Only a player can direct this bot.";
        case Decision::DENY_BOT_OFFLINE:     return "Bot is offline.";
        case Decision::DENY_NOT_ELIGIBLE:    return "This bot cannot be directed by players.";
        case Decision::DENY_NO_MASTER:       return "This bot isn't yours. Invite it to your group first.";
        case Decision::DENY_NOT_YOUR_BOT:    return "This bot isn't yours.";
        case Decision::DENY_NOT_IN_GROUP:    return "This bot must be in your group.";
    }
    return "This bot isn't yours.";
}

// Explicit GM bypass (AiPlayerbot.RosterControl.GmMinSecurity). The module's
// SEC_GAMEMASTER is SEC_ADMINISTRATOR (4), so the owner's GM 3 account needs
// its own threshold. 0 turns the bypass off: fail closed, never "everyone".
inline bool IsGmBypass(unsigned int accountSecurity, unsigned int gmMinSecurity)
{
    return gmMinSecurity > 0 && accountSecurity >= gmMinSecurity;
}

// `.bot <cmd>` subcommands that change a bot's gear, items, spells, level or
// identity, or run diagnostics in its name. They are GM tools: a player who
// could run them would skip the self-earned progression (ADR-0031).
inline bool IsGmOnlyBotCommand(std::string const& cmd)
{
    static std::set<std::string> const gmOnly = {
        "gear", "equip", "train", "learn", "food", "drink", "potions", "pots",
        "consumes", "consumables", "consums", "regs", "reg", "reagents",
        "prepare", "prep", "init", "enchants", "ammo", "pet", "levelup", "level",
        "random", "delete", "debug", "c", "w", "cmd", "test", "do", "record",
        "read", "clear",
    };
    return gmOnly.count(cmd) > 0;
}

// Subcommands whose handler resolves the character itself and may run while
// it is offline. Every other handler needs the bot in the world.
inline bool BotCommandAcceptsOfflineBot(std::string const& cmd)
{
    return cmd == "add" || cmd == "login" || cmd == "always" || cmd == "delete";
}

// `.rndbot` acts on the whole random/roster population; only its help text is
// harmless for players.
inline bool IsRndbotCommandAllowedForPlayer(std::string const& args)
{
    return args.empty() || args == "help" || args.rfind("help ", 0) == 0;
}

// Account or character names reach a logon database query. Names in this
// realm are letters and digits only; anything else is rejected, not escaped.
inline bool IsPlainName(std::string const& name)
{
    if (name.empty() || name.size() > 32)
        return false;
    for (char c : name)
    {
        bool const letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool const digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_')
            return false;
    }
    return true;
}
}
