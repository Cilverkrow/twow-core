#include "DeathLoopPolicy.h"

#include <cstdlib>
#include <iostream>

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

ai::death_loop::Death At(std::uint32_t seconds, float x, float y, std::uint32_t map = 0)
{
    ai::death_loop::Death death;
    death.atSeconds = seconds;
    death.mapId = map;
    death.x = x;
    death.y = y;
    return death;
}
}

int main()
{
    using namespace ai::death_loop;

    Settings settings;
    settings.maxDeaths = 4;
    settings.windowSeconds = 900;
    settings.radius = 150.0f;

    // Live G4 (deaths.csv 2026-09-27): Konso, L9 hunter, Eastvale graveyard.
    // 08:56:48 at a Young Forest Bear 340 yd away, then Prowlers at the graveyard
    // 08:57:53, 08:59:42, 09:00:25, 09:03:11 (seconds after 08:56:48 below).
    std::deque<Death> konso;
    Record(konso, At(0, 1052.28f, -9492.41f), settings);
    Require(!IsLoop(konso, 0, settings), "one death is no loop");
    Record(konso, At(65, 1377.47f, -9608.54f), settings);
    Record(konso, At(174, 1375.67f, -9612.48f), settings);
    Record(konso, At(217, 1376.86f, -9604.04f), settings);
    Require(!IsLoop(konso, 217, settings), "the far first death does not complete the loop");
    Record(konso, At(383, 1387.03f, -9616.79f), settings);
    Require(IsLoop(konso, 383, settings), "four graveyard deaths in six minutes are a loop");
    Require(konso.size() == 4, "the record keeps only maxDeaths entries");

    // Once the loop is over the window it no longer counts.
    Require(!IsLoop(konso, 383 + 901, settings), "an old loop expires with the window");

    // A bot that dies four times spread over a zone is no loop.
    std::deque<Death> spread;
    Record(spread, At(0, 0.0f, 0.0f), settings);
    Record(spread, At(100, 400.0f, 0.0f), settings);
    Record(spread, At(200, 800.0f, 0.0f), settings);
    Record(spread, At(300, 1200.0f, 0.0f), settings);
    Require(!IsLoop(spread, 300, settings), "deaths far apart are no loop");

    // Four deaths at one spot but slower than the window are no loop.
    std::deque<Death> slow;
    for (std::uint32_t i = 0; i < 4; ++i)
        Record(slow, At(i * 400, 10.0f, 10.0f), settings);
    Require(!IsLoop(slow, 1200, settings), "deaths spread over more than the window are no loop");

    // Same coordinates on another map do not count together.
    std::deque<Death> maps;
    Record(maps, At(0, 5.0f, 5.0f, 0), settings);
    Record(maps, At(10, 5.0f, 5.0f, 1), settings);
    Record(maps, At(20, 5.0f, 5.0f, 0), settings);
    Record(maps, At(30, 5.0f, 5.0f, 0), settings);
    Require(!IsLoop(maps, 30, settings), "another map breaks the loop");

    // 0 = off: nothing is kept and nothing is a loop.
    Settings off = settings;
    off.maxDeaths = 0;
    std::deque<Death> none;
    for (std::uint32_t i = 0; i < 10; ++i)
        Record(none, At(i, 1.0f, 1.0f), off);
    Require(none.empty() && !IsLoop(none, 10, off), "MaxDeaths 0 switches the guard off");

    std::cout << "death_loop_policy_tests passed\n";
    return 0;
}
