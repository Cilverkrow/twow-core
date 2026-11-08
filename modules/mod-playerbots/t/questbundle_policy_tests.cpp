// A3 step 2 quest book bundle scoring (QuestBundlePolicy.h, AiPlayerbot.QuestBook.Bundle). Hand-rolled Require(), std::exit(1) on
// failure, no gtest, no game library.
#ifdef _WIN32
#include <windows.h>
#endif
#include "QuestBundlePolicy.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0005f;
}
}

int main()
{
    using namespace ai::questbundle;

    // 1) The radius clamp and the constants.
    {
        Require(ClampRadius(100) == 100 && ClampRadius(30) == 30 && ClampRadius(300) == 300, "radius in range");
        Require(ClampRadius(29) == 30 && ClampRadius(-5) == 30 && ClampRadius(301) == 300 && ClampRadius(100000) == 300, "radius clamp");
        Require(RadiusDefault == 100 && AcceptSeconds == 20.0f && MaxSites == 256, "constants of the plan");
    }

    // 2) Sites and the bundle: distinct quests, same map, 2D radius (z ignored), quest 0 never counted.
    {
        Point const site{0, 100.0f, 100.0f, 11};
        Require(SameSite(site, Point{0, 100.0f, 100.0f, 12}) && !SameSite(site, Point{1, 100.0f, 100.0f, 11}) &&
            !SameSite(site, Point{0, 100.5f, 100.0f, 11}), "same site: map and position exactly");
        Require(Within(site, Point{0, 160.0f, 180.0f, 1}, 100.0f), "60/80 = 100 yd: inside");
        Require(!Within(site, Point{0, 161.0f, 180.0f, 1}, 100.0f), "just outside");
        Require(!Within(site, Point{1, 100.0f, 100.0f, 1}, 100.0f), "other map: never");
        std::vector<Point> const points = {
            {0, 100.0f, 100.0f, 11}, {0, 100.0f, 100.0f, 12}, {0, 100.0f, 100.0f, 13},   // one spawn, three quests
            {0, 150.0f, 100.0f, 12},                                                    // the same quest at a neighbour
            {0, 150.0f, 100.0f, 14},                                                    // a fourth quest at 50 yd
            {0, 400.0f, 100.0f, 15},                                                    // 300 yd away
            {1, 100.0f, 100.0f, 16},                                                    // other map
            {0, 100.0f, 100.0f, 0}};                                                    // no quest
        Require(BundleSize(site, points, 100.0f) == 4, "bundle: 11, 12, 13, 14 (12 counted once)");
        Require(BundleSize(site, points, 30.0f) == 3, "radius 30: the spawn only");
        Require(BundleSize(site, points, 300.0f) == 5, "radius 300: + quest 15, never the other map");
        Require(BundleSize(Point{2, 0.0f, 0.0f, 0}, points, 300.0f) == 0, "no giver on the map: 0");
    }

    // 3) The score: quests per second; the plan's examples.
    {
        float const speed = 7.0f;
        float const lone100 = Score(1, 100.0f / speed);
        float const three300 = Score(3, 300.0f / speed);
        float const three250 = Score(3, 250.0f / speed);
        Require(Near(lone100, 0.0292f) && Near(three300, 0.0292f), "lone at 100 yd and three at 300 yd: about 0.029");
        Require(three250 > lone100 && Near(three250, 0.0312f), "three at 250 yd win (0.031)");
        Require(Score(0, 10.0f) == 0.0f, "empty bundle: 0");
        Require(Near(Score(2, -5.0f), Score(2, 0.0f)) && Near(Score(2, 0.0f), 0.05f), "negative route counts as 0");
        Require(Near(Score(2, std::numeric_limits<float>::quiet_NaN()), 0.05f), "NaN route counts as 0");
        Require(Near(Score(1, 0.0f), 1.0f / AcceptSeconds) && Near(Score(5, 0.0f), 1.0f / AcceptSeconds), "no route: 1 / AcceptSeconds");
        Require(Score(1, 10.0f) < Score(2, 10.0f) && Score(2, 10.0f) < 1.0f / AcceptSeconds, "same route: the bigger bundle wins");
    }

    // 4) The order: highest score first, stable on the first-fit order.
    {
        std::vector<std::size_t> const a = Order({0.029f, 0.031f, 0.020f});
        Require(a.size() == 3 && a[0] == 1 && a[1] == 0 && a[2] == 2, "best first");
        std::vector<std::size_t> const b = Order({0.03f, 0.03f, 0.03f});
        Require(b[0] == 0 && b[1] == 1 && b[2] == 2, "ties keep the first-fit order");
        std::vector<std::size_t> const c = Order({0.01f, 0.05f, 0.05f});
        Require(c[0] == 1 && c[1] == 2 && c[2] == 0, "equal best: the earlier one first");
        Require(Order({}).empty() && Order({0.5f}).size() == 1, "empty and one site");
    }

    std::cout << "questbundle_policy_tests: all passed\n";
    return 0;
}
