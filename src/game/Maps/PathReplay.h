#pragma once

// `.mmaps replay` (twow-repo#592 PATH-02, PR pair 0): deterministic replay of PathInfo.
//
// The GM command reads a fixture of (mapId, start, dest, profile, chain) rows, computes every row on a
// fresh PathInfo with today's code and writes one greppable [PathReplay] line per result with the points
// as hexfloats (%a, bit exact). Two runs on the same mmaps diff line by line: old build against old
// build (A/A, precondition) and old against new (proof that PathInfo variant A leaves unit paths byte
// identical). It changes no behaviour and runs only when called, so it has no switch. Never in
// measurement arms: the command runs on the world thread (a GM's chat packet is always processed in
// World::UpdateSessions, whatever the chat type, see WorldSession::QueuePacket; a console line runs in
// World::ProcessCliCommands, called from World::Update after sMapMgr.Update returned), while no map
// updates, so a call stalls the whole world tick for up to the time budget plus one row.
//
// Syntax: .mmaps replay <name> [offset [probe]]
//
// The probe is the unit whose PathInfo computes the unit rows. In game without a probe argument it is the
// selected unit, without a selection the caller, and it must stand on the caller's map. From the server
// console (M2 scripts write `mmaps replay <name> <offset> <probe>` into mangosd.in) there is no session and
// no selection, so the probe argument is required there; without it the call ends with an error line
// before the fixture is opened. Probe spec (ParsePathReplayProbe, fails closed on anything else):
//
//   <player>          an online player (bots are players), 1..PATH_REPLAY_MAX_PROBE_NAME_LEN ASCII
//                     letters, the case is normalized as for any character name
//   <player>:<entry>  the nearest live creature with that entry (1..9 digits, > 0) within
//                     PATH_REPLAY_PROBE_RANGE yd of that player
//
// An explicit probe has no caller map check (the console has no map); unit rows still run only on the
// probe's map. Any probe must be in the world and not on a transport.
//
// Fixture: <DataDir>/pathreplay/<name>, plain text, one row per line, fields separated by blanks or tabs:
//
//   <id> <mapId> <sx> <sy> <sz> <dx> <dy> <dz> <profile> <chain>
//
//   id       row id (uint32), printed in every result line; the diff key
//   mapId    map of start and dest; a unit row runs only when the probe unit stands on this map
//   s*/d*    start and destination, decimal or C99 hexfloat (as printed by %a), finite
//   profile  unit: PathInfo(probe), the probe as described above
//            map:  PathInfo(mapId, instanceId) without a unit (twow-repo#592 PR pair 2). With
//                  PathFinder.Unitless = 0 every calculate fails closed (NOPATH); with 1 it runs in map
//                  mode with player defaults on the map's loaded terrain. Runs on any map (no probe).
//   chain    0: one calculate (getPathTo / getPathStepFrom); 1..40: the same PathInfo is reused for up to
//            chain calculates from the end of the last step, as WorldPosition::getPathFromPath does
//            with maxAttempt = chain (bot default 40)
//
// Lines starting with '#' and blank lines are comments. A line that does not parse is reported as
// "[PathReplay] bad line=<n>" and skipped; the run goes on.
//
// Output (server log; the caller's chat or console gets the done/partial line and errors):
//   [PathReplay] begin file=<name> offset=<n> probe=<player|creature> entry=<n> map=<n> probe_name=<player> probe_guid=<n>
//   [PathReplay] id=<id> mode=<unit|map> type=0x<PathType> n=<points> end=<x>,<y>,<z> pts=<x>,<y>,<z>;...
//   [PathReplay] id=<id> step=<k> mode=<unit|map> type=... n=... end=... pts=... (one per chain step)
//   [PathReplay] id=<id> chain steps=<k> n=<points of the joined path> stop=<nopath|stall|arrived|max>
//   [PathReplay] id=<id> skipped=other_map                                     (unit rows only)
//   [PathReplay] done lines=<n> rows=<n> skipped=<n> bad=<n> ms=<n>
//   [PathReplay] partial next=<offset> reason=<lines|time> lines=<n> rows=<n> skipped=<n> bad=<n> ms=<n>
// end= is PathInfo::getActualEndPosition. probe_guid= is the low guid of the probe unit, probe_name= the
// player it was found from (the named player of the probe argument, in game without it the caller). The
// diff compares the lines with id= only (begin/done carry the probe and the wall time).
//
// Limits per call: at most PATH_REPLAY_MAX_LINES lines read (comments included) and a wall-clock budget
// of PATH_REPLAY_BUDGET_MS, checked after every row. When either is reached the call ends after the
// running row with "partial next=<offset>"; `.mmaps replay <name> <offset>` continues there. The offset
// counts physical lines from the start of the file. Memory and I/O stay bounded as well: a line keeps at
// most PATH_REPLAY_MAX_LINE_LEN + 1 characters (the rest of a longer line is skipped, the line is bad)
// and a file larger than PATH_REPLAY_MAX_FILE_BYTES, or one without a size (pipe, device), is refused.
//
// Header-only and free of server types so t/path_replay_test.cpp checks the name guard, the probe spec,
// the row parser, the hexfloat format, the limits and the chain rule without a world.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <limits>
#include <string>
#include <vector>

