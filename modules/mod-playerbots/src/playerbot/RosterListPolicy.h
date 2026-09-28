#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::roster_list
{
// twow-repo#419 variant B: `.bot roster` lists the persistent roster bots for
// any account rank, without the 49-entry cap of /who. Everything here is pure
// so the wire format the BotMenu addon parses is pinned by a unit test
// (roster_list_policy_tests) and cannot drift from the server silently.
//
// Wire format, protocol 1. One system message per line, fields split by ';'.
// Field values never contain ';' or '|' (Sanitize), so a line splits exactly.
//
//   BL1;H;<page>;<pages>;<matched>;<shown>      header, first line
//   BL1;E;<name>;<class>;<race>;<level>;<role>;<status>;<group>;<zone>;<guild>
//   BL1;Z;<shown>                               end of page, last line
//   BL1;X;<code>;<retryMs>                      refused; the only line sent
//
// <class>/<race> are English tokens (ClassToken/RaceToken), <role> is
// tank|heal|dps, <status> online|offline, <group> free|group|raid. <zone> and
// <guild> may be empty. Account names, account ids and addresses are never part
// of an entry.

constexpr char const* kProtocol = "BL1";
constexpr unsigned int kMaxPageSize = 50;

enum class Faction { ALLIANCE, HORDE };
enum class FactionFilter { OWN, ALLIANCE, HORDE, ALL };
enum class Role { ANY, TANK, HEAL, DPS };
enum class GroupState { FREE, GROUP, RAID };

struct Entry
{
    std::string name;
    unsigned int classId = 0;
    unsigned int raceId = 0;
    unsigned int level = 0;
    Role role = Role::DPS;
    bool online = false;
    GroupState group = GroupState::FREE;
    std::string zone;
    std::string guild;
    Faction faction = Faction::ALLIANCE;
    unsigned int security = 0;          // account rank of the bot's session
};

struct Filter
{
    unsigned int page = 1;
    FactionFilter faction = FactionFilter::OWN;
    unsigned int classId = 0;           // 0 = any
    Role role = Role::ANY;
    unsigned int minLevel = 0;
    unsigned int maxLevel = 0;          // 0 = no upper bound
    std::string zone;                   // case-insensitive substring, '_' = ' '
    bool includeOffline = false;
};

// Who is asking, and what the world config lets them see (mirrors /who).
struct Viewer
{
    Faction faction = Faction::ALLIANCE;
    bool gm = false;                    // AiPlayerbot.RosterControl.GmMinSecurity
    bool allowTwoSide = false;          // AllowTwoSide.WhoList
};

inline char const* ClassToken(unsigned int classId)
{
    switch (classId)
    {
        case 1: return "Warrior";
        case 2: return "Paladin";
        case 3: return "Hunter";
        case 4: return "Rogue";
        case 5: return "Priest";
        case 7: return "Shaman";
        case 8: return "Mage";
        case 9: return "Warlock";
        case 11: return "Druid";
    }
    return "Unknown";
}

inline char const* RaceToken(unsigned int raceId)
{
    switch (raceId)
    {
        case 1: return "Human";
        case 2: return "Orc";
        case 3: return "Dwarf";
        case 4: return "NightElf";
        case 5: return "Undead";
        case 6: return "Tauren";
        case 7: return "Gnome";
        case 8: return "Troll";
        case 9: return "Goblin";
        case 10: return "HighElf";
    }
    return "Unknown";
}

inline char const* RoleToken(Role role)
{
    switch (role)
    {
        case Role::TANK: return "tank";
        case Role::HEAL: return "heal";
        case Role::DPS:  return "dps";
        case Role::ANY:  break;
    }
    return "dps";
}

inline char const* GroupToken(GroupState group)
{
    switch (group)
    {
        case GroupState::FREE:  return "free";
        case GroupState::GROUP: return "group";
        case GroupState::RAID:  return "raid";
    }
    return "free";
}

inline std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return text;
}

// ';' splits fields and '|' starts a chat escape sequence in the client.
inline std::string Sanitize(std::string const& text)
{
    std::string out = text;
    for (char& c : out)
        if (c == ';' || c == '|' || (static_cast<unsigned char>(c) < 0x20))
            c = ' ';
    return out;
}

