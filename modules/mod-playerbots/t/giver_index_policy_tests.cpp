// A3 step 3 planner S5 F1-IX (twow-repo#726; IX-ENTWURF-v0.2, OB-60 pre-review 4376ef95, OB-10 OK 773531cd;
// AiPlayerbot.Planner.GiverIndex, default 0): the giver index of GiverIndexPolicy.h - the bucket function, the axes, the superset
// against a brute-force reference of the original box distance (radius 300, 1500 and 20000), the radius edge, the wide list, the
// dedupe by position with the generation wrap, and the verify line. The header names no game type, so this suite compiles it
// standalone; the list equality on real destinations is the [PlanIndex] verify mode.

#include "GiverIndexPolicy.h"
#include "GiverPrefilterPolicy.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
using namespace ai::giver_index;

void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// The original's 2D box distance (WorldPointSquare::sqOutDistance: clamp the point to the box, then the squared 2D distance).
float BoxSq(Box const& b, float x, float y)
{
    float const sx = std::min(std::max(b.minX, x), b.maxX);
    float const sy = std::min(std::max(b.minY, y), b.maxY);
    float const dx = x - sx;
    float const dy = y - sy;
    return dx * dx + dy * dy;
}

// The original keeps a destination when float(sqrt(sq)) <= r (TravelMgr: DistanceTo > maxDistance drops it).
bool InReach(Box const& b, float x, float y, float r)
{
    return !ai::giver_prefilter::Excludes(BoxSq(b, x, y), r);
}

std::set<std::uint32_t> Query(Grid const& g, std::uint32_t map, float x, float y, float r, std::uint32_t* probed = nullptr)
{
    std::set<std::uint32_t> got;
    std::uint32_t const p = g.Collect(map, x, y, r, [&](std::uint32_t ord) { got.insert(ord); });
    g.Wide(map, [&](std::uint32_t ord) { got.insert(ord); });
    if (probed)
        *probed = p;
    return got;
}

void TestBucket()
{
    Require(Bucket(-kHalf) == 0, "the map edge is bucket 0");
    Require(Bucket(-kHalf - 5000.0f) == 0 && Bucket(kHalf + 5000.0f) == kCells - 1, "beyond the map clamps");
    Require(Bucket(std::nanf("")) == 0, "NaN is bucket 0");
    Require(Bucket(0.0f) == 32, "the map centre is bucket 32");
    Require(Bucket(-0.01f) == 31 && Bucket(0.01f) == 32, "negative coordinates fall below the centre bucket");
    int last = Bucket(-20000.0f);
    for (float v = -20000.0f; v <= 20000.0f; v += 7.3f)
    {
        int const b = Bucket(v);
        Require(b >= last, "Bucket is monotone");
        last = b;
    }
}

// OB-60 1: the box values are the ones sqOutDistance clamps against; an asymmetric box far off-axis finds the giver at the right
// place and not at the swapped one.
void TestAxes()
{
    std::vector<Box> boxes = { Box{ 1, 100.0f, 5000.0f, 110.0f, 5010.0f, 0 } };
    Grid g;
    g.Build(boxes);
    Require(Query(g, 1, 105.0f, 6400.0f, 1500.0f).count(0) == 1, "found 1390 yd north of the box");
    Require(Query(g, 1, 5005.0f, 105.0f, 1500.0f).empty(), "not found at the swapped point");
    Require(Query(g, 1, -1400.0f, 5005.0f, 1500.0f).count(0) == 1, "found 1500 yd west (negative x)");
    Require(Query(g, 2, 105.0f, 5005.0f, 1500.0f).empty(), "another map has no box");
}

