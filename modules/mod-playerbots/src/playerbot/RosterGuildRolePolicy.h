#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ai::roster_guild_role
{
// twow-repo#485 / #518: fill roster guilds by role. The split is configuration
// (AiPlayerbot.RosterGuild.Tanks/Healers/Dps per guild, 0/0/0 = no role quota = the behaviour of
// core#281; the config default is the owner's 7/10/28 of 04.10., active only with BotsPerGuild > 0).
// The deal follows
// OB-40's planner twow-repo PR #519 deploy/roster/plan-360/guild_plan.py: per role (tanks, healers,
// DPS), class by class with the rarest class first, then level band, race and ordinal; members that
// already have a guild keep it (--keep); a new member goes to the open guild with the fewest of its
// class in that role, then the fewest of its role, then the fewest of its level band, then the
// first guild. This deal holds for every role with or without switches, so with the switches off
// the core deals exactly as guild_plan.py. Three switches, default off, only add hard limits
// (assignment v3):
//   HealerClassMin  - healers: class spread, and healer slots stay free for every healer class of
//                     the faction that the guild still lacks (buffs, dispels);
//   TankClassSpread - tanks: class spread, and at most ceil(tanks of the class / guilds) per guild;
//   RareComboSpread - every race x class pair at most ceil(pair count / guilds) per guild, so rare
//                     pairs (dwarf shaman, undead paladin, druids) land in different guilds.
// DPS are dealt class by class as in guild_plan.py. Two owner rules (assignment v3, 04.10.), empty
// (off) in the structs here, set by the config defaults:
//   tankMix   - per tank class a minimum and an optional maximum per guild (owner: 2-3 warriors and one
//               bear, rogue, paladin and shaman tank each, as far as the faction has them); a minimum
//               keeps tank slots free only while the faction still has an unplaced tank of that class;
//   rarePairs - listed race x class pairs (dwarf shaman, dwarf warlock, undead paladin) at most
//               rarePairCap per guild (owner: about 2.5 % per pair, so 1 in a guild of 45).
// Pure code, std only.

enum class Role : std::uint8_t
{
    Unknown = 0,
    Tank = 1,
    Healer = 2,
    Dps = 3,
};

inline char const* RoleName(Role role)
{
    switch (role)
    {
        case Role::Tank: return "TANK";
        case Role::Healer: return "HEALER";
        case Role::Dps: return "DPS";
        case Role::Unknown: break;
    }
    return "UNKNOWN";
}

// The role column of guilds.tsv / the roster plan (TANK, HEALER, DPS).
inline Role ParseRole(std::string const& text)
{
    if (text == "TANK")
        return Role::Tank;
    if (text == "HEALER")
        return Role::Healer;
    if (text == "DPS")
        return Role::Dps;
    return Role::Unknown;
}

// From the bot's own spec (AiFactory::GetPlayerRoles bits: tank 0x01, healer 0x02, dps 0x04). A bot
// without spent talents has no role yet.
inline Role RoleFromBits(std::uint32_t bits, bool hasTalents)
{
    if (!hasTalents)
        return Role::Unknown;
    if (bits & 0x01)
        return Role::Tank;
    if (bits & 0x02)
        return Role::Healer;
    return Role::Dps;
}

struct Quota
{
    std::uint32_t tanks = 0;
    std::uint32_t healers = 0;
    std::uint32_t dps = 0;
};

// 0/0/0 (default) = no role quota. With any value set, a role at 0 gets no slot.
inline bool QuotaActive(Quota const& quota)
{
    return quota.tanks || quota.healers || quota.dps;
}

inline std::uint32_t QuotaFor(Quota const& quota, Role role)
{
    switch (role)
    {
        case Role::Tank: return quota.tanks;
        case Role::Healer: return quota.healers;
        case Role::Dps: return quota.dps;
        case Role::Unknown: break;
    }
    return 0;
}

typedef std::pair<Role, std::uint8_t> RoleClass;
typedef std::pair<std::uint8_t, std::uint8_t> RaceClass;

// Tank class -> (minimum, maximum) per guild; maximum 0 = no cap.
typedef std::map<std::uint8_t, std::pair<std::uint32_t, std::uint32_t>> TankMix;

struct Switches
{
    bool healerClassMin = false;
    bool tankClassSpread = false;
    bool rareComboSpread = false;
    TankMix tankMix;                    // empty = no tank class mix
    std::set<RaceClass> rarePairs;      // (race, class) pairs under rarePairCap
    std::uint32_t rarePairCap = 0;      // 0 = no cap
};

// Role fill only on the roster path (BotsPerGuild > 0, roster bot, no real master) and only with a
// quota or a plan file; otherwise everything stays as in core#281.
inline bool UsesRoleFill(bool rosterPath, Quota const& quota, bool planLoaded)
{
    return rosterPath && (QuotaActive(quota) || planLoaded);
}

struct Member
{
    std::uint32_t guid = 0;
    std::uint32_t ordinal = 0;     // plan ordinal, else the guid (deterministic tie-break)
    std::uint8_t cls = 0;
    std::uint8_t race = 0;
    std::uint32_t level = 1;
    Role role = Role::Unknown;
    std::uint32_t guild = 0;       // 0 = no guild
};

// 5-level bands as guild_plan.py: (level - 1) // 5.
inline std::uint32_t Band(std::uint32_t level)
{
    return level ? (level - 1) / 5 : 0;
}

// ceil(total / guilds): the most of one kind a guild takes when the kind is spread.
inline std::uint32_t SpreadCap(std::uint32_t total, std::uint32_t guilds)
{
    return guilds ? (total + guilds - 1) / guilds : total;
}

// Counts of one guild or one charter.
struct GuildCounts
{
    std::map<Role, std::uint32_t> role;
    std::map<RoleClass, std::uint32_t> roleClass;
    std::map<RaceClass, std::uint32_t> combo;
    std::map<std::pair<Role, std::uint32_t>, std::uint32_t> roleBand;

    std::uint32_t Get(Role r) const { auto it = role.find(r); return it == role.end() ? 0 : it->second; }
    std::uint32_t Get(RoleClass const& k) const { auto it = roleClass.find(k); return it == roleClass.end() ? 0 : it->second; }
    std::uint32_t GetCombo(RaceClass const& k) const { auto it = combo.find(k); return it == combo.end() ? 0 : it->second; }
    std::uint32_t GetBand(Role r, std::uint32_t band) const { auto it = roleBand.find(std::make_pair(r, band)); return it == roleBand.end() ? 0 : it->second; }
};

inline void AddToCounts(GuildCounts& counts, Member const& member)
{
    if (member.role == Role::Unknown)
        return;
    ++counts.role[member.role];
    ++counts.roleClass[RoleClass(member.role, member.cls)];
    ++counts.combo[RaceClass(member.race, member.cls)];
    ++counts.roleBand[std::make_pair(member.role, Band(member.level))];
}

// Faction totals the spread rules need.
struct FactionStats
{
    std::uint32_t guilds = 0;                                   // guild target of the faction
    std::map<RoleClass, std::uint32_t> roleClass;               // all members with a known role
    std::map<RaceClass, std::uint32_t> combo;
    std::map<std::uint8_t, std::uint32_t> unplacedHealers;      // healers without a guild, by class
    std::map<std::uint8_t, std::uint32_t> unplacedTanks;        // tanks without a guild, by class

    std::uint32_t Get(RoleClass const& k) const { auto it = roleClass.find(k); return it == roleClass.end() ? 0 : it->second; }
    std::uint32_t GetCombo(RaceClass const& k) const { auto it = combo.find(k); return it == combo.end() ? 0 : it->second; }
};

inline FactionStats MakeStats(std::vector<Member> const& members, std::uint32_t guilds)
{
    FactionStats stats;
    stats.guilds = guilds;
    for (Member const& member : members)
    {
        if (member.role == Role::Unknown)
            continue;
        ++stats.roleClass[RoleClass(member.role, member.cls)];
        ++stats.combo[RaceClass(member.race, member.cls)];
        if (member.role == Role::Healer && !member.guild)
            ++stats.unplacedHealers[member.cls];
        if (member.role == Role::Tank && !member.guild)
            ++stats.unplacedTanks[member.cls];
    }
    return stats;
}

// Tank slots the guild keeps free for the other mix classes still below their minimum, as far as the
// faction still has unplaced tanks of that class (owner: "soweit die Fraktion sie hat").
inline std::uint32_t MissingTankMix(GuildCounts const& counts, FactionStats const& stats, TankMix const& mix, std::uint8_t cls)
{
    std::uint32_t missing = 0;
    for (auto const& item : mix)
    {
        if (item.first == cls)
            continue;
        std::uint32_t const have = counts.Get(RoleClass(Role::Tank, item.first));
        if (have >= item.second.first)
            continue;
        auto const unplaced = stats.unplacedTanks.find(item.first);
        if (unplaced == stats.unplacedTanks.end())
            continue;
        missing += std::min(item.second.first - have, unplaced->second);
    }
    return missing;
}

// Healer classes of the faction (any healer of the class in the roster) the guild has none of,
// other than cls, and that still have a healer without a guild.
inline std::uint32_t MissingHealerClasses(GuildCounts const& counts, FactionStats const& stats, std::uint8_t cls)
{
    std::uint32_t missing = 0;
    for (auto const& item : stats.unplacedHealers)
        if (item.first != cls && item.second && !counts.Get(RoleClass(Role::Healer, item.first)))
            ++missing;
    return missing;
}

enum class Fit
{
    Ok,
    UnknownRole,
    RoleFull,
    HealerClassReserved,
    TankClassSpread,
    RareComboSpread,
    TankClassMix,
    RarePairCap,
};

inline char const* FitName(Fit fit)
{
    switch (fit)
    {
        case Fit::Ok: return "ok";
        case Fit::UnknownRole: return "unknown_role";
        case Fit::RoleFull: return "role_full";
        case Fit::HealerClassReserved: return "healer_class_reserved";
        case Fit::TankClassSpread: return "tank_class_spread";
        case Fit::RareComboSpread: return "rare_combo_spread";
        case Fit::TankClassMix: return "tank_class_mix";
        case Fit::RarePairCap: return "rare_pair_cap";
    }
    return "unknown";
}

// May the member join this guild (or sign this charter)? The role slot must be open; the switches
// add the spread rules.
inline Fit CheckFit(GuildCounts const& counts, Member const& member, Quota const& quota, Switches const& switches,
    FactionStats const& stats)
{
    if (member.role == Role::Unknown)
        return Fit::UnknownRole;

    std::uint32_t const inRole = counts.Get(member.role);
    if (inRole >= QuotaFor(quota, member.role))
        return Fit::RoleFull;

    if (switches.healerClassMin && member.role == Role::Healer &&
        inRole + 1 + MissingHealerClasses(counts, stats, member.cls) > quota.healers)
        return Fit::HealerClassReserved;

    RoleClass const roleClass(member.role, member.cls);
    if (switches.tankClassSpread && member.role == Role::Tank &&
        counts.Get(roleClass) >= SpreadCap(stats.Get(roleClass), stats.guilds))
        return Fit::TankClassSpread;

    // Owner tank mix: the class maximum, and tank slots kept free for mix classes below their minimum.
    if (member.role == Role::Tank && !switches.tankMix.empty())
    {
        auto const own = switches.tankMix.find(member.cls);
        if (own != switches.tankMix.end() && own->second.second && counts.Get(roleClass) >= own->second.second)
            return Fit::TankClassMix;
        if (inRole + 1 + MissingTankMix(counts, stats, switches.tankMix, member.cls) > quota.tanks)
            return Fit::TankClassMix;
    }

    RaceClass const combo(member.race, member.cls);
    if (switches.rareComboSpread && counts.GetCombo(combo) >= SpreadCap(stats.GetCombo(combo), stats.guilds))
        return Fit::RareComboSpread;

    // Owner rare pairs: at most rarePairCap of a listed race x class pair per guild.
    if (switches.rarePairCap && switches.rarePairs.count(combo) && counts.GetCombo(combo) >= switches.rarePairCap)
        return Fit::RarePairCap;

    return Fit::Ok;
}
// The deal: guid -> guild for every member with a guild among `guilds` (kept) and every member
// without a guild that gets a slot. `guilds` in a fixed order (ascending id): the index is the last
// tie-break. targetGuilds (>= guilds.size()) sets the spread caps. A member whose role slots are all
// taken stays without a guild. The spread rules are hard while guilds of the target are still missing
// (the member waits for the next guild, as a charter does); once every target guild exists they step
// back to the role slot alone where no guild fits, so nobody waits for a guild that cannot come.
inline std::map<std::uint32_t, std::uint32_t> Deal(std::vector<Member> const& members, std::vector<std::uint32_t> const& guilds,
    Quota const& quota, Switches const& switches, std::uint32_t targetGuilds)
{
    std::map<std::uint32_t, std::uint32_t> assigned;
    std::map<std::uint32_t, GuildCounts> counts;
    for (std::uint32_t guild : guilds)
        counts[guild];

    FactionStats stats = MakeStats(members, std::max<std::uint32_t>(targetGuilds, std::uint32_t(guilds.size())));
    // Fall back to the role slot alone only when no further guild of the target can come.
    bool const allGuilds = guilds.size() >= targetGuilds;
    for (Member const& member : members)
    {
        if (!member.guild || !counts.count(member.guild))
            continue;
        AddToCounts(counts[member.guild], member);
        assigned[member.guid] = member.guild;
    }

    for (Role role : { Role::Tank, Role::Healer, Role::Dps })
    {
        std::map<std::uint8_t, std::uint32_t> perClass;
        std::vector<Member> fresh;
        for (Member const& member : members)
        {
            if (member.role != role)
                continue;
            ++perClass[member.cls];
            if (!member.guild)
                fresh.push_back(member);
        }

        // Rarest class first, then class, level band, race, ordinal (guild_plan.py, every role).
        std::sort(fresh.begin(), fresh.end(), [&perClass](Member const& a, Member const& b)
        {
            return std::make_tuple(perClass[a.cls], a.cls, Band(a.level), a.race, a.ordinal, a.guid) <
                std::make_tuple(perClass[b.cls], b.cls, Band(b.level), b.race, b.ordinal, b.guid);
        });

        for (Member const& member : fresh)
        {
            std::vector<std::size_t> open;
            std::vector<std::size_t> fitting;
            for (std::size_t i = 0; i < guilds.size(); ++i)
            {
                Fit const fit = CheckFit(counts[guilds[i]], member, quota, switches, stats);
                if (fit == Fit::RoleFull || fit == Fit::UnknownRole)
                    continue;
                open.push_back(i);
                if (fit == Fit::Ok)
                    fitting.push_back(i);
            }
            std::vector<std::size_t> const& candidates = (fitting.empty() && allGuilds) ? open : fitting;
            if (candidates.empty())
                continue;

            RoleClass const roleClass(role, member.cls);
            RaceClass const combo(member.race, member.cls);
            std::uint32_t const band = Band(member.level);
            // guild_plan.py: (cls_count, count, band_count, index); RareComboSpread puts the pair second.
            auto key = [&](std::size_t i)
            {
                GuildCounts const& c = counts[guilds[i]];
                return std::make_tuple(c.Get(roleClass), switches.rareComboSpread ? c.GetCombo(combo) : 0u,
                    c.Get(role), c.GetBand(role, band), i);
            };
            std::size_t best = candidates.front();
            for (std::size_t i : candidates)
                if (key(i) < key(best))
                    best = i;

            std::uint32_t const guild = guilds[best];
            AddToCounts(counts[guild], member);
            assigned[member.guid] = guild;
            if (role == Role::Healer && stats.unplacedHealers[member.cls])
                --stats.unplacedHealers[member.cls];
            if (role == Role::Tank && stats.unplacedTanks[member.cls])
                --stats.unplacedTanks[member.cls];
        }
    }

    return assigned;
}

// --- Plan file (AiPlayerbot.RosterGuild.PlanFile): OB-40's guilds.tsv, columns
//     ordinal guid faction guild role class race (tab separated, header line "ordinal ..."). ---------

struct PlanEntry
{
    std::uint32_t ordinal = 0;
    std::uint32_t guid = 0;
    std::string faction;       // "A" / "H"
    std::string guild;         // plan label, e.g. "A1"
    Role role = Role::Unknown;
    std::uint8_t cls = 0;
    std::uint8_t race = 0;
};

inline bool ParseUnsigned(std::string const& text, std::uint32_t& out)
{
    if (text.empty() || text.size() > 9)
        return false;
    std::uint32_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + std::uint32_t(c - '0');
    }
    out = value;
    return true;
}

