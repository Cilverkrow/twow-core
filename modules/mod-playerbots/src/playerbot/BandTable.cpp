#include "playerbot/playerbot.h"
#include "playerbot/BandTable.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <openssl/sha.h>

#include <atomic>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// A3 step 3 planner P-1 (A3-S3-PLANER-ENTWURF.md v0.3 §6; AiPlayerbot.Planner.BandTable, default ""). The pattern of the hub
// ladder loader (QuestBand.cpp LoadLadder), without its code: boot only on the world thread, fail closed above 1 MiB or 2000
// lines, publish last, never rebuild or free. The file's SHA-256 goes into the [BandTable] loaded line (the kit pins the file);
// nothing reads the table yet - the planner (P-2) does.
namespace
{
constexpr std::size_t MaxTableBytes = 1024 * 1024;
constexpr std::uint32_t MaxTableLines = 2000;

std::atomic<ai::band_table::Table const*> published{ nullptr };
bool loadAttempted = false;   // world thread only

void LogOff(char const* reason, std::string const& path)
{
    sLog.outError("%s", ai::band_table::FormatOff(reason, path).c_str());
}

std::string ShaHex8(std::string const& bytes)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<unsigned char const*>(bytes.data()), bytes.size(), hash);
    static char const* const digits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(digits[hash[i] >> 4]);
        out.push_back(digits[hash[i] & 0x0F]);
    }
    return out;
}
}

void ai::band_table::Load()
{
    std::string const path = sPlayerbotAIConfig.plannerBandTable;
    if (path.empty())
        return;   // default: no file I/O, no line

    // Boot only: the table is never rebuilt or freed (P-2 keeps row pointers).
    if (loadAttempted)
        return;
    loadAttempted = true;

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
    {
        LogOff("unreadable", path);
        return;
    }
    std::string bytes(MaxTableBytes + 1, '\0');
    file.read(&bytes[0], std::streamsize(bytes.size()));
    if (file.bad())
    {
        LogOff("unreadable", path);
        return;
    }
    bytes.resize(std::size_t(file.gcount()));
    if (bytes.size() > MaxTableBytes)
    {
        LogOff("too_long", path);
        return;
    }

    std::vector<std::string> lines;
    std::string::size_type start = 0;
    while (start < bytes.size())
    {
        if (lines.size() >= MaxTableLines)
        {
            LogOff("too_long", path);
            return;
        }
        std::string::size_type const end = bytes.find('\n', start);
        lines.push_back(bytes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    if (lines.empty() || !VersionLine(lines[0]))
    {
        LogOff("version", path);
        return;
    }
    Header header;
    if (lines.size() < 3 || !SourceLine(lines[1]) || !ParseHeader(lines[2], header))
    {
        LogOff("header", path);
        return;
    }

    Table* table = new Table();
    LoadCounts counts;
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> seen;   // (key, min_level, rank)
    for (std::size_t i = 3; i < lines.size(); ++i)
    {
        std::string const& line = lines[i];
        if (line.empty() || line == "\r" || line[0] == '#')
            continue;
        std::uint32_t const lineNo = std::uint32_t(i + 1);
        ++counts.lines;

        Row row;
        RowVerdict const verdict = ParseRow(SplitTabs(line), header, row);
        if (verdict != RowVerdict::Use)
        {
            ++counts.skipped;
            sLog.outBasic("%s", FormatRowSkipped(lineNo, RowVerdictName(verdict)).c_str());
            continue;
        }
        if (!seen.insert(std::make_tuple(row.keyIndex, row.minLevel, row.rank)).second)
        {
            ++counts.skipped;
            ++counts.duplicate;
            sLog.outBasic("%s", FormatRowSkipped(lineNo, "duplicate").c_str());
            continue;
        }
        if (row.z2Sec)
            ++counts.z2Set;
        if (row.r1MinLevel)
            ++counts.r1Set;
        table->rows.push_back(row);
    }
    counts.used = std::uint32_t(table->rows.size());

    if (table->rows.empty())
    {
        delete table;
        LogOff("empty", path);
        return;
    }

    sLog.outBasic("%s", FormatLoaded(ShaHex8(bytes).c_str(), counts, path).c_str());
    published.store(table, std::memory_order_release);
}

ai::band_table::Table const* ai::band_table::Get()
{
    return published.load(std::memory_order_acquire);
}
