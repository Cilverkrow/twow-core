// perfmon_collection_tests -- does the bot performance monitor actually collect?
//
// Why this suite exists
// ---------------------
// `.perfmon toggle` printed "Performance monitor enabled" and `.perfmon tick`
// then printed nothing, for as long as anyone had tried it. The cause was not in
// this file's subject at all: PerformanceMonitor::Init() is the only writer of
// mapsData, start() returns nullptr for an unregistered (mapId, instanceId), and
// the single call to Init() sat near the bottom of
// RandomPlayerbotMgr::UpdateAIInternal -- below the randomBotAutologin/enabled
// gate and below both `return`s of the persistent-roster branch. On a server
// running a persistent roster the call was unreachable, so mapsData stayed
// empty, every probe handed back nullptr, and PrintStats returned at its first
// line. An instrument that reports "enabled" while measuring nothing is worse
// than one that is off.
//
// Two tests guard the two halves of that:
//   * this suite pins the collector's contract -- no buckets means no
//     collection, buckets plus a probe means a real number comes out;
//   * perfmon_init_reachable (t/perfmon_init_reachable_tests.py) pins the call
//     site, i.e. that Init() is still reached before every early return.
//
// Neither is sufficient alone: the collector was never broken, and the call site
// compiles fine wherever it sits.
//
// How it compiles
// ---------------
// It links the real src/playerbot/PerformanceMonitor.cpp against the doubles in
// t/stubs-perfmon (a capturing sLog, a one-field config, a two-field bot). The
// real .cpp is compiled from a configure-time copy in the build tree, because a
// quote-include is resolved against the directory of the file containing the
// directive first -- next to the original, `#include "playerbot.h"` can only
// ever find the real one, whatever the -I order says. See tests.cmake.
//
// Hand-rolled assertions, no gtest: the same shape as
// persistent_active_roster_tests and the event-store contract suite.

#include "playerbot.h"                  // t/stubs-perfmon: capturing sLog, ChatHandler
#include "playerbot/PlayerbotAIConfig.h" // t/stubs-perfmon: perfMonEnabled
#include "PlayerbotAI.h"                // t/stubs-perfmon: Player, AiObjectContext
#include "PerformanceMonitor.h"         // the real header under test

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
int g_failures = 0;

void Check(bool condition, char const* what)
{
    if (condition)
    {
        std::cout << "  ok   " << what << "\n";
        return;
    }

    std::cout << "  FAIL " << what << "\n";
    ++g_failures;
}

// A probe has to last longer than the monitor's resolution to be recorded:
// PerformanceMonitorOperation stores milliseconds and finish() only folds in
// `elapsed` when it is greater than zero. 5 ms is comfortably above that and
// still instant on any runner.
void SpendMeasurableTime()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

bool LogContains(char const* needle)
{
    return sLog.Text().find(needle) != std::string::npos;
}

// Every test shares the process-wide singleton, so each one starts from a known
// state. Reset() zeroes the counters it already has; the buckets themselves are
// only ever added, which is exactly why the tests below that must see an empty
// mapsData run first.
void ClearLog() { sLog.Clear(); }

// ---------------------------------------------------------------------------
// 1. Disabled monitor: nothing is collected and nothing is printed.
// ---------------------------------------------------------------------------
void TestDisabledCollectsNothing()
{
    std::cout << "disabled monitor collects nothing\n";

    sPlayerbotAIConfig.perfMonEnabled = false;
    ClearLog();

    sPerformanceMonitor.Init(0, 0);
    Check(!sPerformanceMonitor.IsCollecting(),
          "Init() registers no bucket while the monitor is disabled");

    auto probe = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
    Check(probe == nullptr, "start() hands back nothing while the monitor is disabled");

    Check(!sPerformanceMonitor.PrintStats(), "PrintStats() reports that it printed nothing");
    Check(sLog.Lines().empty(), "PrintStats() wrote no lines");
}

// ---------------------------------------------------------------------------
// 2. Enabled but never Init()ed: the exact shape of the reported bug.
//
// This is what a persistent-roster server looked like. The toggle is on, the
// operator has been told the monitor is enabled, and not one probe records
// anything, because no (mapId, instanceId) bucket exists.
// ---------------------------------------------------------------------------
void TestEnabledWithoutInitCollectsNothing()
{
    std::cout << "enabled but un-Init()ed monitor collects nothing (the reported bug)\n";

    sPlayerbotAIConfig.perfMonEnabled = true;
    ClearLog();

    Check(!sPerformanceMonitor.IsCollecting(), "no buckets exist before Init()");

    auto tickProbe = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
    Check(tickProbe == nullptr, "the map-less FullTick probe is dropped without a bucket");

    auto mapProbe = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAI 0", nullptr, 0, 0);
    Check(mapProbe == nullptr, "an explicit (map 0, instance 0) probe is dropped too");

    Player bot(1, 0);
    AiObjectContext context;
    PlayerbotAI ai(&bot, &context);
    auto aiProbe = sPerformanceMonitor.start(PERF_MON_ACTION, "SomeAction", &ai);
    Check(aiProbe == nullptr, "the PlayerbotAI overload is dropped too");

    Check(!sPerformanceMonitor.PrintStats(true), "PrintStats(tick) reports that it printed nothing");
    Check(sLog.Lines().empty(), "PrintStats(tick) wrote no lines: this is the silent failure");
}

