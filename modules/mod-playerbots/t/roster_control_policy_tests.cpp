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

    // #292 summon safety and cooldown.
    SummonState safe;
    safe.now = 1000;
    Require(CheckSummon(safe) == SummonBlock::NONE, "a safe summon goes through");
    SummonState s = safe; s.inCombat = true;
    Require(CheckSummon(s) == SummonBlock::IN_COMBAT, "no summon in combat");
    s = safe; s.battleground = true;
    Require(CheckSummon(s) == SummonBlock::BATTLEGROUND, "no summon around battlegrounds");
    s = safe; s.instance = true;
    Require(CheckSummon(s) == SummonBlock::INSTANCE, "no summon into or out of a dungeon");
    s = safe; s.taxi = true;
    Require(CheckSummon(s) == SummonBlock::TAXI, "no summon on a flight path");
    s = safe; s.transport = true;
    Require(CheckSummon(s) == SummonBlock::TRANSPORT, "no summon on a boat or zeppelin");
    s = safe; s.dead = true;
    Require(CheckSummon(s) == SummonBlock::DEAD, "no summon while dead");
    s = safe; s.teleporting = true;
    Require(CheckSummon(s) == SummonBlock::TELEPORTING, "no summon mid-teleport");
    s = safe; s.cooldownUntil = SummonCooldownUntil(900, 300);
    Require(CheckSummon(s) == SummonBlock::COOLDOWN, "a second summon within the cooldown is refused");
    s = safe; s.cooldownUntil = SummonCooldownUntil(700, 300);
    Require(CheckSummon(s) == SummonBlock::NONE, "the cooldown ends");
    s = safe; s.cooldownUntil = SummonCooldownUntil(1000, 0);
    Require(CheckSummon(s) == SummonBlock::NONE, "cooldown 0 means no cooldown");
    for (SummonBlock b : {SummonBlock::NONE, SummonBlock::IN_COMBAT, SummonBlock::BATTLEGROUND, SummonBlock::INSTANCE,
                          SummonBlock::TAXI, SummonBlock::TRANSPORT, SummonBlock::DEAD, SummonBlock::TELEPORTING,
                          SummonBlock::COOLDOWN})
        Require(*SummonBlockCode(b) && *SummonBlockText(b), "every summon block has a code and a text");

    // #292 leave.
    Require(MayDismiss(false, true, false, false), "the master may send its bot away");
    Require(MayDismiss(false, false, true, false), "the group leader may send a bot away");
    Require(MayDismiss(true, false, false, false), "the bot may leave by itself");
    Require(MayDismiss(false, false, false, true), "a GM may send a bot away");
    Require(!MayDismiss(false, false, false, false), "a bystander may not");
    return 0;
}