inline bool ParseUInt(std::string const& text, unsigned int& value)
{
    if (text.empty() || text.size() > 6)
        return false;
    unsigned int v = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + unsigned(c - '0');
    }
    value = v;
    return true;
}

inline unsigned int ClassFromToken(std::string const& token)
{
    std::string const t = Lower(token);
    for (unsigned int id = 1; id <= 11; ++id)
        if (Lower(ClassToken(id)) == t)
            return id;
    return 0;
}

// `.bot roster [page=N] [faction=own|alliance|horde|all] [class=<name>]
// [role=tank|heal|dps] [level=N|A-B] [zone=<text>] [status=online|all]`.
// A bare number is the page. Anything else is an error, never ignored: an
// addon that sends a filter the server does not know must find out.
inline bool ParseFilter(std::string const& args, Filter& filter, std::string& error)
{
    filter = Filter{};
    size_t pos = 0;
    while (pos < args.size())
    {
        while (pos < args.size() && args[pos] == ' ')
            ++pos;
        if (pos >= args.size())
            break;
        size_t end = args.find(' ', pos);
        if (end == std::string::npos)
            end = args.size();
        std::string const token = args.substr(pos, end - pos);
        pos = end;

        size_t const eq = token.find('=');
        std::string const key = eq == std::string::npos ? "page" : Lower(token.substr(0, eq));
        std::string const value = eq == std::string::npos ? token : token.substr(eq + 1);
        std::string const lvalue = Lower(value);

        if (key == "page")
        {
            if (!ParseUInt(value, filter.page) || filter.page == 0)
                { error = "page"; return false; }
        }
        else if (key == "faction")
        {
            if (lvalue == "own") filter.faction = FactionFilter::OWN;
            else if (lvalue == "alliance") filter.faction = FactionFilter::ALLIANCE;
            else if (lvalue == "horde") filter.faction = FactionFilter::HORDE;
            else if (lvalue == "all") filter.faction = FactionFilter::ALL;
            else { error = "faction"; return false; }
        }
        else if (key == "class")
        {
            filter.classId = ClassFromToken(value);
            if (!filter.classId)
                { error = "class"; return false; }
        }
        else if (key == "role")
        {
            if (lvalue == "tank") filter.role = Role::TANK;
            else if (lvalue == "heal" || lvalue == "healer") filter.role = Role::HEAL;
            else if (lvalue == "dps") filter.role = Role::DPS;
            else { error = "role"; return false; }
        }
        else if (key == "level")
        {
            size_t const dash = value.find('-');
            unsigned int lo = 0, hi = 0;
            if (dash == std::string::npos)
            {
                if (!ParseUInt(value, lo))
                    { error = "level"; return false; }
                hi = lo;
            }
            else if (!ParseUInt(value.substr(0, dash), lo) || !ParseUInt(value.substr(dash + 1), hi))
                { error = "level"; return false; }
            if (lo == 0 || hi < lo)
                { error = "level"; return false; }
            filter.minLevel = lo;
            filter.maxLevel = hi;
        }
        else if (key == "zone")
        {
            if (value.empty())
                { error = "zone"; return false; }
            filter.zone = lvalue;
            std::replace(filter.zone.begin(), filter.zone.end(), '_', ' ');
        }
        else if (key == "status")
        {
            if (lvalue == "online") filter.includeOffline = false;
            else if (lvalue == "all") filter.includeOffline = true;
            else { error = "status"; return false; }
        }
        else
        {
            error = "unknown_key";
            return false;
        }
    }
    return true;
}

// A rank-0 viewer sees another faction only where /who would show it.
inline bool FactionFilterAllowed(Filter const& filter, Viewer const& viewer)
{
    if (viewer.gm || viewer.allowTwoSide || filter.faction == FactionFilter::OWN)
        return true;
    FactionFilter const own = viewer.faction == Faction::ALLIANCE ? FactionFilter::ALLIANCE : FactionFilter::HORDE;
    return filter.faction == own;
}

// Visibility first (who may see this character at all), then the filter.
// Stricter than /who on purpose: GM.InWhoList.Level lets a player see GM
// accounts up to that rank (3 on the live realm); this list shows a non-GM
// viewer only rank-0 characters, whatever that setting is.
inline bool Visible(Entry const& e, Viewer const& viewer)
{
    return viewer.gm || e.security == 0;
}