// One data line; false for the header, blank lines, comments and malformed lines.
inline bool ParsePlanLine(std::string line, PlanEntry& out)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    if (line.empty() || line[0] == '#' || line.compare(0, 7, "ordinal") == 0)
        return false;

    std::vector<std::string> cols;
    std::string::size_type start = 0;
    while (true)
    {
        std::string::size_type const tab = line.find('\t', start);
        cols.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            break;
        start = tab + 1;
    }
    if (cols.size() < 7)
        return false;

    PlanEntry entry;
    std::uint32_t cls = 0;
    std::uint32_t race = 0;
    if (!ParseUnsigned(cols[0], entry.ordinal) || !ParseUnsigned(cols[1], entry.guid) || !entry.guid ||
        (cols[2] != "A" && cols[2] != "H") || cols[3].empty() || !ParseUnsigned(cols[5], cls) || !ParseUnsigned(cols[6], race) ||
        cls > 255 || race > 255)
        return false;
    entry.faction = cols[2];
    entry.guild = cols[3];
    entry.role = ParseRole(cols[4]);
    if (entry.role == Role::Unknown)
        return false;
    entry.cls = std::uint8_t(cls);
    entry.race = std::uint8_t(race);
    out = entry;
    return true;
}

// guid -> entry; rejected counts the malformed data lines (header, blanks and comments not counted).
inline std::map<std::uint32_t, PlanEntry> ParsePlan(std::vector<std::string> const& lines, std::uint32_t& rejected)
{
    std::map<std::uint32_t, PlanEntry> plan;
    rejected = 0;
    for (std::string const& line : lines)
    {
        PlanEntry entry;
        if (ParsePlanLine(line, entry))
        {
            plan[entry.guid] = entry;
            continue;
        }
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ' || trimmed.back() == '\t'))
            trimmed.pop_back();
        if (!trimmed.empty() && trimmed[0] != '#' && trimmed.compare(0, 7, "ordinal") != 0)
            ++rejected;
    }
    return plan;
}

