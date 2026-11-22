// A3 step 3 planner S5 F1 (twow-repo#726, F1-ENTWURF-v0.2; AiPlayerbot.Planner.GiverPrefilter, default 0): GiverPrefilterPolicy.h -
// the key check, the minimum and the portal term (the same float operations as the original), the exclusion test at the radius edge
// (a skipped giver is one the original range check drops too), and the verify line. The header names no game type, so this suite
// compiles it standalone; the list equality on real destinations is the verify mode's job (GiverPrefilterVerify, non-measured run).

#include "GiverPrefilterPolicy.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
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

using namespace ai::giver_prefilter;

// The original range test of TravelMgr::GetDestinations on DistanceTo = sqrt(sqDistance) (TravelMgr.h; written with the C sqrt
// on double as the worst case of the header's unqualified sqrt): true = the original drops the destination.
bool OriginalDrops(float sq, float maxDistance)
{
    float const distance = float(::sqrt(double(sq)));
    return (maxDistance > 0 && distance > maxDistance) || distance == FLT_MAX;
}

void TestValidMode()
{
    Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "modes 0-3 are valid (cohort rule)");
    Require(!ValidMode(4) && !ValidMode(0xFFFFFFFFu), "above 3 is not valid (loader sets 0)");
}

void TestTakeMin()
{
    float acc = FLT_MAX;
    TakeMin(acc, 5.0f);
    TakeMin(acc, 7.0f);
    Require(acc == 5.0f, "minimum kept");
    TakeMin(acc, 0.0f);
    Require(acc == 0.0f, "0 (inside the box) wins");
    float none = FLT_MAX;
    Require(none == FLT_MAX, "no term: FLT_MAX, as the original");
}

// PortalTerm is the original's "distance + sqDistance(portal)" with a smaller or equal second operand: float addition is monotone,
// so the term never exceeds the original's.
void TestPortalTermMonotone()
{
    float const transfers[] = { 0.0f, 1.0f, 2250000.0f, 1.0e7f, 3.3e9f };
    float const squares[] = { 0.0f, 0.5f, 1.0f, 9.0e5f, 2250000.0f, 1.7e8f };
    for (float d : transfers)
        for (float a : squares)
            for (float b : squares)
            {
                if (a > b)
                    continue;
                float const original = d + b;   // the original expression (WorldSquare.cpp MapWpSquare::sqOutDistance)
                Require(PortalTerm(d, a) <= original, "portal term <= the original term when the square bound <= sqDistance(portal)");
                Require(PortalTerm(d, b) == original, "portal term = the original term for the same operands (no formula of its own)");
            }
}

// The edge: for every lower bound lb <= sq near the radius, Excludes(lb) implies the original drops sq.
void TestExcludesEdge()
{
    float const radii[] = { 1500.0f, 1.0f, 10000.0f, 1234.5678f };
    for (float r : radii)
    {
        float const r2 = r * r;
        std::vector<float> values;
        float v = r2;
        for (int i = 0; i < 64; ++i)
            v = std::nextafter(v, 0.0f);
        for (int i = 0; i < 129; ++i)
        {
            values.push_back(v);
            v = std::nextafter(v, FLT_MAX);
        }
        for (float lb : values)
            for (float sq : values)
            {
                if (lb > sq)
                    continue;
                if (Excludes(lb, r))
                    Require(OriginalDrops(sq, r), "a skipped giver is dropped by the original range check too (ulp edge)");
            }
        for (float sq : values)
            Require(Excludes(sq, r) == OriginalDrops(sq, r), "lb = sq: the prefilter and the original agree exactly");
    }
    Require(Excludes(FLT_MAX, 1500.0f), "FLT_MAX (no reachable square) is skipped, as the original drops it");
    Require(!Excludes(FLT_MAX, 0.0f) && !Excludes(1.0e9f, 0.0f), "no radius (0): never skipped");
    Require(!Excludes(0.0f, 1500.0f), "inside the box: never skipped");
}

void TestFirstDiff()
{
    std::vector<int*> a, b;
    int x = 1, y = 2, z = 3;
    Require(FirstDiff(a, b) == -1, "two empty lists are equal");
    a = { &x, &y, &z };
    b = { &x, &y, &z };
    Require(FirstDiff(a, b) == -1, "same elements, same order: equal");
    b = { &x, &z, &y };
    Require(FirstDiff(a, b) == 1, "order differs at index 1");
    b = { &x, &y };
    Require(FirstDiff(a, b) == 2 && FirstDiff(b, a) == 2, "length differs: the shorter length");
}

void TestVerifyLine()
{
    Counts c;
    c.pre = 5800;
    c.xmap = 3100;
    c.inbox = 2;
    char buf[kVerifyBuf];
    std::size_t const len = FormatVerifyLine(buf, sizeof(buf), 1413, 164, 164, -1, c);
    std::string const line(buf);
    Require(len == line.size(), "returned length = string length");
    Require(line == "[PlanPrefilter] verify bot=1413 n_orig=164 n_pre=164 mismatch=0 first_idx=-1 pre=5800 xmap=3100 inbox=2",
        "verify line shape (equal)");
    FormatVerifyLine(buf, sizeof(buf), 1413, 165, 164, 37, c);
    Require(std::strstr(buf, " mismatch=1 first_idx=37 ") != nullptr, "a difference sets mismatch=1 and first_idx");
    Counts m;
    m.pre = m.xmap = m.inbox = 0xFFFFFFFFu;
    std::size_t const mlen = FormatVerifyLine(buf, sizeof(buf), 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFFFFFFFFFll, m);
    Require(mlen + 1 < sizeof(buf) && std::strstr(buf, " inbox=4294967295") != nullptr, "every field at its maximum fits kVerifyBuf");
    char small[12];
    std::size_t const cut = FormatVerifyLine(small, sizeof(small), 1, 1, 1, -1, c);
    Require(cut == sizeof(small) - 1 && std::strlen(small) == cut, "a short buffer cuts and terminates");
    Require(FormatVerifyLine(nullptr, 8, 1, 1, 1, -1, c) == 0 && FormatVerifyLine(small, 0, 1, 1, 1, -1, c) == 0, "no buffer = 0");
}
}

int main()
{
    TestValidMode();
    TestTakeMin();
    TestPortalTermMonotone();
    TestExcludesEdge();
    TestFirstDiff();
    TestVerifyLine();
    std::cout << "giver_prefilter_policy_tests: OK\n";
    return 0;
}
