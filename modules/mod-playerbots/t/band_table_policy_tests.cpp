// A3 step 3 planner P-1 (A3-S3-PLANER-ENTWURF.md v0.3 §6; AiPlayerbot.Planner.BandTable, default ""): the pure rules of the
// planner's band table (BandTablePolicy.h) - keys, header by name, row checks, lookup by lowest rank, line formats. The header
// names no game type, so this suite compiles it standalone. The loader (BandTable.cpp) is pinned by
// t/contracts.d/a3s3_planner_band_table_source_contract_tests.cmake.

#include "BandTablePolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

using namespace ai::band_table;

std::string const kHeader = "key\tmin_level\tmax_level\tzone_id\trank\thub_map\thub_x\thub_y\thub_z\tmob_level\tz2_sec\tr1_min_level";

Header MakeHeader()
{
    Header h;
    Require(ParseHeader(kHeader, h), "the canonical header parses");
    return h;
}

RowVerdict Parse(std::string const& line, Row& row)
{
    return ParseRow(SplitTabs(line), MakeHeader(), row);
}

void TestKeys()
{
    Require(KeyIndex("1") == 1 && KeyIndex("10") == 10, "race keys 1..10");
    Require(KeyIndex("0") == 0 && KeyIndex("11") == 0 && KeyIndex("01") == 0 && KeyIndex("") == 0, "no race key 0, 11, 01, empty");
    Require(KeyIndex("A0") == 11 && KeyIndex("A1") == 12 && KeyIndex("H0") == 13 && KeyIndex("H1") == 14, "faction keys");
    Require(KeyIndex("A2") == 0 && KeyIndex("X0") == 0 && KeyIndex("a0") == 0, "no other faction keys");
    Require(KeyFor(4, 'A', 1, 12) == 4, "race key up to level 12");
    Require(KeyFor(4, 'A', 1, 13) == 12 && KeyFor(2, 'H', 0, 20) == 13, "faction + continent from level 13");
    Require(KeyFor(4, 'A', 530, 20) == 0 && KeyFor(11, 'A', 0, 5) == 0 && KeyFor(4, 'X', 0, 20) == 0, "no key elsewhere");
}

void TestVersionAndHeader()
{
    Require(VersionLine("# version=2") && VersionLine("# version=2\r") && VersionLine("# version=2 note"), "version 2");
    Require(!VersionLine("# version=1") && !VersionLine("# version=20") && !VersionLine("version=2"), "not version 2");
    Require(SourceLine("# source=ob30 dauer-b 19.11.") && !SourceLine("# src="), "source line");

    Header h;
    Require(ParseHeader("note\t" + kHeader + "\textra", h), "extra columns are ignored");
    Require(h.col[ColKey] == 1 && h.col[ColR1MinLevel] == 12, "columns found by name");
    std::string reordered = "r1_min_level\tz2_sec\tmob_level\thub_z\thub_y\thub_x\thub_map\trank\tzone_id\tmax_level\tmin_level\tkey";
    Require(ParseHeader(reordered, h) && h.col[ColKey] == 11 && h.col[ColR1MinLevel] == 0, "any column order");
    Require(!ParseHeader("key\tmin_level\tmax_level\tzone_id\trank\thub_map\thub_x\thub_y\thub_z\tmob_level\tz2_sec", h),
        "a missing column fails the header");
}