// ---------------------------------------------------------------------------
// 3. Init() then probe: a real elapsed time comes out the other end.
// ---------------------------------------------------------------------------
void TestInitThenProbeCollects()
{
    std::cout << "Init() then probe produces a measured number\n";

    sPlayerbotAIConfig.perfMonEnabled = true;

    // Register the map-less bucket the way RandomPlayerbotMgr::UpdateAIInternal
    // now does on every random-bot update tick.
    sPerformanceMonitor.Init(0, 0);
    Check(sPerformanceMonitor.IsCollecting(), "Init() registers a bucket when enabled");

    sPerformanceMonitor.Reset();
    ClearLog();

    {
        auto fullTick = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
        Check(fullTick != nullptr, "the FullTick probe is live once a bucket exists");
        SpendMeasurableTime();
    } // ~PerformanceMonitorOperation folds the elapsed time in here

    Check(sPerformanceMonitor.PrintStats(true), "PrintStats(tick) reports that it printed");
    Check(LogContains("PER TICK"), "the per-tick report header is in the log");
    Check(LogContains("PlayerbotAIBase::FullTick"), "the FullTick row is in the report");
    Check(LogContains("Estimated avg diff"),
          "the report carries a measured average, so `total` was non-zero");

    // The measured average is the number the roster gate needs to exist at all.
    // Assert it is a real quantity rather than a formatted zero: the row is
    // printed as "<pct>% <ms>ms | <min> .. <max> (...)", and min must be at
    // least the 5 ms the probe was held open, minus nothing -- the monitor
    // truncates to whole milliseconds, so 4 is the honest floor.
    bool sawMeasuredMilliseconds = false;
    for (auto const& line : sLog.Lines())
    {
        if (line.find("PlayerbotAIBase::FullTick") == std::string::npos)
            continue;
        std::size_t const bar = line.find('|');
        if (bar == std::string::npos)
            continue;
        long const minTime = std::strtol(line.c_str() + bar + 1, nullptr, 10);
        std::cout << "       measured min for FullTick: " << minTime << " ms\n";
        if (minTime >= 4)
            sawMeasuredMilliseconds = true;
    }
    Check(sawMeasuredMilliseconds,
          "the FullTick row carries the probe's real duration (>= 4 ms)");
}