constexpr char const* PATH_REPLAY_DIR = "pathreplay/";      // below DataDir, fixed
constexpr uint32_t PATH_REPLAY_MAX_LINES = 500;             // lines read per call
constexpr uint32_t PATH_REPLAY_BUDGET_MS = 200;             // wall clock per call
constexpr uint32_t PATH_REPLAY_MAX_CHAIN = 40;              // getPathFromPath default maxAttempt
constexpr uint32_t PATH_REPLAY_MAX_OFFSET = 100000;         // fixtures have ~5400 rows
constexpr size_t PATH_REPLAY_MAX_NAME_LEN = 64;
constexpr size_t PATH_REPLAY_MAX_LINE_LEN = 512;
constexpr uint64_t PATH_REPLAY_MAX_FILE_BYTES = 64ull * 1024 * 1024;   // fixtures stay below 1 MB
constexpr size_t PATH_REPLAY_MAX_PROBE_NAME_LEN = 12;       // MAX_PLAYER_NAME
constexpr size_t PATH_REPLAY_MAX_PROBE_ENTRY_DIGITS = 9;    // entry below 10^9, fits uint32
constexpr float PATH_REPLAY_PROBE_RANGE = 150.0f;           // <player>:<entry>: creature search radius, yd

// Chain rule of WorldPosition::getPathFromPath / isPathTo with the bot defaults:
// AiPlayerbot.TargetPosRecalcDistance 0.1 (not set in the live config) and maxZDistance 2.
constexpr float PATH_REPLAY_CHAIN_RECALC_DIST = 0.1f;
constexpr float PATH_REPLAY_CHAIN_MAX_Z = 2.0f;

// PathType values the bot accepts in getPathStepFrom (exact match, so NORMAL|NOT_USING_PATH is not one).
constexpr uint32_t PATH_REPLAY_TYPE_NORMAL = 0x0001;        // PATHFIND_NORMAL
constexpr uint32_t PATH_REPLAY_TYPE_INCOMPLETE = 0x0004;    // PATHFIND_INCOMPLETE

enum class PathReplayProfile
{
    Unit,
    Map,
};

struct PathReplayRow
{
    uint32_t id = 0;
    uint32_t mapId = 0;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    PathReplayProfile profile = PathReplayProfile::Unit;
    uint32_t chain = 0;
};

enum class PathReplayParse
{
    Row,
    Comment,    // blank line or '#'
    Bad,
};

enum class PathReplayStop
{
    None,
    Lines,
    Time,
};

namespace path_replay_detail
{
// Windows opens a device instead of a file for these basenames in any directory and with any extension
// (CON blocks on console input), so "CON", "nul.tsv" or "com1.x" are no fixture names on any platform.
inline bool IsReservedDeviceStem(std::string const& name)
{
    std::string stem = name.substr(0, name.find('.'));
    for (char& c : stem)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
        return true;
    return stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        stem[3] >= '0' && stem[3] <= '9';
}
}

// Fixture names: letters, digits, '_', '-', '.', no leading '.', no "..", no separators, no drive, no
// reserved device name. The file is always opened as DataDir + PATH_REPLAY_DIR + name, so an accepted
// name cannot leave that directory.
inline bool PathReplayNameAllowed(std::string const& name)
{
    if (name.empty() || name.size() > PATH_REPLAY_MAX_NAME_LEN)
        return false;
    if (name[0] == '.' || name.find("..") != std::string::npos)
        return false;
    for (char c : name)
    {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.';
        if (!ok)
            return false;
    }
    if (path_replay_detail::IsReservedDeviceStem(name))
        return false;
    return true;
}

