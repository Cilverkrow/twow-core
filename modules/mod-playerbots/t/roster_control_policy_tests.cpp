#include "RosterControlPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

ai::roster_control::Request OwnGroupedBot()
{
    ai::roster_control::Request r;
    r.issuerIsRealPlayer = true;
    r.botOnline = true;
    r.botIsRosterOrRandom = true;
    r.botHasMaster = true;
    r.masterIsIssuer = true;
    r.sameGroup = true;
    return r;
}
}

int main()
{
    using namespace ai::roster_control;

    Require(Decide(OwnGroupedBot()) == Decision::ALLOW, "the master may direct its grouped roster bot");

    Request r = OwnGroupedBot();
    r.issuerIsRealPlayer = false;
    Require(Decide(r) == Decision::DENY_NOT_REAL_PLAYER, "a bot cannot direct another bot");

    r = OwnGroupedBot();
    r.botOnline = false;
    Require(Decide(r) == Decision::DENY_BOT_OFFLINE, "an offline bot is refused, not dereferenced");

    r = OwnGroupedBot();
    r.botIsRosterOrRandom = false;
    Require(Decide(r) == Decision::DENY_NOT_ELIGIBLE, "a player character is never directed");

    r = OwnGroupedBot();
    r.botHasMaster = false;
    r.masterIsIssuer = false;
    Require(Decide(r) == Decision::DENY_NO_MASTER, "unknown ownership fails closed");

    r = OwnGroupedBot();
    r.masterIsIssuer = false;
    Require(Decide(r) == Decision::DENY_NOT_YOUR_BOT, "another player's bot is refused");

    r = OwnGroupedBot();
    r.sameGroup = false;
    Require(Decide(r) == Decision::DENY_NOT_IN_GROUP, "a master outside the group is refused");

    for (Decision d : {Decision::ALLOW, Decision::DENY_NOT_REAL_PLAYER, Decision::DENY_BOT_OFFLINE,
                       Decision::DENY_NOT_ELIGIBLE, Decision::DENY_NO_MASTER, Decision::DENY_NOT_YOUR_BOT,
                       Decision::DENY_NOT_IN_GROUP})
    {
        Require(ReasonCode(d) && *ReasonCode(d), "every decision has a reason code");
        Require(ReasonText(d) && *ReasonText(d), "every decision has a reason text");
    }

    for (char const* cmd : {"gear", "equip", "init", "levelup", "level", "random", "train", "learn",
                            "food", "potions", "consumes", "reagents", "prepare", "enchants", "ammo",
                            "pet", "delete", "debug", "c", "w", "cmd", "do", "test", "record", "read", "clear"})
        Require(IsGmOnlyBotCommand(cmd), "progression, identity and diagnostic tools are GM-only");
    for (char const* cmd : {"add", "login", "remove", "logout", "rm", "summon", "recall", "come", "always"})
        Require(!IsGmOnlyBotCommand(cmd), "login, logout and summon keep their own rules");

    Require(BotCommandAcceptsOfflineBot("add") && BotCommandAcceptsOfflineBot("login") &&
            BotCommandAcceptsOfflineBot("always") && BotCommandAcceptsOfflineBot("delete"),
            "handlers that resolve the character themselves accept offline names");
    Require(!BotCommandAcceptsOfflineBot("gear") && !BotCommandAcceptsOfflineBot("summon") &&
            !BotCommandAcceptsOfflineBot("remove"), "everything else needs the bot in the world");

    Require(IsRndbotCommandAllowedForPlayer("") && IsRndbotCommandAllowedForPlayer("help") &&
            IsRndbotCommandAllowedForPlayer("help commands"), "players may read rndbot help");
    for (char const* cmd : {"reset", "update", "stats", "init A", "remove A", "clean map", "roster status", "helpx"})
        Require(!IsRndbotCommandAllowedForPlayer(cmd), "every other rndbot command is GM-only");

    Require(IsGmBypass(3, 3), "the owner's GM 3 account keeps admin control by default");
    Require(IsGmBypass(4, 3) && IsGmBypass(6, 3), "higher ranks keep it too");
    Require(!IsGmBypass(0, 3) && !IsGmBypass(2, 3), "players and moderators do not");
    Require(!IsGmBypass(6, 0) && !IsGmBypass(0, 0), "0 switches the bypass off instead of opening it to everyone");
    Require(!IsGmBypass(3, 4), "a stricter threshold is honoured");

    Require(IsPlainName("RNDBOT12") && IsPlainName("Farley"), "plain names pass");
    for (char const* name : {"", "a'b", "x OR 1=1", "a;b", "a\\b", "name%", "a b"})
        Require(!IsPlainName(name), "anything but letters, digits and _ is kept out of SQL");
    Require(!IsPlainName(std::string(33, 'a')), "overlong names are kept out of SQL");
    return 0;
}
