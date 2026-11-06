#include "IslandGuardPolicy.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// twow-repo#575 A3 step 4 M-c (AiPlayerbot.Path.IslandGuard): the pure rules of IslandGuardPolicy.h and the thread model of
// its PublishOnceCache (OB-00 06.11.2026: the lazily filled tile label cache is read and filled by several map threads).
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

struct Labels
{
    std::uint32_t key = 0;
    std::uint32_t stamp = 0;
    std::vector<std::uint16_t> comp;
};
}

int main()
{
    using namespace ai::island;

    // 1. Config loaders: an unknown mode is off; the other keys are clamped.
    {
        Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0-3 pass");
        Require(ClampMode(4) == 0 && ClampMode(-1) == 0 && ClampMode(99) == 0, "an unknown mode acts like 0");
        Require(ClampMaxPolys(200) == 200, "default threshold");
        Require(ClampMaxPolys(5) == MinMaxPolys && ClampMaxPolys(-3) == MinMaxPolys, "threshold floor");
        Require(ClampMaxPolys(70000) == MaxMaxPolys, "threshold ceiling (uint16 labels)");
        Require(ClampSnapDz(10.0f) == 10.0f && ClampSnapDz(1.0f) == MinSnapDz && ClampSnapDz(99.0f) == MaxSnapDz, "snap dz clamp");
        Require(ClampSnapDz(std::nanf("")) == MinSnapDz, "NaN snap dz -> floor");
        Require(ClampStepOffYards(7.0f) == 7.0f && ClampStepOffYards(0.0f) == 0.0f && ClampStepOffYards(-2.0f) == 0.0f,
            "step-off yards: 0 or below = no step-off");
        Require(ClampStepOffYards(40.0f) == MaxStepOffYards, "step-off ceiling");
        Require(DefaultMaxPolys == 200 && DefaultSnapDz == 10.0f && DefaultStepOffYards == 7.0f, "defaults as in conf.dist");
    }

    // 2. Tile verdict: complete small = island, large = main, small at the tile edge = check across the edge.
    {
        Require(FromTileLabels(36, false, 200) == TileVerdict::Island, "q959: a 36-poly component inside one tile is an island");
        Require(FromTileLabels(1, false, 200) == TileVerdict::Island, "a single poly (rock top) is an island");
        Require(FromTileLabels(199, false, 200) == TileVerdict::Island, "199 < 200");
        Require(FromTileLabels(200, false, 200) == TileVerdict::Main, "200 is main");
        Require(FromTileLabels(5000, true, 200) == TileVerdict::Main, "large inside the tile is main, border or not");
        Require(FromTileLabels(12, true, 200) == TileVerdict::CheckAcross, "small but touching the tile edge: decided across");
        Require(FromTileLabels(150, false, 100) == TileVerdict::Main, "the threshold is the configured one");
    }

    // 3. Across the edge: an open edge (neighbour tile not loaded) is never an island; else the capped size decides.
    {
        Require(IslandAcross(40, false, 200), "a closed 40-poly component across two tiles is an island");
        Require(!IslandAcross(200, false, 200), "the capped BFS reached the threshold: main");
        Require(!IslandAcross(10, true, 200), "an open edge counts as main (no snap in doubt)");
    }

    // 4. Snap and step-off targets.
    {
        Require(SnapCandidateOk(19.9f, 9.9f, 10.0f) && SnapCandidateOk(0.0f, -10.0f, 10.0f), "inside the arrival box");
        Require(!SnapCandidateOk(20.5f, 0.0f, 10.0f), "beyond SnapXY");
        Require(!SnapCandidateOk(5.0f, 10.5f, 10.0f) && !SnapCandidateOk(5.0f, -12.0f, 10.0f), "beyond the snap dz");
        Require(StepOffTargetOk(6.9f, 2.9f, 7.0f) && StepOffTargetOk(1.0f, -3.0f, 7.0f), "inside the step-off reach");
        Require(!StepOffTargetOk(7.1f, 0.0f, 7.0f), "beyond the step-off reach");
        Require(!StepOffTargetOk(2.0f, 3.5f, 7.0f), "too far up or down");
        Require(!StepOffTargetOk(0.5f, 0.0f, 0.0f), "reach 0 = no step-off");
    }

    // 5. Step-off gate: only the fallback, at most once per StepOffIntervalSeconds.
    {
        std::uint64_t const t = 1'800'000'000;
        Require(StepOffAllowed(false, false, false, false, t, 0), "first check");
        Require(!StepOffAllowed(false, false, false, false, t + 9, t), "within the interval");
        Require(StepOffAllowed(false, false, false, false, t + StepOffIntervalSeconds, t), "after the interval");
        Require(!StepOffAllowed(true, false, false, false, t, 0), "not on a transport");
        Require(!StepOffAllowed(false, true, false, false, t, 0), "not in water");
        Require(!StepOffAllowed(false, false, true, false, t, 0), "not flying");
        Require(!StepOffAllowed(false, false, false, true, t, 0), "not during a far move (its arrival is snapped)");
    }

    // 6. Snap names (the snap= field of [FarMoveArrive]).
    {
        Require(std::string(SnapName(ArrivalSnap::None)) == "none", "none");
        Require(std::string(SnapName(ArrivalSnap::IslandToMain)) == "island>main", "island>main");
        Require(std::string(SnapName(ArrivalSnap::IslandNoMain)) == "island>none", "island>none");
        Require(std::uint8_t(ArrivalSnap::Unchecked) == 0, "0 = unchecked (LastMovement default)");
    }

    // 7. PublishOnceCache, one thread: compute once per (key, stamp), a new stamp replaces, an old holder keeps its value.
    {
        PublishOnceCache<std::uint64_t, Labels> cache;
        int computes = 0;
        bool here = false;
        auto make = [&](std::uint32_t key, std::uint32_t stamp) { ++computes; Labels l; l.key = key; l.stamp = stamp; return l; };
        auto a = cache.Get(7, 1, [&] { return make(7, 1); }, &here);
        Require(here && computes == 1 && a->stamp == 1, "a miss computes and publishes");
        auto b = cache.Get(7, 1, [&] { return make(7, 1); }, &here);
        Require(!here && computes == 1 && a.get() == b.get(), "a hit returns the published object");
        auto c = cache.Get(7, 2, [&] { return make(7, 2); }, &here);
        Require(here && computes == 2 && c->stamp == 2 && a->stamp == 1, "a new salt relabels; the old holder keeps the old value");
        Require(cache.Size() == 1, "one slot per key");
    }

    // 8. PublishOnceCache, many threads on the same keys: every caller of one (key, stamp) gets the same object, exactly one
    //    publish per (key, stamp) is reported, and the values are complete (no torn reads).
    {
        PublishOnceCache<std::uint64_t, Labels> cache;
        constexpr int Threads = 8, Keys = 64, Rounds = 200;
        std::atomic<int> published{0}, computed{0};
        std::vector<std::vector<Labels const*>> seen(Threads, std::vector<Labels const*>(Keys, nullptr));
        std::atomic<bool> bad{false};
        std::vector<std::thread> pool;
        for (int t = 0; t < Threads; ++t)
            pool.emplace_back([&, t]
            {
                for (int r = 0; r < Rounds; ++r)
                    for (int k = 0; k < Keys; ++k)
                    {
                        int const key = (k + t * 7 + r) % Keys;
                        bool here = false;
                        auto const value = cache.Get(std::uint64_t(key), 3, [&]
                        {
                            computed.fetch_add(1);
                            Labels l;
                            l.key = std::uint32_t(key);
                            l.stamp = 3;
                            l.comp.assign(1000, std::uint16_t(key));
                            return l;
                        }, &here);
                        if (here)
                            published.fetch_add(1);
                        if (value->key != std::uint32_t(key) || value->stamp != 3 || value->comp.size() != 1000 ||
                            value->comp[999] != std::uint16_t(key))
                            bad = true;
                        if (!seen[t][key])
                            seen[t][key] = value.get();
                        else if (seen[t][key] != value.get())
                            bad = true;   // a second object for the same (key, stamp)
                    }
            });
        for (std::thread& th : pool)
            th.join();
        Require(!bad, "every caller sees one complete object per (key, stamp)");
        Require(published.load() == Keys, "exactly one publish per key");
        Require(computed.load() >= Keys, "a lost race may compute twice, but publishes once");
        for (int k = 0; k < Keys; ++k)
            for (int t = 1; t < Threads; ++t)
                Require(seen[t][k] == seen[0][k], "all threads hold the same object");
    }

    // 9. Counters: one summary per minute, step-off lines capped per minute.
    {
        Counters counters;
        Require(counters.LogDue(1'800'000'020) && !counters.LogDue(1'800'000'040), "one summary per minute");
        Require(counters.LogDue(1'800'000'080), "the next minute logs again");
        std::uint32_t allowed = 0;
        for (int i = 0; i < 100; ++i)
            allowed += counters.StepOffLineAllowed() ? 1 : 0;
        Require(allowed == StepOffLinesPerMinute, "step-off lines capped");
    }

    std::cout << "island_guard_policy_tests: ok\n";
    return 0;
}
