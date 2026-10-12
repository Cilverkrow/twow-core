#include "PathFailPolicy.h"

#include <cstdlib>
#include <string>
#include <iostream>
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
}

int main()
{
    using namespace ai::path_fail;

    // twow-repo#575 (PATH-01 #1): lines per minute, clamped.
    Require(ClampLinesPerMinute(0) == 0, "0 = summary only");
    Require(ClampLinesPerMinute(-3) == 0, "negative = summary only");
    Require(ClampLinesPerMinute(30) == 30, "30 kept");
    Require(ClampLinesPerMinute(100000) == MaxLinesPerMinute, "clamped to 600");

    // Every failure is counted, only the budget is logged; the summary goes to exactly one caller per minute.
    {
        Counters counters;
        std::uint32_t failed = 0, logged = 0, honest = 0;
        Require(!counters.Roll(600, failed, logged, honest), "no summary for the very first minute");
        Require(counters.Take(2) && counters.Take(2), "two lines within the budget");
        Require(!counters.Take(2), "third line over the budget");
        counters.honest.fetch_add(1);
        Require(!counters.Roll(659, failed, logged, honest), "same minute: no summary");
        Require(counters.Roll(660, failed, logged, honest), "next minute: summary");
        Require(failed == 3 && logged == 2 && honest == 1, "summary counts all failures, the logged lines and honest fails");
        Require(counters.Take(2), "budget starts again in the new minute");
    }

    // Shared by map threads: one summary per minute, the budget holds under concurrency.
    {
        Counters counters;
        std::uint32_t f = 0, l = 0, h = 0;
        counters.Roll(60, f, l, h);
        std::atomic<std::uint32_t> taken{ 0 };
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&counters, &taken]() {
                for (int i = 0; i < 1000; ++i)
                    if (counters.Take(30))
                        taken.fetch_add(1);
            });
        for (std::thread& thread : threads)
            thread.join();
        Require(taken.load() == 30, "exactly the budget is logged under concurrency");
        Require(counters.Roll(120, f, l, h) && f == 4000 && l == 30, "all 4000 failures counted, 30 logged");
    }

    // twow-repo#575 (PATH-01 b): flight results and dock waits per minute.
    {
        TaxiCounters taxi;
        std::uint32_t results[5] = {};
        std::uint32_t waits = 0, logged = 0;
        Require(!taxi.Roll(600, results, waits, logged), "taxi: no summary for the very first minute");
        taxi.Count(TaxiResult::Ok);
        taxi.Count(TaxiResult::NoPath);
        taxi.Count(TaxiResult::NoPath);
        taxi.Count(TaxiResult::TooFar);
        taxi.transportWaits.fetch_add(3);
        Require(taxi.Take(1) && !taxi.Take(1), "taxi: line budget");
        Require(taxi.Roll(660, results, waits, logged), "taxi: summary in the next minute");
        Require(results[0] == 1 && results[1] == 2 && results[2] == 0 && results[3] == 1 && results[4] == 0 && waits == 3 && logged == 1,
            "taxi: summary counts per result, waits, logged lines");
        Require(std::string(TaxiResultName(TaxiResult::TooFar)) == "tooFar" && std::string(TaxiResultName(TaxiResult::NoPath)) == "noPath",
            "taxi: result names");
    }

    // twow-repo#575 (PATH-01 #4): start-zone exclusion, old boxes vs zone boxes.
    Require(InCustomStartZone(1, -100.0f, -7500.0f, false) && InCustomStartZone(1, -100.0f, -7500.0f, true), "island centre: both");
    Require(!InCustomStartZone(1, -503.0f, -7134.0f, false), "Dazlon Brassdigger: outside the old box");
    Require(InCustomStartZone(1, -503.0f, -7134.0f, true), "Dazlon Brassdigger: inside the zone box");
    Require(!InCustomStartZone(1, -618.5f, -4251.7f, true), "Valley of Trials stays a target");
    Require(!InCustomStartZone(1, 819.0f, -5006.0f, true), "Sparkwater flying machine (Durotar) stays a target");
    Require(!InCustomStartZone(0, 3985.0f, -2706.0f, false), "Calhir Dawnchaser: outside the old box");
    Require(InCustomStartZone(0, 3985.0f, -2706.0f, true), "Calhir Dawnchaser: inside the zone boxes");
    Require(InCustomStartZone(0, 4300.0f, -2900.0f, true), "Alah'Thalas: inside the zone boxes");
    Require(InCustomStartZone(0, 3212.6f, -2501.4f, true), "high-elf start point: inside the zone boxes");
    Require(InCustomStartZone(0, 3500.0f, -2500.0f, false) && InCustomStartZone(0, 3500.0f, -2500.0f, true), "old box centre: both");
    Require(!InCustomStartZone(0, 3247.0f, -3048.0f, true), "Eastern Plaguelands spawn: not excluded");
    Require(!InCustomStartZone(0, 3130.0f, -2265.0f, true), "Eastern Plaguelands spawn near the boundary: not excluded");
    Require(!InCustomStartZone(0, 2950.0f, -1500.0f, true), "Western Plaguelands spawn: not excluded");
    Require(!InCustomStartZone(0, -8949.95f, -132.5f, true), "Northshire stays a target");
    Require(!InCustomStartZone(530, -100.0f, -7500.0f, true), "other maps: never");

    std::cout << "path_fail_policy_tests passed\n";
    return 0;
}