// Probe spec "<player>" or "<player>:<entry>" (twow-repo#592 PATH-02, console form): the name is 1..12
// ASCII letters, the entry 1..9 digits and > 0, nothing else (no blank, no sign, no second ':'). entry is
// 0 for a player probe. On false name and entry are left untouched and nothing may be looked up.
inline bool ParsePathReplayProbe(std::string const& spec, std::string& name, uint32_t& entry)
{
    size_t const colon = spec.find(':');
    std::string const namePart = spec.substr(0, colon);
    if (namePart.empty() || namePart.size() > PATH_REPLAY_MAX_PROBE_NAME_LEN)
        return false;
    for (char c : namePart)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            return false;

    uint32_t value = 0;
    if (colon != std::string::npos)
    {
        std::string const entryPart = spec.substr(colon + 1);
        if (entryPart.empty() || entryPart.size() > PATH_REPLAY_MAX_PROBE_ENTRY_DIGITS)
            return false;
        for (char c : entryPart)
            if (c < '0' || c > '9')
                return false;   // a second ':' ends here as well
        value = uint32_t(std::strtoul(entryPart.c_str(), nullptr, 10));
        if (value == 0)
            return false;
    }
    name = namePart;
    entry = value;
    return true;
}

// Size of the opened fixture (istream tellg at the end); -1 means it has none (pipe, device): refused.
inline bool PathReplayFileSizeAllowed(int64_t size)
{
    return size >= 0 && uint64_t(size) <= PATH_REPLAY_MAX_FILE_BYTES;
}

enum class PathReplayRead
{
    Line,
    TooLong,    // longer than PATH_REPLAY_MAX_LINE_LEN + 1, the rest of it was skipped: one bad line
    End,
};

// One physical line with a fixed memory bound (std::getline would first read a line of any length into
// memory). At most PATH_REPLAY_MAX_LINE_LEN + 1 characters are kept, so ParsePathReplayLine still sees a
// line that is one character too long and rejects it. The '\n' is dropped, a '\r' stays (blank to the
// parser), an embedded NUL stays in the line (a parse error, not a shorter line).
inline PathReplayRead ReadPathReplayLine(std::istream& in, std::string& line)
{
    line.clear();
    char buf[PATH_REPLAY_MAX_LINE_LEN + 2];
    in.getline(buf, sizeof(buf));
    std::streamsize const got = in.gcount();
    if (in.bad())
        return PathReplayRead::End;
    if (in.fail())
    {
        // Nothing extracted: end of the file. Otherwise the buffer is full before the '\n'.
        if (got == 0)
            return PathReplayRead::End;
        in.clear();
        in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return PathReplayRead::TooLong;
    }
    // gcount counts the extracted '\n' too, unless the last line of the file has none.
    size_t const len = size_t(got) - (in.eof() ? 0 : 1);
    line.assign(buf, len);
    return PathReplayRead::Line;
}

// Limits are checked after a row: the row that crosses the budget still completes.
inline PathReplayStop PathReplayShouldStop(uint32_t linesRead, uint64_t elapsedMs)
{
    if (linesRead >= PATH_REPLAY_MAX_LINES)
        return PathReplayStop::Lines;
    if (elapsedMs >= PATH_REPLAY_BUDGET_MS)
        return PathReplayStop::Time;
    return PathReplayStop::None;
}

namespace path_replay_detail
{
inline bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool ParseUInt(std::string const& token, uint32_t& out)
{
    if (token.empty() || token.size() > 10)
        return false;
    for (char c : token)
        if (c < '0' || c > '9')
            return false;
    unsigned long long const v = std::strtoull(token.c_str(), nullptr, 10);
    if (v > 0xFFFFFFFFull)
        return false;
    out = uint32_t(v);
    return true;
}

// Decimal or C99 hexfloat; the whole token must be consumed and the value finite.
inline bool ParseFloat(std::string const& token, float& out)
{
    if (token.empty())
        return false;
    char* end = nullptr;
    float const v = std::strtof(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}
}

inline PathReplayParse ParsePathReplayLine(std::string const& line, PathReplayRow& row)
{
    if (line.size() > PATH_REPLAY_MAX_LINE_LEN)
        return PathReplayParse::Bad;

    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && path_replay_detail::IsBlank(line[i]))
            ++i;
        if (i >= line.size())
            break;
        if (tokens.empty() && line[i] == '#')
            return PathReplayParse::Comment;
        size_t const start = i;
        while (i < line.size() && !path_replay_detail::IsBlank(line[i]))
            ++i;
        if (tokens.size() == 10)
            return PathReplayParse::Bad;    // more than 10 fields
        tokens.push_back(line.substr(start, i - start));
    }
    if (tokens.empty())
        return PathReplayParse::Comment;
    if (tokens.size() != 10)
        return PathReplayParse::Bad;

    PathReplayRow r;
    using namespace path_replay_detail;
    if (!ParseUInt(tokens[0], r.id) || !ParseUInt(tokens[1], r.mapId) ||
        !ParseFloat(tokens[2], r.sx) || !ParseFloat(tokens[3], r.sy) || !ParseFloat(tokens[4], r.sz) ||
        !ParseFloat(tokens[5], r.dx) || !ParseFloat(tokens[6], r.dy) || !ParseFloat(tokens[7], r.dz) ||
        !ParseUInt(tokens[9], r.chain))
        return PathReplayParse::Bad;
    if (tokens[8] == "unit")
        r.profile = PathReplayProfile::Unit;
    else if (tokens[8] == "map")
        r.profile = PathReplayProfile::Map;
    else
        return PathReplayParse::Bad;
    if (r.chain > PATH_REPLAY_MAX_CHAIN)
        return PathReplayParse::Bad;

    row = r;
    return PathReplayParse::Row;
}