// The superset: every box the original keeps is a candidate, for radius 300, 1500 and 20000 (larger than the map; OB-10 B1).
void TestSupersetRandom()
{
    std::uint32_t seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    auto coord = [&]() { return float(int(rnd() % 34000u) - 17000) + float(rnd() % 1000u) / 1000.0f; };
    std::vector<Box> boxes;
    for (std::uint32_t i = 0; i < 10000; ++i)
    {
        float const x = coord();
        float const y = coord();
        float const w = (rnd() % 10u == 0) ? float(rnd() % 3000u) : float(rnd() % 40u);
        float const h = (rnd() % 10u == 0) ? float(rnd() % 3000u) : float(rnd() % 40u);
        boxes.push_back(Box{ rnd() % 3u, x, y, x + w, y + h, i });
    }
    Grid g;
    g.Build(boxes);
    Require(g.BoxCount() == 10000 && g.Maps().size() == 3, "all boxes on three maps");
    Require(g.WideCount() > 0, "some boxes go to the wide list");
    float const radii[3] = { 300.0f, 1500.0f, 20000.0f };
    std::uint64_t sumCand = 0, sumKeep = 0;
    for (std::uint32_t q = 0; q < 1000; ++q)
    {
        std::uint32_t const map = rnd() % 3u;
        float const x = coord();
        float const y = coord();
        float const r = radii[q % 3];
        std::set<std::uint32_t> const got = Query(g, map, x, y, r);
        for (Box const& b : boxes)
            if (b.map == map && InReach(b, x, y, r))
            {
                Require(got.count(b.ord) == 1, "superset: a box in reach is a candidate");
                ++sumKeep;
            }
        sumCand += got.size();
    }
    Require(sumCand >= sumKeep, "candidates >= kept");
    std::cout << "giver_index superset: kept " << sumKeep << " candidates " << sumCand << '\n';
}

// The radius edge: a box exactly at r and one float step inside / outside.
void TestEdge()
{
    float const r = 1500.0f;
    for (float x0 : { 1500.0f, std::nextafter(1500.0f, 0.0f), std::nextafter(1500.0f, 3000.0f) })
    {
        std::vector<Box> boxes = { Box{ 0, x0, -3.0f, x0 + 2.0f, 3.0f, 0 } };
        Grid g;
        g.Build(boxes);
        bool const keep = InReach(boxes[0], 0.0f, 0.0f, r);
        if (keep)
            Require(Query(g, 0, 0.0f, 0.0f, r).count(0) == 1, "edge: a kept box is a candidate");
    }
    // the portal skip at sqrt(t) = r +- 1 ulp is the F1 comparison (GiverPrefilterPolicy.h Excludes): a portal the skip keeps is
    // queried; one it skips has an original term t + box >= t that the original drops too
    float const t = r * r;
    Require(!ai::giver_prefilter::Excludes(t, r), "a portal exactly at r is kept");
    Require(!ai::giver_prefilter::Excludes(std::nextafter(t, 0.0f), r), "a portal 1 ulp inside r is kept");
    Require(ai::giver_prefilter::Excludes((r + 0.01f) * (r + 0.01f), r), "a portal beyond r is skipped");
    std::vector<Box> portalSide = { Box{ 3, 200.0f, 200.0f, 210.0f, 210.0f, 0 } };
    Grid pg;
    pg.Build(portalSide);
    Require(Query(pg, 3, 205.0f, 205.0f + r, r).count(0) == 1, "a box r from the portal point is a candidate");
}

// A box over more than kWideSpan buckets and a box with a coordinate that is not finite go to the wide list.
void TestWide()
{
    std::vector<Box> boxes = {
        Box{ 7, -8000.0f, -8000.0f, 8000.0f, 8000.0f, 0 },
        Box{ 7, 0.0f, 0.0f, 10.0f, 10.0f, 1 },
        Box{ 7, std::nanf(""), 0.0f, 10.0f, 10.0f, 2 },
    };
    Grid g;
    g.Build(boxes);
    Require(g.WideCount() == 2, "the big box and the NaN box are wide");
    std::set<std::uint32_t> const got = Query(g, 7, 16000.0f, 16000.0f, 300.0f);
    Require(got.count(0) == 1 && got.count(2) == 1 && got.count(1) == 0, "wide boxes are candidates whenever the map is queried");
}

