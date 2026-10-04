#include "GuildPoachPolicy.h"

#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <iostream>
#include <string>

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

// A roster bot of bot guild 7 (not its guild master), invited by a real player of the same faction
// into his player guild 9, no switch so far: the case owner decision 5 asks for.
ai::guild_poach::PoachInput Invitation()
{
    ai::guild_poach::PoachInput in;
    in.enabled = true;
    in.rosterBot = true;
    in.inviterRealPlayer = true;
    in.sameFaction = true;
    in.currentGuildId = 7;
    in.currentGuildIsBotGuild = true;
    in.leadsCurrentGuild = false;
    in.targetGuildId = 9;
    in.targetIsBotGuild = false;
    in.now = 1000000;
    in.lastSwitch = 0;
    in.cooldownSeconds = 86400;
    return in;
}

// The same bot, offered the player's own charter (a guild he is founding).
ai::guild_poach::PoachInput Charter()
{
    ai::guild_poach::PoachInput in = Invitation();
    in.targetGuildId = 0;
    in.charterOfInviter = true;
    return in;
}
}

int main()
{
    using namespace ai::guild_poach;

    Require(DecidePoach(Invitation()) == PoachDecision::Allow, "a real player recruits a bot from a bot guild");
    Require(DecidePoach(Charter()) == PoachDecision::Allow, "a real player's charter recruits a bot from a bot guild");

    // Default: the switch is off, the core's "already in a guild" stays.
    PoachInput in = Invitation();
    in.enabled = false;
    Require(DecidePoach(in) == PoachDecision::Disabled, "AllowPoaching = 0 refuses");
    Require(DecidePoach(PoachInput()) == PoachDecision::Disabled, "a default input refuses");

    in = Invitation();
    in.rosterBot = false;
    Require(DecidePoach(in) == PoachDecision::NotRosterBot, "only persistent roster bots");

    in = Invitation();
    in.currentGuildId = 0;
    Require(DecidePoach(in) == PoachDecision::NotInGuild, "a guildless bot takes the normal path");

    // Only a real player of the same faction poaches.
    in = Invitation();
    in.inviterRealPlayer = false;
    Require(DecidePoach(in) == PoachDecision::InviterNotReal, "bots never poach");
    in = Charter();
    in.inviterRealPlayer = false;
    Require(DecidePoach(in) == PoachDecision::InviterNotReal, "a bot's charter never poaches");
    in = Invitation();
    in.sameFaction = false;
    Require(DecidePoach(in) == PoachDecision::Faction, "other faction refused (AllowTwoSide.Interaction.Guild = 1 live)");

    // A bot never leaves a player guild on its own - this also ends ping-pong between two players.
    in = Invitation();
    in.currentGuildIsBotGuild = false;
    Require(DecidePoach(in) == PoachDecision::PlayerGuild, "a bot in a player guild stays");
    in = Charter();
    in.currentGuildIsBotGuild = false;
    Require(DecidePoach(in) == PoachDecision::PlayerGuild, "a bot in a player guild signs no other charter");

    // The guild master of a bot guild stays (the core would refuse the leave anyway).
    in = Invitation();
    in.leadsCurrentGuild = true;
    Require(DecidePoach(in) == PoachDecision::GuildMaster, "bot guild master stays");

    // The player's guild is no bot guild; the own guild is no switch.
    in = Invitation();
    in.targetIsBotGuild = true;
    Require(DecidePoach(in) == PoachDecision::TargetBotGuild, "no switch from one bot guild to another");
    in = Invitation();
    in.targetGuildId = in.currentGuildId;
    Require(DecidePoach(in) == PoachDecision::SameGuild, "an invitation into the own guild is no switch");
    in = Charter();
    in.charterOfInviter = false;
    Require(DecidePoach(in) == PoachDecision::NotOwnCharter, "only the inviter's own charter");

    // Cooldown: one switch per bot and PoachCooldownSeconds.
    in = Invitation();
    in.lastSwitch = in.now - 86399;
    Require(DecidePoach(in) == PoachDecision::Cooldown, "second switch within 24 h refused");
    in.lastSwitch = in.now - 86400;
    Require(DecidePoach(in) == PoachDecision::Allow, "after 24 h the bot may switch again");
    in.lastSwitch = in.now;
    Require(DecidePoach(in) == PoachDecision::Cooldown, "the switch of this second counts");

    // Order: a disabled switch says nothing more, the player guild rule beats the cooldown.
    in = Invitation();
    in.enabled = false;
    in.currentGuildIsBotGuild = false;
    Require(DecidePoach(in) == PoachDecision::Disabled, "disabled first");
    in = Invitation();
    in.currentGuildIsBotGuild = false;
    in.lastSwitch = in.now;
    Require(DecidePoach(in) == PoachDecision::PlayerGuild, "player guild before cooldown");

    // No ping-pong on the charter path: a bot that switched by charter is guildless (DecidePoach no
    // longer applies) and keeps its signature within the cooldown.
    std::time_t const now = 1000000;
    Require(KeepsPoachedCharter(true, false, now, now - 60, 86400), "a second player's charter right after the switch is refused");
    Require(KeepsPoachedCharter(true, false, now, now - 86399, 86400), "kept within 24 h");
    Require(!KeepsPoachedCharter(true, false, now, now - 86400, 86400), "after 24 h the normal sign path applies");
    Require(!KeepsPoachedCharter(true, true, now, now - 60, 86400), "the same charter again is no move");
    Require(!KeepsPoachedCharter(false, false, now, now - 60, 86400), "no signature: nothing to keep (charter turned in or gone)");
    Require(!KeepsPoachedCharter(true, false, now, 0, 86400), "neutral for bots without a switch");
    Require(KeepsPoachedCharter(true, false, now, now, PoachCooldown(0)), "the clamped minimum cooldown still keeps it");

    // Cooldown as used: 1 h to 30 days.
    Require(PoachCooldown(0) == 3600, "0 is clamped to 1 h (no ping-pong)");
    Require(PoachCooldown(86400) == 86400, "default 24 h kept");
    Require(PoachCooldown(4294967295u) == 30u * 86400u, "a negative config value is clamped to 30 days");

    // Trace only real refusals of roster bots.
    Require(!IsTracedRefusal(PoachDecision::Allow) && !IsTracedRefusal(PoachDecision::Disabled) &&
        !IsTracedRefusal(PoachDecision::NotRosterBot) && !IsTracedRefusal(PoachDecision::NotInGuild), "quiet decisions");
    Require(IsTracedRefusal(PoachDecision::Cooldown) && IsTracedRefusal(PoachDecision::PlayerGuild) &&
        IsTracedRefusal(PoachDecision::GuildMaster), "traced refusals");

    // Every decision has a name for the trace.
    for (PoachDecision decision : { PoachDecision::Allow, PoachDecision::Disabled, PoachDecision::NotRosterBot,
             PoachDecision::NotInGuild, PoachDecision::InviterNotReal, PoachDecision::Faction, PoachDecision::PlayerGuild,
             PoachDecision::GuildMaster, PoachDecision::SameGuild, PoachDecision::TargetBotGuild,
             PoachDecision::NotOwnCharter, PoachDecision::Cooldown })
        Require(std::string(PoachDecisionName(decision)) != "unknown", "decision named");

    std::cout << "guild_poach_policy_tests passed\n";
    return 0;
}