// ---------------------------------------------------------------------------
// 3b. The report's max column really is a maximum.
//
// PrintStats aggregated its max out of each bucket's MINIMUM
// (`pd.maxTime = performanceData.minTime`), so min and max came out equal in
// every report ever printed. That matters beyond tidiness: the platform's roster
// rollback guard is written against a maximum -- "if tick p99 > 1000 ms or
// max > 3000 ms persists, the roster goes back to 136" -- and a max column that
// is actually the min cannot trip it.
// ---------------------------------------------------------------------------
void TestReportedMaxIsAMaximum()
{
    std::cout << "the report's max column is a maximum, not the min\n";

    sPlayerbotAIConfig.perfMonEnabled = true;
    sPerformanceMonitor.Init(0, 0);
    sPerformanceMonitor.Reset();
    ClearLog();

    // Two probes into the same bucket with clearly different durations.
    { auto quick = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
      std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    { auto slow = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
      std::this_thread::sleep_for(std::chrono::milliseconds(40)); }

    Check(sPerformanceMonitor.PrintStats(true), "PrintStats(tick) printed");

    bool sawSpread = false;
    for (auto const& line : sLog.Lines())
    {
        if (line.find("PlayerbotAIBase::FullTick") == std::string::npos)
            continue;
        std::size_t const bar = line.find('|');
        if (bar == std::string::npos)
            continue;
        char const* cursor = line.c_str() + bar + 1;
        char* after = nullptr;
        long const minTime = std::strtol(cursor, &after, 10);
        // The column separator is " .. ".
        char const* dots = std::strstr(after, "..");
        long const maxTime = dots ? std::strtol(dots + 2, nullptr, 10) : 0;
        std::cout << "       reported min/max for FullTick: " << minTime
                  << " / " << maxTime << " ms\n";
        if (maxTime > minTime && maxTime >= 35)
            sawSpread = true;
    }
    Check(sawSpread, "max is the 40 ms probe and min is the 5 ms one");
}

// ---------------------------------------------------------------------------
// 4. The per-map probes and the PlayerbotAI overload, once their map is known.
// ---------------------------------------------------------------------------
void TestPerMapCollection()
{
    std::cout << "per-map probes collect once their map is registered\n";

    sPlayerbotAIConfig.perfMonEnabled = true;

    // Map 1 (Kalimdor), no instance -- what the loop over sMapMgr.Maps()
    // registers for a loaded continent.
    sPerformanceMonitor.Init(1, 0);
    sPerformanceMonitor.Reset();
    ClearLog();

    Player bot(1, 0);
    AiObjectContext context;
    PlayerbotAI ai(&bot, &context);

    {
        auto fullTick = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
        auto action = sPerformanceMonitor.start(PERF_MON_ACTION, "CastSpell", &ai);
        Check(action != nullptr, "the PlayerbotAI overload is live for a registered map");
        SpendMeasurableTime();
    }

    Check(context.performanceStack.empty(),
          "the probe popped itself off the AI's performance stack");

    Check(sPerformanceMonitor.PrintStats(false, false, true),
          "PrintStats(showMap) reports that it printed");
    Check(LogContains("CastSpell"), "the per-map action row is in the report");
    Check(LogContains(" 1"), "the report is annotated with the map id");
}

// ---------------------------------------------------------------------------
// 5. `.perfmon` tells the operator what happened, in every state.
//
// The old handler returned true and said nothing at all, in all three of
// "disabled", "collecting nothing" and "here is your report" -- which is how the
// dead instrument stayed invisible. PrintStats writes through sLog, so the
// report itself never reaches the requester's chat window either; the handler
// has to say where it went.
// ---------------------------------------------------------------------------
void TestCommandAlwaysAnswers()
{
    std::cout << ".perfmon answers the operator in every state\n";

    ChatHandler handler;
    char disabledArgs[] = "tick";

    sPlayerbotAIConfig.perfMonEnabled = false;
    handler.ClearMessages();
    ClearLog();
    Check(handler.HandlePerfMonCommand(disabledArgs), ".perfmon tick is handled while disabled");
    Check(handler.Messages().size() == 1 &&
              handler.Messages()[0].find("disabled") != std::string::npos,
          "it says the monitor is disabled instead of staying silent");

    char toggleArgs[] = "toggle";
    handler.ClearMessages();
    Check(handler.HandlePerfMonCommand(toggleArgs), ".perfmon toggle is handled");
    Check(sPlayerbotAIConfig.perfMonEnabled, "toggle enabled the monitor");
    Check(handler.Messages().size() == 1 &&
              handler.Messages()[0].find("enabled") != std::string::npos,
          "it says the monitor is enabled");
    Check(handler.Messages()[0].find("next random-bot update tick") != std::string::npos,
          "and says collection has not started yet, which is the part that was missing");

    // Buckets already exist by now (tests 3 and 4 added them), so a report is
    // available and the handler should point at the log.
    sPerformanceMonitor.Reset();
    {
        auto fullTick = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAIBase::FullTick");
        SpendMeasurableTime();
    }

    handler.ClearMessages();
    ClearLog();
    Check(handler.HandlePerfMonCommand(disabledArgs), ".perfmon tick is handled while collecting");
    Check(handler.Messages().size() == 1 &&
              handler.Messages()[0].find("server log") != std::string::npos,
          "it says where the report went, because PrintStats does not reach chat");

    // A null argv is what a bare `.perfmon` can hand the command table. The old
    // handler ran strcmp and then std::string on it.
    handler.ClearMessages();
    Check(handler.HandlePerfMonCommand(nullptr), ".perfmon with a null argument does not crash");
    Check(handler.Messages().size() == 1, "and still answers");

    char resetArgs[] = "reset";
    handler.ClearMessages();
    Check(handler.HandlePerfMonCommand(resetArgs), ".perfmon reset is handled");
    Check(handler.Messages().size() == 1 &&
              handler.Messages()[0].find("reset") != std::string::npos,
          "and confirms the reset");
}
} // namespace

int main()
{
    // Order matters: the first two tests must run before any bucket exists,
    // because mapsData is only ever added to.
    TestDisabledCollectsNothing();
    TestEnabledWithoutInitCollectsNothing();
    TestInitThenProbeCollects();
    TestReportedMaxIsAMaximum();
    TestPerMapCollection();
    TestCommandAlwaysAnswers();

    if (g_failures)
    {
        std::cout << "\n" << g_failures << " assertion(s) failed\n";
        return 1;
    }

    std::cout << "\nall assertions passed\n";
    return 0;
}