void TestRows()
{
    Row row;
    Require(Parse("1\t1\t5\t12\t1\t0\t-8913.2\t-133.5\t80.5\t3\t0\t5", row) == RowVerdict::Use, "a race row");
    Require(row.keyIndex == 1 && row.minLevel == 1 && row.maxLevel == 5 && row.zoneId == 12 && row.rank == 1 &&
        row.hubMap == 0 && row.mobLevel == 3 && row.z2Sec == 0 && row.r1MinLevel == 5, "race row fields");
    Require(row.hubX < -8913.0f && row.hubX > -8914.0f && row.hubZ > 80.0f, "hub position");
    Require(Parse("A0\t13\t20\t40\t2\t0\t1\t2\t3\t16\t36000\t0", row) == RowVerdict::Use && row.keyIndex == 11 &&
        row.z2Sec == 36000, "a faction row");
    Require(Parse("1\t1\t5\t12\t1\t0\t1\t2\t3\t3\t0\t5\r", row) == RowVerdict::Use, "a CRLF row");

    Require(Parse("1\t1\t5\t12\t1\t0\tx\t2\t3\t3\t0\t0", row) == RowVerdict::Parse, "a bad float");
    Require(Parse("1\t1\t5\t0\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Parse, "zone 0");
    Require(Parse("1\t1\t5\t12\t1\t0\t1\t2\t3\t3\t0", row) == RowVerdict::Parse, "a missing field");
    Require(Parse("1\t-1\t5\t12\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Parse, "a negative level");
    Require(Parse("Z9\t1\t5\t12\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Key, "an unknown key");
    Require(Parse("1\t6\t5\t12\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Levels, "min above max");
    Require(Parse("1\t0\t5\t12\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Levels, "min level 0");
    Require(Parse("1\t10\t13\t12\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Levels, "a race row above level 12");
    Require(Parse("A0\t12\t20\t40\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Levels, "a faction row from level 12");
    Require(Parse("A0\t13\t64\t40\t1\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Levels, "above the max level");
    Require(Parse("1\t1\t5\t12\t3\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Rank, "rank 3");
    Require(Parse("1\t1\t5\t12\t0\t0\t1\t2\t3\t3\t0\t0", row) == RowVerdict::Rank, "rank 0");
    Require(Parse("A0\t13\t20\t40\t1\t0\t1\t2\t3\t3\t0\t15", row) == RowVerdict::R1, "R1 only on a race key");
    Require(Parse("1\t6\t10\t12\t1\t0\t1\t2\t3\t3\t0\t5", row) == RowVerdict::R1, "R1 below the row's levels");
    Require(Parse("1\t1\t5\t12\t1\t0\t1\t2\t3\t3\t0\t6", row) == RowVerdict::Use, "R1 = max + 1 is allowed");
    Require(Parse("1\t1\t5\t12\t1\t0\t1\t2\t3\t3\t0\t7", row) == RowVerdict::R1, "R1 beyond max + 1");
}

void TestFind()
{
    Table t;
    Row r;
    r.keyIndex = 1; r.minLevel = 1; r.maxLevel = 5; r.zoneId = 12; r.rank = 2; t.rows.push_back(r);
    r.zoneId = 85; r.rank = 1; t.rows.push_back(r);
    r.zoneId = 40; r.minLevel = 6; r.maxLevel = 10; t.rows.push_back(r);
    r.keyIndex = 11; r.minLevel = 13; r.maxLevel = 20; r.zoneId = 44; t.rows.push_back(r);

    Require(Find(t, 1, 3) && Find(t, 1, 3)->zoneId == 85, "the lowest rank wins over file order");
    Require(Find(t, 1, 5)->zoneId == 85 && Find(t, 1, 6)->zoneId == 40 && Find(t, 1, 10)->zoneId == 40, "level bounds inclusive");
    Require(!Find(t, 1, 11) && !Find(t, 2, 3) && !Find(t, 0, 3), "no row: other level, other key, no key");
    Require(Find(t, 11, 15)->zoneId == 44, "a faction row");

    Table tie;
    r.keyIndex = 3; r.minLevel = 1; r.maxLevel = 5; r.rank = 1; r.zoneId = 1; tie.rows.push_back(r);
    r.zoneId = 2; tie.rows.push_back(r);
    Require(Find(tie, 3, 2)->zoneId == 1, "a rank tie keeps the first row of the file");
}

void TestLines()
{
    LoadCounts c;
    c.lines = 9; c.used = 7; c.skipped = 2; c.duplicate = 1; c.z2Set = 0; c.r1Set = 3;
    Require(FormatLoaded("1a2b3c4d", c, "/etc/band.tsv") ==
        "[BandTable] loaded sha=1a2b3c4d lines=9 rows=7 skipped=2 duplicate=1 z2_set=0 r1_set=3 path=/etc/band.tsv", "loaded line");
    Require(FormatOff("version", "x.tsv") == "[BandTable] off reason=version path=x.tsv", "off line");
    Require(FormatRowSkipped(9, "rank") == "[BandTable] row_skipped line=9 reason=rank", "row line");
    Require(std::string(RowVerdictName(RowVerdict::R1)) == "r1" && std::string(ColumnName(ColZ2Sec)) == "z2_sec", "names");
}
}

int main()
{
    TestKeys();
    TestVersionAndHeader();
    TestRows();
    TestFind();
    TestLines();
    std::cout << "band_table_policy_tests: OK\n";
    return 0;
}