inline bool Matches(Entry const& e, Filter const& f, Viewer const& viewer)
{
    if (!Visible(e, viewer))
        return false;
    if (!e.online && !f.includeOffline)
        return false;
    switch (f.faction)
    {
        case FactionFilter::OWN:      if (e.faction != viewer.faction) return false; break;
        case FactionFilter::ALLIANCE: if (e.faction != Faction::ALLIANCE) return false; break;
        case FactionFilter::HORDE:    if (e.faction != Faction::HORDE) return false; break;
        case FactionFilter::ALL:      break;
    }
    if (f.classId && e.classId != f.classId)
        return false;
    if (f.role != Role::ANY && e.role != f.role)
        return false;
    if (f.minLevel && (e.level < f.minLevel || e.level > f.maxLevel))
        return false;
    if (!f.zone.empty() && Lower(e.zone).find(f.zone) == std::string::npos)
        return false;
    return true;
}

inline unsigned int ClampPageSize(unsigned int configured)
{
    if (configured == 0)
        return 1;
    return configured > kMaxPageSize ? kMaxPageSize : configured;
}

inline unsigned int PageCount(size_t matched, unsigned int pageSize)
{
    if (matched == 0)
        return 1;
    return unsigned((matched + pageSize - 1) / pageSize);
}

// Per-session rate limit. lastMs == 0 means no earlier request.
inline bool RateLimited(uint64_t nowMs, uint64_t lastMs, unsigned int minIntervalMs, unsigned int& retryMs)
{
    retryMs = 0;
    if (lastMs == 0 || nowMs < lastMs || nowMs - lastMs >= minIntervalMs)
        return false;
    retryMs = unsigned(minIntervalMs - (nowMs - lastMs));
    return true;
}

inline std::string HeaderLine(unsigned int page, unsigned int pages, size_t matched, size_t shown)
{
    return std::string(kProtocol) + ";H;" + std::to_string(page) + ";" + std::to_string(pages) + ";" +
        std::to_string(matched) + ";" + std::to_string(shown);
}

inline std::string EntryLine(Entry const& e)
{
    std::string line = std::string(kProtocol) + ";E;";
    line += Sanitize(e.name) + ";";
    line += std::string(ClassToken(e.classId)) + ";";
    line += std::string(RaceToken(e.raceId)) + ";";
    line += std::to_string(e.level) + ";";
    line += std::string(RoleToken(e.role)) + ";";
    line += std::string(e.online ? "online" : "offline") + ";";
    line += std::string(GroupToken(e.group)) + ";";
    line += Sanitize(e.zone) + ";";
    line += Sanitize(e.guild);
    return line;
}

inline std::string EndLine(size_t shown)
{
    return std::string(kProtocol) + ";Z;" + std::to_string(shown);
}

inline std::string ErrorLine(std::string const& code, unsigned int retryMs = 0)
{
    return std::string(kProtocol) + ";X;" + Sanitize(code) + ";" + std::to_string(retryMs);
}

// One page, header to end marker. `entries` must already be in a stable order
// (the cache sorts by name) so that page N means the same thing twice.
inline std::vector<std::string> BuildPage(std::vector<Entry> const& entries, Filter const& filter,
    Viewer const& viewer, unsigned int pageSize, size_t* matchedOut = nullptr)
{
    pageSize = ClampPageSize(pageSize);
    std::vector<Entry const*> matched;
    for (Entry const& e : entries)
        if (Matches(e, filter, viewer))
            matched.push_back(&e);
    if (matchedOut)
        *matchedOut = matched.size();

    unsigned int const pages = PageCount(matched.size(), pageSize);
    if (filter.page > pages)
        return { ErrorLine("page") };

    size_t const begin = size_t(filter.page - 1) * pageSize;
    size_t const end = std::min(matched.size(), begin + pageSize);

    std::vector<std::string> lines;
    lines.reserve(end - begin + 2);
    lines.push_back(HeaderLine(filter.page, pages, matched.size(), end - begin));
    for (size_t i = begin; i < end; ++i)
        lines.push_back(EntryLine(*matched[i]));
    lines.push_back(EndLine(end - begin));
    return lines;
}
}
