#include "RouteTimePolicy.h"

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
    using namespace ai::route_time;

    // twow-repo#592 (PATH-02): steps of the direct attempt in log2 classes 0, 1, 2, 3-4, 5-8, 9-16, 17-40.
    Require(StepClass(0) == 0 && StepClass(1) == 1 && StepClass(2) == 2, "0, 1, 2 steps have their own class");
    Require(StepClass(3) == 3 && StepClass(4) == 3, "3-4 steps in class 4");
    Require(StepClass(5) == 4 && StepClass(8) == 4, "5-8 steps in class 8");
    Require(StepClass(9) == 5 && StepClass(16) == 5, "9-16 steps in class 16");
    Require(StepClass(17) == 6 && StepClass(40) == 6 && StepClass(255) == 6, "17-40 (and more) in class 40");

    // Phases: A* keeps runs and the longest run, steps keep the longest step.
    {
        RouteTimes t;
        t.Add(Phase::AStar, 300);
        t.Add(Phase::AStar, 900);
        t.Add(Phase::AStar, 100);
        t.Add(Phase::EndLeg, 4000);
        t.AddStep(700);
        t.AddStep(250);
        Require(t.astarUs == 1300 && t.astarN == 3 && t.astarMaxUs == 900, "A* sum, runs and longest run");
        Require(t.endlegUs == 4000 && t.endlegN == 1, "end leg sum and runs");
        Require(t.steps == 2 && t.stepMaxUs == 700, "steps and longest step");
    }

    // Null times: the timer reads no clock and writes nothing (the switch-off path).
    {
        PhaseTimer const off(nullptr, Phase::Build);
        RouteTimes t;
        {
            PhaseTimer const on(&t, Phase::Build);
        }
        Require(t.buildUs < 1000000, "a scoped timer adds its (short) scope");
    }

    // Token and detail: empty when not timed, so the M:route token and the detail line stay as they were.
    {
        RouteTimes off;
        Require(FormatToken(off).empty(), "no token appendix with the switch off");
        Require(FormatDetail(off).empty(), "no detail appendix with the switch off");

        RouteTimes t;
        t.on = true;
        t.directUs = 1210;
        t.directSteps = 2;
        t.nodesUs = 40;
        t.astarUs = 2900;
        t.astarN = 6;
        t.astarMaxUs = 800;
        t.endlegUs = 40100;
        t.endlegN = 5;
        t.hearthUs = 35;
        t.stepMaxUs = 1200;
        t.totalUs = 45786;
        t.pf = 17;
        RequireText(FormatToken(t), " d=1210/2 n=40 a=2900/6 e=40100/5 s=0/0 h=35 b=0 l=0 x=1200 pf=17", "M:route appendix");
        RequireText(FormatDetail(t), " total_us=45786 direct_us=1210 direct_steps=2 nodes_us=40 astar_us=2900 astar_n=6 astar_max_us=800"
            " endleg_us=40100 endleg_n=5 startleg_us=0 startleg_n=0 hearth_us=35 lock_us=0 step_max_us=1200 pf=17", "detail appendix");
        // The existing parsers read "M:route [0-9]+us pairs=[0-9]+ pts=[0-9]+": the appendix starts with a blank after pts=N.
        Require(FormatToken(t)[0] == ' ', "appendix separated from pts= by a blank");
    }

    // Counters: edges inclusive (the [BotUpdate] buckets start at 16 384 / 32 768 us), exchange semantics.
    {
        Counters c;
        RouteTimes below;
        below.totalUs = Ge16Us - 1;
        c.AddBuild(below);
        RouteTimes edges;
        edges.totalUs = Ge33Us;
        c.AddBuild(edges);
        Snapshot s = c.Take();
        Require(s.builds == 2 && s.ge16 == 1 && s.ge33 == 1 && s.ge50 == 0, "16383 us is below ge16, 32768 us counts for ge16 and ge33");
        Require(s.fail == 2 && s.ok == 0, "not found = fail");
        s = c.Take();
        Require(s.builds == 0 && s.totalUs == 0 && s.maxUs == 0, "taken once: the next minute starts at zero");
    }

    // The [RouteTime] line, field by field.
    {
        Counters c;
        RouteTimes ok;
        ok.found = ok.directOk = true;
        ok.totalUs = 16384;
        ok.directSteps = 1;
        ok.directUs = 1000;
        ok.stepMaxUs = 900;
        ok.pf = 1;
        c.AddBuild(ok);
        RouteTimes failed;
        failed.totalUs = 100000;
        failed.directSteps = 3;
        failed.directUs = 5000;
        failed.nodesUs = 300;
        failed.astarUs = 20000;
        failed.astarMaxUs = SliceLimitUs;
        failed.endlegUs = 60000;
        failed.startlegUs = 7000;
        failed.hearthUs = 2000;
        failed.lockUs = 10;
        failed.stepMaxUs = 15000;
        failed.pf = 40;
        c.AddBuild(failed);
        c.AddDispatch(800);
        c.AddDispatch(1500);
        RequireText(FormatLine(c.Take()),
            "[RouteTime] builds=2 ms=116 ge16=2 ge33=1 ge50=1 ge100=1 max_ms=100 direct_ms=6 astar_ms=20 endleg_ms=60 startleg_ms=7"
            " step_max_us=15000 dsteps_ok=0:0,1:1,2:0,4:0,8:0,16:0,40:0 dsteps_fail=0:0,1:0,2:0,4:1,8:0,16:0,40:0"
            " dispatch_n=2 dispatch_ms=2 dispatch_max_us=1500 ok=1 fail=1 nodes_ms=0 hearth_ms=2 build_ms=0 lock_ms=0"
            " step_max_ge12=1 astar_max_ge12=1 pf=41",
            "[RouteTime] line");
        RequireText(FormatLine(c.Take()),
            "[RouteTime] builds=0 ms=0 ge16=0 ge33=0 ge50=0 ge100=0 max_ms=0 direct_ms=0 astar_ms=0 endleg_ms=0 startleg_ms=0"
            " step_max_us=0 dsteps_ok=0:0,1:0,2:0,4:0,8:0,16:0,40:0 dsteps_fail=0:0,1:0,2:0,4:0,8:0,16:0,40:0"
            " dispatch_n=0 dispatch_ms=0 dispatch_max_us=0 ok=0 fail=0 nodes_ms=0 hearth_ms=0 build_ms=0 lock_ms=0"
            " step_max_ge12=0 astar_max_ge12=0 pf=0",
            "an empty minute");
    }

    // Shared by all map threads: no build and no maximum lost.
    {
        Counters c;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&c, t]() {
                for (int i = 0; i < 1000; ++i)
                {
                    RouteTimes r;
                    r.totalUs = std::uint64_t(t * 1000 + i);
                    c.AddBuild(r);
                    c.AddDispatch(std::uint64_t(i));
                }
            });
        for (std::thread& thread : threads)
            thread.join();
        Snapshot const s = c.Take();
        Require(s.builds == 4000 && s.dispatchN == 4000, "every build and dispatch counted");
        Require(s.maxUs == 3999 && s.dispatchMaxUs == 999, "maxima kept under concurrency");
    }

    // The active build is per thread.
    {
        RouteTimes mine;
        Active() = &mine;
        RouteTimes* seen = &mine;
        std::thread other([&seen]() { seen = Active(); });
        other.join();
        Require(seen == nullptr, "another thread sees no active build");
        Require(Active() == &mine, "this thread keeps its own");
        Active() = nullptr;
    }

    std::cout << "route_time policy tests passed\n";
    return 0;
}
