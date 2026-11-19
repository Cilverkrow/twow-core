#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// A3 step 3 planner P-1 (A3-S3-PLANER-ENTWURF.md v0.3 §6, A3-GERUEST-L60.md 3 B6a; AiPlayerbot.Planner.BandTable, default ""):
// the band table of the planner. One row = one quest zone for one start key and one level range, with the zone's hub (by
// position; the hub id of the travel node store is renumbered on save, so no id), the median mob level for the grind candidate,
// the band target time Z2 (provisional, 0 = not set yet) and the start-zone rule R1 (only echoed and compared in T03).
// The planner (P-2) looks up the row with the lowest rank for its key and level. Pure rules and line formats, no game types,
// no locks, no config reads: the loader is BandTable.cpp. This file takes no code from the hub ladder (QuestBandPolicy.h,
// deleted in step 8), only the key scheme of its data.
namespace ai::band_table
{
// ---- keys -----------------------------------------------------------------------------------------------------------
// "1".."10" = race (levels 1-12), "A0" "A1" "H0" "H1" = faction + continent (map 0 / 1, levels 13+).
constexpr std::uint32_t KeyCount = 15;   // 0 = none, 1..10 races, 11 A0, 12 A1, 13 H0, 14 H1
constexpr std::uint32_t RaceKeyMaxLevel = 12;
constexpr std::uint32_t MaxLevel = 63;

inline std::uint32_t KeyIndex(std::string const& key)
{
    if (key.size() == 2 && (key[0] == 'A' || key[0] == 'H') && (key[1] == '0' || key[1] == '1'))
        return 11 + (key[0] == 'H' ? 2 : 0) + std::uint32_t(key[1] - '0');
    if (key.empty() || key.size() > 2 || key[0] == '0' || key.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    std::uint32_t const race = std::uint32_t(std::atoi(key.c_str()));
    return (race >= 1 && race <= 10) ? race : 0;
}

inline bool IsRaceKey(std::uint32_t keyIndex) { return keyIndex >= 1 && keyIndex <= 10; }

// The key of a bot: its race up to level 12, then faction ('A' / 'H') + continent (map 0 / 1); 0 = no key (other maps).
inline std::uint32_t KeyFor(std::uint32_t race, char faction, std::uint32_t mapId, std::uint32_t level)
{
    if (level <= RaceKeyMaxLevel)
        return (race >= 1 && race <= 10) ? race : 0;
    if (mapId > 1 || (faction != 'A' && faction != 'H'))
        return 0;
    return 11 + (faction == 'H' ? 2 : 0) + mapId;
}

// ---- rows -----------------------------------------------------------------------------------------------------------
struct Row
{
    std::uint32_t keyIndex = 0;
    std::uint32_t minLevel = 0;
    std::uint32_t maxLevel = 0;
    std::uint32_t zoneId = 0;
    std::uint32_t rank = 0;         // 1 = first choice, 2 = alternative
    std::uint32_t hubMap = 0;
    float hubX = 0.f, hubY = 0.f, hubZ = 0.f;
    std::uint32_t mobLevel = 0;     // median mob level at the hub; 0 = unknown (the grind candidate gets no XP)
    std::uint32_t z2Sec = 0;        // band target time Z2 in seconds; 0 = not set yet (values from DAUER-B, OB-00 19.11.)
    std::uint32_t r1MinLevel = 0;   // R1: leaving the start zone allowed from this level; 0 = no rule (echo + compare only)
};

struct Table
{
    std::vector<Row> rows;
};

// ---- file format ----------------------------------------------------------------------------------------------------
// Line 1 "# version=2", line 2 "# source=..." (free text), line 3 the header; columns by NAME, extra columns ignored, a
// missing column fails the whole file. Rows: tab separated; blank lines and lines starting with '#' are skipped.
enum Column { ColKey, ColMinLevel, ColMaxLevel, ColZoneId, ColRank, ColHubMap, ColHubX, ColHubY, ColHubZ, ColMobLevel,
    ColZ2Sec, ColR1MinLevel, ColumnCount };

inline char const* ColumnName(int c)
{
    static char const* const names[ColumnCount] = { "key", "min_level", "max_level", "zone_id", "rank", "hub_map", "hub_x",
        "hub_y", "hub_z", "mob_level", "z2_sec", "r1_min_level" };
    return (c >= 0 && c < ColumnCount) ? names[c] : "?";
}

struct Header
{
    int col[ColumnCount];
};

inline bool VersionLine(std::string const& line)
{
    static std::string const prefix = "# version=2";
    if (line.compare(0, prefix.size(), prefix) != 0)
        return false;
    return line.size() == prefix.size() || line[prefix.size()] == ' ' || line[prefix.size()] == '\t' ||
        line[prefix.size()] == '\r';
}

inline bool SourceLine(std::string const& line)
{
    return line.compare(0, 9, "# source=") == 0;
}

inline std::vector<std::string> SplitTabs(std::string const& line)
{
    std::vector<std::string> out;
    std::string::size_type start = 0;
    for (;;)
    {
        std::string::size_type const tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            break;
        start = tab + 1;
    }
    if (!out.empty() && !out.back().empty() && out.back().back() == '\r')
        out.back().pop_back();
    return out;
}

inline bool ParseHeader(std::string const& line, Header& header)
{
    std::vector<std::string> const fields = SplitTabs(line);
    for (int c = 0; c < ColumnCount; ++c)
    {
        header.col[c] = -1;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (fields[i] == ColumnName(c))
            {
                header.col[c] = int(i);
                break;
            }
        if (header.col[c] < 0)
            return false;
    }
    return true;
}

inline bool ParseUint(std::string const& s, std::uint32_t& out)
{
    if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    out = std::uint32_t(std::strtoul(s.c_str(), nullptr, 10));
    return true;
}

inline bool ParseFloat(std::string const& s, float& out)
{
    if (s.empty() || s.size() > 16 || s.find_first_not_of("0123456789.-") != std::string::npos)
        return false;
    char* end = nullptr;
    double const v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0' || v != v || v < -100000.0 || v > 100000.0)
        return false;
    out = float(v);
    return true;
}

enum class RowVerdict : std::uint8_t { Use, Parse, Key, Levels, Rank, R1 };

inline char const* RowVerdictName(RowVerdict v)
{
    switch (v)
    {
        case RowVerdict::Use: return "use";
        case RowVerdict::Parse: return "parse";
        case RowVerdict::Key: return "key";
        case RowVerdict::Levels: return "levels";
        case RowVerdict::Rank: return "rank";
        case RowVerdict::R1: return "r1";
    }
    return "?";
}

// One data row. A race key covers levels <= 12 only, a faction key levels >= 13 only; min <= max <= MaxLevel; rank 1 or 2;
// r1_min_level only on a race key and inside the row's levels (+1: "from the level after the band" is allowed).
inline RowVerdict ParseRow(std::vector<std::string> const& fields, Header const& header, Row& row)
{
    auto const at = [&](int c) -> std::string const& {
        static std::string const empty;
        std::size_t const i = std::size_t(header.col[c]);
        return i < fields.size() ? fields[i] : empty;
    };
    row = Row();
    row.keyIndex = KeyIndex(at(ColKey));
    if (!ParseUint(at(ColMinLevel), row.minLevel) || !ParseUint(at(ColMaxLevel), row.maxLevel) ||
        !ParseUint(at(ColZoneId), row.zoneId) || !ParseUint(at(ColRank), row.rank) || !ParseUint(at(ColHubMap), row.hubMap) ||
        !ParseFloat(at(ColHubX), row.hubX) || !ParseFloat(at(ColHubY), row.hubY) || !ParseFloat(at(ColHubZ), row.hubZ) ||
        !ParseUint(at(ColMobLevel), row.mobLevel) || !ParseUint(at(ColZ2Sec), row.z2Sec) ||
        !ParseUint(at(ColR1MinLevel), row.r1MinLevel) || row.zoneId == 0)
        return RowVerdict::Parse;
    if (row.keyIndex == 0)
        return RowVerdict::Key;
    if (row.minLevel == 0 || row.minLevel > row.maxLevel || row.maxLevel > MaxLevel || row.mobLevel > MaxLevel)
        return RowVerdict::Levels;
    if (IsRaceKey(row.keyIndex) ? row.maxLevel > RaceKeyMaxLevel : row.minLevel <= RaceKeyMaxLevel)
        return RowVerdict::Levels;
    if (row.rank != 1 && row.rank != 2)
        return RowVerdict::Rank;
    if (row.r1MinLevel && (!IsRaceKey(row.keyIndex) || row.r1MinLevel < row.minLevel || row.r1MinLevel > row.maxLevel + 1))
        return RowVerdict::R1;
    return RowVerdict::Use;
}

// ---- lookup ---------------------------------------------------------------------------------------------------------
// The row of the plan zone: key matches, minLevel <= level <= maxLevel, lowest rank; on a tie the first row of the file.
// nullptr = no row (the planner then keeps the current zone: fb=band_table_off is only for a missing table).
inline Row const* Find(Table const& table, std::uint32_t keyIndex, std::uint32_t level)
{
    Row const* best = nullptr;
    if (keyIndex == 0)
        return nullptr;
    for (Row const& row : table.rows)
        if (row.keyIndex == keyIndex && row.minLevel <= level && level <= row.maxLevel && (!best || row.rank < best->rank))
            best = &row;
    return best;
}

// ---- lines ----------------------------------------------------------------------------------------------------------
struct LoadCounts
{
    std::uint32_t lines = 0;     // data lines seen
    std::uint32_t used = 0;
    std::uint32_t skipped = 0;   // parse + key + levels + rank + r1 + duplicate
    std::uint32_t duplicate = 0;
    std::uint32_t z2Set = 0;     // rows with z2_sec > 0
    std::uint32_t r1Set = 0;     // rows with r1_min_level > 0
};

// shaHex8: the first 8 hex digits of the file's SHA-256 (the kit pins the file by it).
inline std::string FormatLoaded(char const* shaHex8, LoadCounts const& c, std::string const& path)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "[BandTable] loaded sha=%s lines=%u rows=%u skipped=%u duplicate=%u z2_set=%u r1_set=%u path=%s",
        shaHex8, c.lines, c.used, c.skipped, c.duplicate, c.z2Set, c.r1Set, path.c_str());
    return buf;
}

// reason: unreadable (missing or not readable) | too_long | version (also a UTF-8 BOM before it) | header | empty
inline std::string FormatOff(char const* reason, std::string const& path)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[BandTable] off reason=%s path=%s", reason, path.c_str());
    return buf;
}

inline std::string FormatRowSkipped(std::uint32_t lineNo, char const* reason)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[BandTable] row_skipped line=%u reason=%s", lineNo, reason);
    return buf;
}
}
