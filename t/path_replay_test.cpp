// twow-repo#592 PATH-02 PR pair 0: `.mmaps replay` name guard, fixture rows, hexfloat format, limits and
// the getPathFromPath chain rule (src/game/Maps/PathReplay.h), no server needed.
#include "../src/game/Maps/PathReplay.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
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

struct P
{
    float x, y, z;
};

uint32_t Bits(float v)
{
    uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

// %a output read back with strtof gives the same bits (platform independent: glibc and MSVC print
// different digit counts, both exact).
bool RoundTrips(float v)
{
    std::string s;
    AppendPathReplayHex(s, v);
    char* end = nullptr;
    float const back = std::strtof(s.c_str(), &end);
    return end == s.c_str() + s.size() && Bits(back) == Bits(v);
}
}

int main()
{
    // Name guard: only plain names inside the fixed directory.
    Require(PathReplayNameAllowed("m2-fixture.tsv"), "plain name allowed");
    Require(PathReplayNameAllowed("seed592_a.v1"), "letters, digits, _ - . allowed");
    Require(PathReplayNameAllowed(std::string(PATH_REPLAY_MAX_NAME_LEN, 'a')), "name at the length limit");
    Require(!PathReplayNameAllowed(std::string(PATH_REPLAY_MAX_NAME_LEN + 1, 'a')), "name over the length limit");
    Require(!PathReplayNameAllowed(""), "empty name");
    Require(!PathReplayNameAllowed("."), "dot");
    Require(!PathReplayNameAllowed(".."), "dot dot");
    Require(!PathReplayNameAllowed("../mangosd.conf"), "parent directory");
    Require(!PathReplayNameAllowed("a..b"), "dot dot inside the name");
    Require(!PathReplayNameAllowed(".hidden"), "leading dot");
    Require(!PathReplayNameAllowed("/etc/passwd"), "absolute POSIX path");
    Require(!PathReplayNameAllowed("sub/fixture.tsv"), "slash");
    Require(!PathReplayNameAllowed("sub\\fixture.tsv"), "backslash");
    Require(!PathReplayNameAllowed("C:fixture.tsv"), "drive letter");
    Require(!PathReplayNameAllowed("a b"), "blank");
    Require(!PathReplayNameAllowed("fix\xc3\xa4.tsv"), "non-ASCII");

    // Limits: checked after a row, lines first.
    Require(PathReplayShouldStop(0, 0) == PathReplayStop::None, "fresh call runs");
    Require(PathReplayShouldStop(PATH_REPLAY_MAX_LINES - 1, PATH_REPLAY_BUDGET_MS - 1) == PathReplayStop::None, "below both limits");
    Require(PathReplayShouldStop(PATH_REPLAY_MAX_LINES, 0) == PathReplayStop::Lines, "line cap reached");
    Require(PathReplayShouldStop(1, PATH_REPLAY_BUDGET_MS) == PathReplayStop::Time, "time budget reached");
    Require(PathReplayShouldStop(PATH_REPLAY_MAX_LINES, PATH_REPLAY_BUDGET_MS) == PathReplayStop::Lines, "both: lines reported");
    Require(PATH_REPLAY_MAX_LINES == 500 && PATH_REPLAY_BUDGET_MS == 200, "limits of SYNTHESE 5.1");

    // Rows.
    PathReplayRow row;
    Require(ParsePathReplayLine("7 1 -1.5 2 3 4 5 6 unit 0", row) == PathReplayParse::Row, "single row");
    Require(row.id == 7 && row.mapId == 1 && row.sx == -1.5f && row.dz == 6.0f, "single row fields");
    Require(row.profile == PathReplayProfile::Unit && row.chain == 0, "single row profile and chain");
    Require(ParsePathReplayLine("8\t0\t0x1.8p+1\t1\t1\t2\t2\t2\tmap\t40\r", row) == PathReplayParse::Row, "tabs, hexfloat, CRLF");
    Require(row.sx == 3.0f && row.profile == PathReplayProfile::Map && row.chain == PATH_REPLAY_MAX_CHAIN, "hexfloat value, map, chain 40");
    Require(ParsePathReplayLine("", row) == PathReplayParse::Comment, "empty line");
    Require(ParsePathReplayLine("   \t\r", row) == PathReplayParse::Comment, "blank line");
    Require(ParsePathReplayLine("# id map sx sy sz dx dy dz profile chain", row) == PathReplayParse::Comment, "comment");
    Require(ParsePathReplayLine("  # indented comment", row) == PathReplayParse::Comment, "indented comment");
    PathReplayRow kept;
    kept.id = 99;
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit", kept) == PathReplayParse::Bad, "missing chain");
    Require(kept.id == 99, "bad line leaves the row untouched");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit 0 extra", row) == PathReplayParse::Bad, "extra field");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit 41", row) == PathReplayParse::Bad, "chain over 40");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit -1", row) == PathReplayParse::Bad, "negative chain");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 player 0", row) == PathReplayParse::Bad, "unknown profile");
    Require(ParsePathReplayLine("1 0 nan 2 3 4 5 6 unit 0", row) == PathReplayParse::Bad, "nan coordinate");
    Require(ParsePathReplayLine("1 0 1 inf 3 4 5 6 unit 0", row) == PathReplayParse::Bad, "infinite coordinate");
    Require(ParsePathReplayLine("1 0 1 2 3x 4 5 6 unit 0", row) == PathReplayParse::Bad, "trailing garbage");
    Require(ParsePathReplayLine("4294967296 0 1 2 3 4 5 6 unit 0", row) == PathReplayParse::Bad, "id over uint32");
    Require(ParsePathReplayLine("+1 0 1 2 3 4 5 6 unit 0", row) == PathReplayParse::Bad, "signed id");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit 0 #", row) == PathReplayParse::Bad, "no trailing comments");
    Require(ParsePathReplayLine("1 0 1 2 3 4 5 6 unit 0" + std::string(PATH_REPLAY_MAX_LINE_LEN, ' '), row) == PathReplayParse::Bad, "line too long");

    // Hexfloats: exact.
    float const values[] = { 0.0f, -0.0f, 1.0f, -1.5f, 0.1f, 1234.5678f, -8913.23f, 3.0e-39f, 3.4e38f };
    for (float v : values)
        Require(RoundTrips(v), "hexfloat reads back bit exact");
    {
        std::string s;
        AppendPathReplayHex(s, -0.0f);
        Require(s[0] == '-', "negative zero keeps its sign");
        std::string t;
        AppendPathReplayHex(t, 1.0f);
        Require(t.compare(0, 2, "0x") == 0, "hexfloat starts with 0x");
    }

    // Points: "x,y,z;x,y,z".
    {
        std::vector<P> none;
        Require(FormatPathReplayPoints(none).empty(), "no points, empty text");
        std::vector<P> two{ { 1.0f, 2.0f, 3.0f }, { -4.0f, 5.5f, 0.25f } };
        std::string const s = FormatPathReplayPoints(two);
        size_t commas = 0, semis = 0;
        for (char c : s)
        {
            commas += c == ',';
            semis += c == ';';
        }
        Require(commas == 4 && semis == 1, "three coordinates per point, ';' between points");
        std::string first;
        AppendPathReplayPoint(first, 1.0f, 2.0f, 3.0f);
        Require(s.compare(0, first.size(), first) == 0 && s[first.size()] == ';', "first point, then ';'");
    }

    // Chain rule (getPathFromPath): steps scripted per k.
    {
        P const start{ 0.0f, 0.0f, 0.0f };
        P const dest{ 100.0f, 0.0f, 0.0f };
        std::vector<std::vector<P>> legs;
        std::vector<uint32_t> types;
        auto step = [&](uint32_t k, float, float, float) { return types[k]; };
        auto points = [&](uint32_t k) -> std::vector<P> const& { return legs[k]; };

        // Two INCOMPLETE legs, the second one arrives.
        legs = { { { 0, 0, 0 }, { 30, 0, 0 }, { 60, 0, 0 } }, { { 60, 0, 0 }, { 100.05f, 0, 1.5f } } };
        types = { PATH_REPLAY_TYPE_INCOMPLETE, PATH_REPLAY_TYPE_NORMAL };
        PathReplayChainResult r = RunPathReplayChain(start, dest, 40, step, points);
        Require(r.steps == 2 && r.stop == PathReplayChainStop::Arrived, "chain arrives on step 2");
        Require(r.points == 1 + 2 + 1, "joined path without the repeated step starts");

        // Height off by 2 yd or more: not arrived, the chain goes on until max.
        legs = { { { 0, 0, 0 }, { 100, 0, 2.0f } }, { { 100, 0, 2.0f }, { 100, 0.5f, 2.0f } } };
        types = { PATH_REPLAY_TYPE_NORMAL, PATH_REPLAY_TYPE_NORMAL };
        r = RunPathReplayChain(start, dest, 2, step, points);
        Require(r.steps == 2 && r.stop == PathReplayChainStop::Max, "2 yd height is not arrived, max reached");

        // A step that ends where it started stalls; its points are not joined.
        legs = { { { 0, 0, 0 }, { 50, 0, 0 } }, { { 50, 0, 0 }, { 50.05f, 0, 0 } } };
        types = { PATH_REPLAY_TYPE_NORMAL, PATH_REPLAY_TYPE_INCOMPLETE };
        r = RunPathReplayChain(start, dest, 40, step, points);
        Require(r.steps == 2 && r.stop == PathReplayChainStop::Stall && r.points == 2, "stall after step 2");

        // NORMAL|NOT_USING_PATH (shortcut) and NOPATH end the chain like getPathStepFrom's {}.
        legs = { { { 0, 0, 0 }, { 100, 0, 0 } } };
        types = { PATH_REPLAY_TYPE_NORMAL | 0x0010 };
        r = RunPathReplayChain(start, dest, 40, step, points);
        Require(r.steps == 1 && r.stop == PathReplayChainStop::NoPath && r.points == 1, "shortcut type ends the chain");
        types = { 0x0008 };
        r = RunPathReplayChain(start, dest, 40, step, points);
        Require(r.stop == PathReplayChainStop::NoPath, "nopath ends the chain");
        legs.assign(1, std::vector<P>());
        types = { PATH_REPLAY_TYPE_NORMAL };
        r = RunPathReplayChain(start, dest, 40, step, points);
        Require(r.stop == PathReplayChainStop::NoPath, "empty path ends the chain");

        Require(std::strcmp(PathReplayChainStopName(PathReplayChainStop::Arrived), "arrived") == 0 &&
            std::strcmp(PathReplayChainStopName(PathReplayChainStop::NoPath), "nopath") == 0, "stop names");
    }

    std::cout << "path_replay_test passed\n";
    return 0;
}