// Plan mode: a guild belongs to the label of its leader; the lowest guild id wins when two guilds
// carry one label. Returns label -> guild.
inline std::map<std::string, std::uint32_t> LabelGuilds(std::vector<std::pair<std::uint32_t, std::string>> const& guildLeaderLabels)
{
    std::map<std::string, std::uint32_t> out;
    for (auto const& item : guildLeaderLabels)
    {
        if (item.second.empty())
            continue;
        auto it = out.find(item.second);
        if (it == out.end() || item.first < it->second)
            out[item.second] = item.first;
    }
    return out;
}

// Plan mode, charter: the signer and the charter owner carry the same label. Bots without a plan
// entry ("" label) are not ruled by the plan.
inline bool SamePlanGuild(std::string const& signerLabel, std::string const& ownerLabel)
{
    return signerLabel.empty() || ownerLabel.empty() || signerLabel == ownerLabel;
}


// "1:2-3,11:1,4:1,2:1,7:1" -> class -> (minimum, maximum): "<class>:<min>" or "<class>:<min>-<max>",
// maximum 0 = no cap. Malformed entries and classes outside 1..11 are skipped; "" = no mix.
inline TankMix ParseTankMix(std::string const& text)
{
    TankMix mix;
    std::string::size_type start = 0;
    while (start <= text.size())
    {
        std::string::size_type const comma = text.find(',', start);
        std::string entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? text.size() + 1 : comma + 1;

        std::string clean;
        for (char c : entry)
            if (c != ' ' && c != '\t' && c != '"')
                clean += c;
        std::string::size_type const colon = clean.find(':');
        if (colon == std::string::npos)
            continue;
        std::string const range = clean.substr(colon + 1);
        std::string::size_type const dash = range.find('-');
        std::uint32_t cls = 0, minimum = 0, maximum = 0;
        if (!ParseUnsigned(clean.substr(0, colon), cls) || cls < 1 || cls > 11 ||
            !ParseUnsigned(range.substr(0, dash), minimum) ||
            (dash != std::string::npos && (!ParseUnsigned(range.substr(dash + 1), maximum) || maximum < minimum)))
            continue;
        mix[std::uint8_t(cls)] = std::make_pair(minimum, maximum);
    }
    return mix;
}

