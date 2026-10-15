#include "RoutePrecheckPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
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

void RequireText(std::string const& actual, std::string const& expected, char const* message)
{
    if (actual != expected)
    {
        std::cerr << "FAILED: " << message << "\n  got:      " << actual << "\n  expected: " << expected << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::route_precheck;

    // twow-repo#592 (PATH-02): the level is clamped to 0..4.
    Require(ClampLevel(-5) == 0 && ClampLevel(0) == 0, "0 and below = off");
    Require(ClampLevel(1) == 1 && ClampLevel(4) == 4, "1..4 kept");
    Require(ClampLevel(9) == MaxLevel, "above 4 clamped to 4");

    // The boxes cover the success criteria with the 0.25 yd margin: 1 yd 2D / 2 yd Z of the target leg plus the 0.5 lift
    // of findSmoothPath, and PathInfo::FindWalkPoly's 5 / 10 / 5 around the start.
    Require(EndHalfXY == 1.25f && EndHalfZ == 2.75f, "target box 1.25 / 2.75");
    Require(StartHalfXY == 5.25f && StartHalfZ == 10.25f, "start box 5.25 / 10.25");
    Require(EndHalfXY > EndLegReach && EndHalfZ > PathZReach + PolyEndLift, "target box strictly covers the criterion");
    Require(DirectHalfZ == EndHalfZ, "direct box: same Z as the target box");

    // Skips: level 1 (shadow) never skips anything; 2 the target check, 3 + start, 4 + direct; only on "no polygon".
    Kind const kinds[] = { Kind::End, Kind::Start, Kind::Direct };
    Result const results[] = { Result::NotRun, Result::ExemptTile, Result::ExemptWater, Result::ExemptOther, Result::Poly, Result::NoPoly };
    for (Kind kind : kinds)
        for (Result r : results)
        {
            Require(!Skips(0, kind, r), "off never skips");
            Require(!Skips(1, kind, r), "shadow never skips");
            if (r != Result::NoPoly)
                for (std::uint32_t level = 0; level <= MaxLevel; ++level)
                    Require(!Skips(level, kind, r), "only 'no polygon' skips");
        }
    Require(Skips(2, Kind::End, Result::NoPoly) && !Skips(2, Kind::Start, Result::NoPoly) && !Skips(2, Kind::Direct, Result::NoPoly), "level 2: target only");
    Require(Skips(3, Kind::End, Result::NoPoly) && Skips(3, Kind::Start, Result::NoPoly) && !Skips(3, Kind::Direct, Result::NoPoly), "level 3: + start");
    Require(Skips(4, Kind::End, Result::NoPoly) && Skips(4, Kind::Start, Result::NoPoly) && Skips(4, Kind::Direct, Result::NoPoly), "level 4: + direct");
    Require(!Skips(4, Kind::None, Result::NoPoly), "no kind, no skip");

    // Decide: exempt first, then tiles, polygon, water - each later question only while a skip is still possible.
    {
        int asked = 0;
        auto yes = [&asked]() { ++asked; return true; };
        auto no = [&asked]() { ++asked; return false; };
        Require(Decide(true, yes, no, no) == Result::ExemptOther && asked == 0, "exempt: nothing else asked");
        asked = 0;
        Require(Decide(false, no, no, no) == Result::ExemptTile && asked == 1, "tile missing: no polygon query");
        asked = 0;
        Require(Decide(false, yes, yes, no) == Result::Poly && asked == 2, "polygon found: no water query");
        asked = 0;
        Require(Decide(false, yes, no, yes) == Result::ExemptWater && asked == 3, "no polygon but water: exempt");
        asked = 0;
        Require(Decide(false, yes, no, no) == Result::NoPoly && asked == 3, "no polygon, no water: impossible");
    }

    // Direct-attempt classes as in the [RouteFailDetail] analyses.
    Require(GapClassOf(false, 5, 3.0f) == GapClass::Na, "not run = na");
    Require(GapClassOf(true, 0, -1.0f) == GapClass::Na, "other map (no path) = na");
    Require(GapClassOf(true, 1, 120.0f) == GapClass::Eq, "never left the start = eq (gap == dist)");
    Require(GapClassOf(true, 7, 4.9f) == GapClass::Lt5, "below 5 yd");
    Require(GapClassOf(true, 7, 5.0f) == GapClass::Lt25 && GapClassOf(true, 7, 24.9f) == GapClass::Lt25, "5 to 25 yd");
    Require(GapClassOf(true, 7, 25.0f) == GapClass::Ge25, "25 yd and more");

    // The build context belongs to exactly one getRoute call: same maps, start and target.
    {
        BuildContext b;
        b.mapId = 1;
        b.sx = 10.0f; b.sy = 20.0f; b.sz = 30.0f;
        b.endMapId = 1;
        b.ex = 40.0f; b.ey = 50.0f; b.ez = 60.0f;
        Require(b.Matches(1, 10.0f, 20.0f, 30.0f, 1, 40.0f, 50.0f, 60.0f), "own call matches");
        Require(!b.Matches(1, 10.0f, 20.0f, 30.0f, 0, 40.0f, 50.0f, 60.0f), "other target map");
        Require(!b.Matches(1, 10.5f, 20.0f, 30.0f, 1, 40.0f, 50.0f, 60.0f), "other start");
        Require(!b.Matches(1, 10.0f, 20.0f, 30.0f, 1, 40.0f, 50.0f, 61.0f), "other target");
    }

    // Counters and the [RoutePrecheck] line.
    {
        Counters c;
        c.Count(Kind::End, Result::NoPoly, GapClass::Eq);
        c.Count(Kind::End, Result::Poly, GapClass::Eq);
        c.Count(Kind::End, Result::NoPoly, GapClass::Lt25);
        c.Count(Kind::End, Result::ExemptWater, GapClass::Ge25);
        c.Count(Kind::End, Result::ExemptTile, GapClass::Na);
        c.Count(Kind::Start, Result::NoPoly, GapClass::Eq);
        c.Count(Kind::Start, Result::ExemptOther, GapClass::Eq);
        c.Count(Kind::Direct, Result::NoPoly, GapClass::Na);
        c.Count(Kind::Direct, Result::Poly, GapClass::Na);
        c.Wrong(Kind::Start);
        c.Skipped(Kind::End);
        c.Skipped(Kind::End);
        c.Skipped(Kind::Direct);
        RequireText(FormatLine(2, c.Take()),
            "[RoutePrecheck] checks=5 hits=2 wrong=1 end_nopoly=eq:1/2,lt5:0/0,lt25:1/1,ge25:0/1,na:0/1 start_checks=2"
            " start_nopoly=1 direct_checks=2 direct_nopoly=1 unknown_tile=1 exempt_water=1 exempt_other=1 wrong_end=0"
            " wrong_start=1 wrong_direct=0 skips_end=2 skips_start=0 skips_direct=1 level=2",
            "[RoutePrecheck] line (spec head, level last)");
        RequireText(FormatLine(1, c.Take()),
            "[RoutePrecheck] checks=0 hits=0 wrong=0 end_nopoly=eq:0/0,lt5:0/0,lt25:0/0,ge25:0/0,na:0/0 start_checks=0"
            " start_nopoly=0 direct_checks=0 direct_nopoly=0 unknown_tile=0 exempt_water=0 exempt_other=0 wrong_end=0"
            " wrong_start=0 wrong_direct=0 skips_end=0 skips_start=0 skips_direct=0 level=1",
            "taken once: the next minute starts at zero");
    }

    // [RouteFailDetail] appendix: nothing without the switch.
    {
        BuildContext off;
        Require(FormatDetail(off).empty(), "no detail appendix with the switch off");
        BuildContext b;
        b.level = 2;
        b.end = Result::NoPoly;
        b.start = Result::NotRun;
        b.direct = Result::ExemptOther;
        b.skipped = Kind::End;
        RequireText(FormatDetail(b), " precheck=nopoly precheck_start=- precheck_direct=other precheck_skip=end", "detail appendix");
    }

    // Shared by all map threads.
    {
        Counters c;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&c]() {
                for (int i = 0; i < 1000; ++i)
                    c.Count(Kind::End, (i % 2) ? Result::NoPoly : Result::Poly, GapClass::Lt5);
            });
        for (std::thread& thread : threads)
            thread.join();
        Snapshot const s = c.Take();
        Require(s.endChecks[std::size_t(GapClass::Lt5)] == 4000 && s.endHits[std::size_t(GapClass::Lt5)] == 2000, "no check lost");
    }

    // The active build is per thread.
    {
        BuildContext mine;
        ActiveBuild() = &mine;
        BuildContext* seen = &mine;
        std::thread other([&seen]() { seen = ActiveBuild(); });
        other.join();
        Require(seen == nullptr && ActiveBuild() == &mine, "another thread sees no active build");
        ActiveBuild() = nullptr;
    }

    std::cout << "route_precheck policy tests passed\n";
    return 0;
}
