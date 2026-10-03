#pragma once

#include <cstdint>
#include <ctime>
#include <set>
#include <string>
#include <vector>

namespace ai::roster_guild
{
// twow-repo#485: guilds of persistent roster bots. Train 8 founded no guild at all: the bots counted
// signatures by the charter's item guid while vmangos keys them by its own petition id, so every
// charter looked empty, offers ran without end and signatures moved between bot charters (88 % of
// 3,122 signatures on v24). These rules decide the new path from plain counts; the game code reads
// the counts as copies from GuildMgr (PetitionSummary / GuildSummary).

// The new path: AiPlayerbot.RosterGuild.BotsPerGuild > 0, a persistent roster bot, no real master.
// BotsPerGuild = 0 (default) keeps the stock petition behaviour.
inline bool UsesRosterPath(std::uint32_t botsPerGuild, bool rosterMember, bool realPlayerMaster)
{
    return botsPerGuild && rosterMember && !realPlayerMaster;
}

// Guild target of one faction: ceil(roster bots of the faction / bots per guild). The configured
// roster counts, not the online bots (login waves would move the target).
inline std::uint32_t TargetGuilds(std::uint32_t rosterBots, std::uint32_t botsPerGuild)
{
    return botsPerGuild ? (rosterBots + botsPerGuild - 1) / botsPerGuild : 0;
}

// A roster bot buys a charter only while the faction's bot guilds plus open bot charters stay below
// the target (owner 02.10.: the server sells no more charters to bots once the target is covered).
inline bool MayBuyCharter(std::uint32_t target, std::uint32_t guilds, std::uint32_t openCharters)
{
    return target > guilds + openCharters;
}

// Founding, offering and signing bot charters only below the target.
inline bool MayFound(std::uint32_t target, std::uint32_t guilds)
{
    return guilds < target;
}

// Critic B5.3: owners of complete charters reach a guild master on different map threads at the
// same time (4 alliance charters had 9 signatures at target 2). A founding takes a slot first;
// guilds plus slots in flight never exceed the target.
struct FoundingLedger
{
    std::uint32_t guilds = 0;      // bot guilds of the faction
    std::uint32_t reserved = 0;    // foundings in flight (slot taken, turn-in not finished)
};

inline bool TryReserveFounding(std::uint32_t target, FoundingLedger& ledger)
{
    if (!MayFound(target, ledger.guilds + ledger.reserved))
        return false;
    ++ledger.reserved;
    return true;
}

// Gives the slot back; a successful founding turns it into a guild.
inline void FinishFounding(FoundingLedger& ledger, bool founded)
{
    if (ledger.reserved)
        --ledger.reserved;
    if (founded)
        ++ledger.guilds;
}

enum class SignDecision
{
    Accept,
    DeclineFaction,         // AllowTwoSide.Interaction.Guild = 1 live: the core would let it through
    DeclineTargetReached,
    DeclineNotBetter,       // the bot's signature stays on a charter at least as full
};

// currentSignedCount: signatures of the charter the bot has signed, -1 = none. A signature moves
// only to a fuller charter, which ends the back and forth between bot charters.
inline SignDecision DecideSign(bool inviterRealPlayer, bool sameFaction, bool targetReached, int currentSignedCount,
    std::uint32_t offeredCount)
{
    if (!sameFaction)
        return SignDecision::DeclineFaction;
    if (inviterRealPlayer)              // owner 02.10.: players can always found a guild
        return SignDecision::Accept;
    if (targetReached)
        return SignDecision::DeclineTargetReached;
    if (currentSignedCount < 0)
        return SignDecision::Accept;
    return offeredCount > std::uint32_t(currentSignedCount) ? SignDecision::Accept : SignDecision::DeclineNotBetter;
}

inline char const* SignDecisionName(SignDecision decision)
{
    switch (decision)
    {
        case SignDecision::Accept: return "accept";
        case SignDecision::DeclineFaction: return "faction";
        case SignDecision::DeclineTargetReached: return "target_reached";
        case SignDecision::DeclineNotBetter: return "not_better";
    }
    return "unknown";
}

// Approved names from the config: comma separated, optionally in quotes; blanks and quotes around
// a name are dropped, empty entries skipped.
inline std::vector<std::string> SplitNames(std::string const& csv)
{
    std::vector<std::string> names;
    std::string::size_type start = 0;
    while (start <= csv.size())
    {
        std::string::size_type end = csv.find(',', start);
        if (end == std::string::npos)
            end = csv.size();
        std::string const item = csv.substr(start, end - start);
        std::string::size_type const first = item.find_first_not_of(" \t\"");
        if (first != std::string::npos)
            names.push_back(item.substr(first, item.find_last_not_of(" \t\"") - first + 1));
        start = end + 1;
    }
    return names;
}

// Letters and spaces, 1-24 characters: MAX_CHARTER_NAME with StrictCharterNames = 1 (live). Game
// code asks the core (ObjectMgr::IsValidCharterName, IsReservedName) - it stays authoritative.
inline bool IsValidCharterName(std::string const& name)
{
    if (name.empty() || name.size() > 24)
        return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ' '))
            return false;
    return true;
}

inline bool IsApproved(std::vector<std::string> const& approved, std::string const& name)
{
    for (std::string const& candidate : approved)
        if (candidate == name)
            return true;
    return false;
}

// First approved name that is not used and that accept() takes; "" when none is left.
template <class Accept>
inline std::string PickName(std::vector<std::string> const& approved, std::set<std::string> const& used, Accept accept)
{
    for (std::string const& name : approved)
        if (!used.count(name) && accept(name))
            return name;
    return std::string();
}

inline std::string PickName(std::vector<std::string> const& approved, std::set<std::string> const& used)
{
    return PickName(approved, used, [](std::string const& name) { return IsValidCharterName(name); });
}

// Throttle for repeated [RosterGuild] lines and retries (last = 0: never done).
inline bool IsDue(std::time_t now, std::time_t last, std::uint32_t intervalSeconds)
{
    return last == 0 || now >= last + std::time_t(intervalSeconds);
}

// AiPlayerbot.RosterGuild.SnapshotSeconds as used: 10-3600. 0 would recount on every call under the
// plan's mutex; a negative config value arrives as 4294967295 and would freeze the counts.
inline std::uint32_t SnapshotInterval(std::uint32_t configuredSeconds)
{
    return configuredSeconds < 10 ? 10 : (configuredSeconds > 3600 ? 3600 : configuredSeconds);
}
}