// "3:7,3:9,5:2" -> (race, class) pairs. Malformed entries are skipped; "" = no pair.
inline std::set<RaceClass> ParseRarePairs(std::string const& text)
{
    std::set<RaceClass> pairs;
    std::string::size_type start = 0;
    while (start <= text.size())
    {
        std::string::size_type const comma = text.find(',', start);
        std::string entry = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? text.size() + 1 : comma + 1;

        std::string clean;
        for (char c : entry)
            if (c != ' ' && c != '\t' && c != '"')
                clean += c;
        std::string::size_type const colon = clean.find(':');
        std::uint32_t race = 0, cls = 0;
        if (colon == std::string::npos || !ParseUnsigned(clean.substr(0, colon), race) ||
            !ParseUnsigned(clean.substr(colon + 1), cls) || !race || race > 255 || !cls || cls > 255)
            continue;
        pairs.insert(RaceClass(std::uint8_t(race), std::uint8_t(cls)));
    }
    return pairs;
}

// Per-guild cap of a rare pair: share of the guild size, rounded, at least 1 (owner: about 2.5 % per
// pair, 1 in a guild of 45). share <= 0 or no guild size = no cap (0).
inline std::uint32_t RarePairCapFor(float share, std::uint32_t guildSize)
{
    if (!(share > 0.0f) || !guildSize)
        return 0;
    std::uint32_t const cap = std::uint32_t(std::lround(double(share) * double(guildSize)));
    return cap ? cap : 1;
}
}