// An empty grid and a map without boxes probe nothing.
void TestEmpty()
{
    Grid g;
    g.Build({});
    std::uint32_t probed = 7;
    Require(Query(g, 0, 0.0f, 0.0f, 1500.0f, &probed).empty() && probed == 0, "empty grid");
}

// The dedupe by position: a repeat is refused, an ordinal outside N is refused, the generation wrap clears the marks once.
void TestStamp()
{
    Stamp s;
    s.Begin(4);
    Require(s.Take(1) && !s.Take(1) && s.Take(3) && !s.Take(4), "repeat and out of range refused");
    s.Begin(4);
    Require(s.Take(1), "a new generation takes again");
    s.SetGenerationForTest(0xFFFFFFFFu);
    s.Take(2);   // marked with the last generation
    s.Begin(4);   // wraps to 0: cleared, restarts at 1
    Require(s.Generation() == 1 && s.Take(2) && s.Take(1), "the wrap clears every mark");
    s.Begin(2);   // a new N resizes
    Require(s.Take(1) && !s.Take(2), "resized to the built N");
}

// The cell size (OB-60 5): buckets and candidates for one grid cell and a half cell, printed for the review.
void TestCellPrint()
{
    std::uint32_t seed = 99u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed; };
    std::vector<Box> boxes;
    for (std::uint32_t i = 0; i < 6000; ++i)
    {
        float const x = float(int(rnd() % 30000u) - 15000);
        float const y = float(int(rnd() % 30000u) - 15000);
        boxes.push_back(Box{ 0, x, y, x + float(rnd() % 30u), y + float(rnd() % 30u), i });
    }
    for (float cell : { kCell, kCell / 2.0f })
    {
        Grid g;
        g.Build(boxes, cell);
        std::uint64_t probed = 0, cand = 0, keep = 0;
        for (std::uint32_t q = 0; q < 200; ++q)
        {
            float const x = float(int(rnd() % 30000u) - 15000);
            float const y = float(int(rnd() % 30000u) - 15000);
            std::uint32_t p = 0;
            std::set<std::uint32_t> const got = Query(g, 0, x, y, 1500.0f, &p);
            probed += p;
            cand += got.size();
            for (Box const& b : boxes)
                if (InReach(b, x, y, 1500.0f))
                    ++keep;
        }
        Require(cand >= keep, "cell print: superset");
        std::cout << "giver_index cell=" << cell << " buckets/query=" << probed / 200 << " candidates/kept="
                  << (keep ? double(cand) / double(keep) : 0.0) << '\n';
    }
}

void TestVerifyLine()
{
    Counts c;
    c.cand = 412;
    c.maps = 3;
    c.portals = 2;
    c.buckets = 63;
    c.wide = 1;
    char buf[kVerifyBuf];
    std::size_t const len = FormatVerifyLine(buf, sizeof(buf), 75, 172, 172, -1, c);
    Require(std::string(buf) == "[PlanIndex] verify bot=75 n_orig=172 n_ix=172 mismatch=0 first_idx=-1 cand=412 maps=3 portals=2 "
        "buckets=63 wide=1" && len == std::strlen(buf), "verify line");
    FormatVerifyLine(buf, sizeof(buf), 75, 172, 171, 171, c);
    Require(std::strstr(buf, " mismatch=1 first_idx=171 ") != nullptr, "verify mismatch");
    Require(FormatVerifyLine(nullptr, 8, 1, 1, 1, -1, c) == 0, "no buffer = 0");
}
}

int main()
{
    TestBucket();
    TestAxes();
    TestSupersetRandom();
    TestEdge();
    TestWide();
    TestEmpty();
    TestStamp();
    TestCellPrint();
    TestVerifyLine();
    std::cout << "giver_index_policy_tests: OK\n";
    return 0;
}