// One float as printf %a of its double promotion: exact, reads back with strtof to the same bits.
inline void AppendPathReplayHex(std::string& out, float v)
{
    char buf[48];
    int const n = std::snprintf(buf, sizeof(buf), "%a", double(v));
    if (n > 0)
        out.append(buf, size_t(n) < sizeof(buf) ? size_t(n) : sizeof(buf) - 1);
}

inline void AppendPathReplayPoint(std::string& out, float x, float y, float z)
{
    AppendPathReplayHex(out, x);
    out += ',';
    AppendPathReplayHex(out, y);
    out += ',';
    AppendPathReplayHex(out, z);
}

// "x,y,z;x,y,z;..." for any point type with x/y/z (G3D::Vector3 in the server).
template <class Points>
inline std::string FormatPathReplayPoints(Points const& points)
{
    std::string out;
    bool first = true;
    for (auto const& p : points)
    {
        if (!first)
            out += ';';
        first = false;
        AppendPathReplayPoint(out, p.x, p.y, p.z);
    }
    return out;
}

enum class PathReplayChainStop
{
    NoPath,     // step result not NORMAL/INCOMPLETE or empty: getPathStepFrom returns {}
    Stall,      // step ends closer than the recalc distance to where it started
    Arrived,    // isPathTo(step): within the recalc distance in 2D and 2 yd in height of dest
    Max,        // chain calculates used up
};

inline char const* PathReplayChainStopName(PathReplayChainStop stop)
{
    switch (stop)
    {
        case PathReplayChainStop::NoPath: return "nopath";
        case PathReplayChainStop::Stall: return "stall";
        case PathReplayChainStop::Arrived: return "arrived";
        case PathReplayChainStop::Max: return "max";
    }
    return "max";
}

struct PathReplayChainResult
{
    uint32_t steps = 0;     // calculates run
    uint32_t points = 1;    // joined path, start point included (fullPath = startPath + steps without their start)
    PathReplayChainStop stop = PathReplayChainStop::Max;
};

// The loop of WorldPosition::getPathFromPath on one reused PathInfo. step(k, x, y, z) runs calculate
// from (x, y, z) to the destination and returns the PathType; points(k) returns that step's points.
// Not modelled: the underwater extension of getPathStepFrom for INCOMPLETE paths (it needs terrain
// and LoS queries of the bot module); the replay checks PathInfo, not that bot helper.
template <class Point, class Step, class StepPoints>
inline PathReplayChainResult RunPathReplayChain(Point start, Point const& dest, uint32_t maxAttempt, Step step, StepPoints points)
{
    PathReplayChainResult result;
    Point current = start;
    for (uint32_t k = 0; k < maxAttempt; ++k)
    {
        uint32_t const type = step(k, current.x, current.y, current.z);
        ++result.steps;
        auto const& pts = points(k);
        if ((type != PATH_REPLAY_TYPE_NORMAL && type != PATH_REPLAY_TYPE_INCOMPLETE) || pts.empty())
        {
            result.stop = PathReplayChainStop::NoPath;
            return result;
        }
        Point const back = pts.back();
        float const mx = back.x - current.x, my = back.y - current.y, mz = back.z - current.z;
        if (std::sqrt(mx * mx + my * my + mz * mz) < PATH_REPLAY_CHAIN_RECALC_DIST)
        {
            result.stop = PathReplayChainStop::Stall;
            return result;
        }
        result.points += uint32_t(pts.size() - 1);
        float const ex = dest.x - back.x, ey = dest.y - back.y;
        if (ex * ex + ey * ey < PATH_REPLAY_CHAIN_RECALC_DIST * PATH_REPLAY_CHAIN_RECALC_DIST &&
            std::fabs(back.z - dest.z) < PATH_REPLAY_CHAIN_MAX_Z)
        {
            result.stop = PathReplayChainStop::Arrived;
            return result;
        }
        current = back;
    }
    result.stop = PathReplayChainStop::Max;
    return result;
}
