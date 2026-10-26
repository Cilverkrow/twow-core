#include "CampBlockPolicy.h"
#include "DangerMapPolicy.h"

#include <cstdlib>
#include <iostream>
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
}

int main()
{
    using namespace ai::camp_block;

    // (a) a death counts only with enough living attackers; no creature fight never.
    Require(!Counts(0, 1), "0 attackers never count");
    Require(Counts(1, 1), "MinAttackers 1: any creature death");
    Require(!Counts(1, 2), "a lone target is below MinAttackers 2");
    Require(Counts(3, 2), "target + 2 adds count with MinAttackers 2");

    // A recorded death blocks points within the radius on the same map, for its seconds only.
    Ring ring;
    Require(!Blocked(ring, 0, 0.0f, 0.0f, 1000, 60.0f), "empty ring blocks nothing");
    Record(ring, 0, 100.0f, 100.0f, 1000, 1800);
    Require(Blocked(ring, 0, 140.0f, 140.0f, 2000, 60.0f), "57 yd away is inside 60");
    Require(!Blocked(ring, 0, 150.0f, 150.0f, 2000, 60.0f), "71 yd away is outside 60");
    Require(!Blocked(ring, 1, 100.0f, 100.0f, 2000, 60.0f), "another map is never blocked");
    Require(!Blocked(ring, 0, 100.0f, 100.0f, 2000, 0.0f), "radius 0 blocks nothing");
    Require(Blocked(ring, 0, 100.0f, 100.0f, 1000 + 1799 * 1000, 60.0f), "still blocked just before the end");
    Require(!Blocked(ring, 0, 100.0f, 100.0f, 1000 + 1800 * 1000, 60.0f), "free at the end");

    // Seconds 0 records nothing; the ring is bounded and overwrites the oldest death.
    Ring zero;
    Record(zero, 0, 0.0f, 0.0f, 1000, 0);
    Require(!Blocked(zero, 0, 0.0f, 0.0f, 1000, 60.0f), "seconds 0 records nothing");
    Ring full;
    for (std::uint32_t i = 0; i < MaxDeaths + 1; ++i)
        Record(full, 0, float(i) * 1000.0f, 0.0f, 1000, 1800);
    Require(!Blocked(full, 0, 0.0f, 0.0f, 2000, 60.0f), "the oldest death is overwritten");
    for (std::uint32_t i = 1; i < MaxDeaths + 1; ++i)
        Require(Blocked(full, 0, float(i) * 1000.0f, 0.0f, 2000, 60.0f), "the newer deaths stay");

    // getMSTime wrap: a block that started just before the wrap still ends.
    Ring wrap;
    Record(wrap, 0, 0.0f, 0.0f, 0xFFFFFF00u, 60);
    Require(Blocked(wrap, 0, 0.0f, 0.0f, 0x00000100u, 60.0f), "blocked across the timer wrap");
    Require(!Blocked(wrap, 0, 0.0f, 0.0f, 0xFFFFFF00u + 60000u, 60.0f), "free 60 s later across the wrap");

    // Quest destinations only rank lower, everything else is skipped; a free point is free.
    Require(Judge(false, false) == Verdict::Free, "not blocked: free");
    Require(Judge(false, true) == Verdict::Free, "not blocked quest destination: free");
    Require(Judge(true, false) == Verdict::Skip, "blocked grind/gather: skipped");
    Require(Judge(true, true) == Verdict::Fallback, "blocked quest destination: fallback, never a veto");

    // (b) DangerMap.CountAddDeaths: a crowd counts whatever the killer's level, only for a bot that reads the rule.
    namespace dm = ai::danger_map;
    dm::Params off;
    dm::Params on;
    on.addDeathMinAttackers = 3;
    std::vector<dm::Death> deaths;
    for (std::uint32_t victim = 1; victim <= 3; ++victim)
    {
        dm::Death d;
        d.time = 100;
        d.victim = victim;
        d.killerLevel = 8;
        d.victimLevel = 11;
        d.attackers = 3;
        deaths.push_back(d);
    }
    Require(dm::DangerousKillerLevel(deaths, 200, 11, off) == 0, "switch off: green killers never mark the cell");
    Require(dm::DangerousKillerLevel(deaths, 200, 11, on) == 8, "switch on: 3 bots with 3 attackers mark the cell");
    deaths[2].attackers = 2;
    Require(dm::DangerousKillerLevel(deaths, 200, 11, on) == 0, "2 attackers are below AddDeathMinAttackers 3 (MinDeaths 3 not met)");
    deaths[2].attackers = 4;
    deaths[1].victim = 1;
    Require(dm::DangerousKillerLevel(deaths, 200, 11, on) == 0, "MinDeaths counts different bots, also for add deaths");
    deaths[1].victim = 2;
    Require(dm::DangerousKillerLevel(deaths, 100 + on.windowSeconds, 11, on) == 0, "add deaths expire with the window");
    dm::Death high;
    high.time = 100;
    high.killerLevel = 20;
    high.victimLevel = 11;
    Require(dm::CountsAsDanger(high, 11, off), "the level rule is unchanged without the switch");
    dm::Death zeroLevel = deaths[0];
    zeroLevel.killerLevel = 0;
    std::vector<dm::Death> crowd{ zeroLevel, deaths[1], deaths[2] };
    Require(dm::DangerousKillerLevel(crowd, 200, 11, on) >= 1, "a counted cell never reports 0");

    std::cout << "camp_block_policy_tests passed\n";
    return 0;
}
