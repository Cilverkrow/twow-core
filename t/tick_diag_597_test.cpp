// twow-repo#597 (TICK-01): pure parts of the core tick diagnostics - [WorldTickSlow] line and rate limit,
// [TerrainReload] / [LogWait] counters, [StallWatch] event tracker and line, [ResultsSlow] callback token,
// [MapTickRegion] window. No server needed (src/shared/TickDiagPolicy.h, src/game/WorldTickSlow.h).
#include "../src/shared/TickDiagPolicy.h"
#include "../src/game/WorldTickSlow.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

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

void RateLimiterTest()
{
    tick_diag::RateLimiter limiter(60);
    for (int i = 0; i < 60; ++i)
        Require(limiter.Allow(1000 + i), "60 lines allowed in a minute");
    Require(!limiter.Allow(2000) && !limiter.Allow(59999), "line 61 and 62 dropped");
    Require(limiter.TakeDropped() == 2 && limiter.TakeDropped() == 0, "dropped counted once, then reset");
    Require(!limiter.Allow(60999), "still the same window (59.999 s)");
    Require(limiter.Allow(61000), "a new window after 60 s");
    Require(limiter.TakeDropped() == 1, "the drop before the new window is reported on the next line");
    tick_diag::RateLimiter twenty(20);
    int allowed = 0;
    for (int i = 0; i < 100; ++i)
        allowed += twenty.Allow(5000) ? 1 : 0;
    Require(allowed == 20 && twenty.TakeDropped() == 80, "[ResultsSlow] limit of 20 a minute");
}

void PhaseWindowStepsTest()
{
    using namespace world_tick;
    PhaseWindow w;
    w.Add(Sessions, 1000);
    w.CloseTick(5000, 200);
    Require(w.tick[Rest] == 4000 && w.tick[Sleep] == 200, "CloseTick fills rest and sleep");
    Require(w.Ticks() == 0, "CloseTick does not push into the minute window");
    w.DropTick();
    Require(w.Ticks() == 0 && w.tick[Rest] == 0 && w.tick[Sessions] == 0, "DropTick clears the tick, no sample");
    w.Add(Results, 300);
    w.CloseTick(1000, 0);
    w.PushTick();
    Require(w.Ticks() == 1 && w.samples[Rest][0] == 700 && w.tick[Results] == 0, "PushTick = the second half of EndTick");
}

void WorldTickSlowLineTest()
{
    using namespace world_tick_slow;
    Require(!IsSlow(999999, 0), "threshold 0 = off");
    Require(!IsSlow(49999, 50) && IsSlow(50000, 50), "at or above the threshold");

    world_tick::PhaseWindow w;
    w.Add(world_tick::Sessions, 1000);
    w.Add(world_tick::MapsPre, 2500);
    w.Add(world_tick::WaitContinents, 30000);
    w.Add(world_tick::Managers, 4000);
    w.Add(world_tick::Results, 5000);
    w.CloseTick(62500, 10000);
    Require(w.tick[world_tick::Rest] == 20000, "rest of the tick");

    PartTimes parts;
    parts.Add(MgrTransport, 1000);
    parts.Add(MgrBg, 3000);
    parts.Add(RPre, 500);
    parts.Add(RTerrain, 15000);
    parts.Add(RHook, 2000);
    parts.Add(Sessions, 999);       // main phases are not printed from the parts
    parts.Add(ROther, 777);         // r_other is the remainder of rest, not the ROther part
    parts.Add(PartCount, 5);        // out of range: ignored

    std::string const line = FormatLine(7, 62500, w.tick, parts, 3);
    std::string const expected = "[WorldTickSlow] tick=7 ms=62.5 sessions=1.0 teleports=0.0 maps_pre=2.5 instances=0.0 "
        "wait_continents=30.0 switch_instances=0.0 maps_post=0.0 managers=4.0 mgr_transport=1.0 mgr_bg=3.0 mgr_lfg=0.0 "
        "mgr_lft=0.0 mgr_guard=0.0 mgr_zone=0.0 mgr_dynvis=0.0 mgr_eluna=0.0 mgr_offline=0.0 async_tasks=0.0 results=5.0 "
        "rest=20.0 r_pre=0.5 r_ah=0.0 r_dispatch=0.0 r_savevars=0.0 r_corpses=0.0 r_events=0.0 r_remove=0.0 r_mps=0.0 "
        "r_account=0.0 r_cli=0.0 r_terrain=15.0 r_guild=0.0 r_hook=2.0 r_other=2.5 dropped=3";
    if (line != expected)
        std::cerr << "got:      " << line << "\nexpected: " << expected << '\n';
    Require(line == expected, "[WorldTickSlow] line format and field order");

    // More rest parts than rest (clock granularity): r_other never negative.
    PartTimes big;
    big.Add(RHook, 50000);
    std::string const clamped = FormatLine(1, 62500, w.tick, big, 0);
    Require(clamped.find(" r_hook=50.0 r_other=0.0 dropped=0") != std::string::npos, "r_other clamped at 0");

    parts.Clear();
    Require(parts.us[RTerrain] == 0 && parts.us[MgrBg] == 0, "parts cleared");

    // Every part has its own name (the [StallWatch] phase=), out of range is "?".
    std::set<std::string> names;
    for (uint32_t p = 0; p < PartCount; ++p)
    {
        Require(std::string(PartName(p)) != "?", "every part named");
        names.insert(PartName(p));
    }
    Require(names.size() == PartCount, "part names unique");
    Require(std::string(PartName(PartCount)) == "?", "unknown part");
    Require(std::string(PartName(RTerrain)) == "r_terrain" && std::string(PartName(Idle)) == "idle", "part names");
    // twow-repo#597 review: WorldTickEnd after the sleep has its own [StallWatch] phase; never on the slow line
    // (the exact line above has no tick_end).
    Require(std::string(PartName(TickEnd)) == "tick_end" && TickEnd + 1 == PartCount, "tick_end part");
}

void LogWaitTest()
{
    tick_diag::LogWaitCounters c;
    c.Record(5000, 6000);
    c.Record(100, 200000);
    c.Record(10, 20);
    tick_diag::LogWaitSnapshot const s = c.Take();
    Require(s.lines == 3, "lines counted");
    Require(s.stdoutMaxUs == 5000 && s.fileMaxUs == 200000, "maxima per stream");
    Require(s.over10ms == 2 && s.over100ms == 1, "console + file >= 10 / 100 ms");
    tick_diag::LogWaitSnapshot const empty = c.Take();
    Require(empty.lines == 0 && empty.fileMaxUs == 0 && empty.over10ms == 0, "Take resets");
}

void TerrainLoadTest()
{
    tick_diag::TerrainLoadCounters c;
    c.Record(4000, true, true);
    c.Record(9000, true, false);
    c.Record(1000, false, false);
    tick_diag::TerrainLoadSnapshot const s = c.Take();
    Require(s.loads == 3 && s.lazy == 2, "loads and lazy loads");
    Require(s.usSum == 14000 && s.maxUs == 9000, "sum and max");
    Require(s.worldThread == 1 && s.otherThread == 2, "thread split");
    Require(c.Take().loads == 0, "Take resets");
}

void StallTrackerTest()
{
    tick_diag::StallLine line;
    tick_diag::StallTracker off(0);
    Require(!off.Step(1000, 0, 5000, 5000, 1, line), "threshold 0 never writes");

    tick_diag::StallTracker t(100);
    Require(!t.Step(1000000, 5000, 0, 50, 3, line), "below the threshold");
    Require(t.Step(1000010, 5010, 0, 120, 9, line), "a line when the stall opens");
    Require(line.startEpochMs == 1000010 - 120 && line.ageMs == 120 && line.oversleepMs == 0 && line.part == 9 && line.dropped == 0,
        "open line: start = now - age, peak, part");
    Require(!t.Step(1000020, 5020, 0, 130, 9, line), "growing, but less than a second since the last line");
    Require(t.Step(1001010, 6010, 0, 1130, 12, line), "growing for a second: progress line");
    Require(line.startEpochMs == 1000010 - 120 && line.ageMs == 1130 && line.part == 12, "same start, new peak and part");
    Require(!t.Step(1001020, 6020, 0, 0, 0, line), "closed without a new peak: no line");
    Require(!t.Open(), "closed");

    Require(t.Step(2000000, 7000, 150, 0, 0, line), "oversleep opens a stall");
    Require(line.oversleepMs == 150 && line.startEpochMs == 2000000 - 150, "oversleep start");
    Require(!t.Step(2000010, 7010, 300, 0, 0, line), "growing");
    Require(t.Step(2000020, 7020, 0, 0, 0, line), "closed with a peak not yet written: final line");
    Require(line.oversleepMs == 300, "final line has the peak");

    // Rate limit: the tracker writes at most linesPerMinute lines, dropped= on the next one.
    tick_diag::StallTracker limited(100, 2);
    Require(limited.Step(10, 0, 0, 200, 1, line), "limit: line 1");
    Require(!limited.Step(20, 10, 0, 300, 1, line), "limit: growing");
    Require(limited.Step(30, 20, 0, 0, 0, line), "limit: line 2 (close)");
    Require(!limited.Step(40, 30, 0, 200, 1, line), "limit: line 3 dropped");
    Require(!limited.Step(50, 40, 0, 0, 0, line), "limit: close without growth");
    Require(limited.Step(60040, 60030, 0, 200, 1, line) && line.dropped == 1, "limit: next minute, dropped=1");

    char buf[256];
    tick_diag::StallLine l;
    l.startEpochMs = 1760000000123ull;
    l.oversleepMs = 7;
    l.ageMs = 2500;
    l.part = world_tick_slow::RHook;
    l.dropped = 4;
    std::size_t const len = tick_diag::FormatStallLine(buf, sizeof(buf), l, world_tick_slow::PartName(l.part));
    std::string const s(buf, len);
    Require(s == "[StallWatch] start=1760000000123 oversleep_ms=7 world_tick_age_ms=2500 phase=r_hook dropped=4\n",
        "[StallWatch] line format");
    char small[16];
    std::size_t const cut = tick_diag::FormatStallLine(small, sizeof(small), l, "x");
    Require(cut == sizeof(small) - 1 && std::strlen(small) == cut, "a short buffer is cut, never overrun");
}

void CallbackTokenTest()
{
    char buf[16];
    tick_diag::CallbackToken(buf, sizeof(buf), "class MaNGOS::Cb");
    Require(std::string(buf) == "class_MaNGOS::C", "blanks made '_', cut at the buffer");
    tick_diag::CallbackToken(buf, sizeof(buf), nullptr);
    Require(std::string(buf) == "-", "no name");
    tick_diag::CallbackToken(buf, sizeof(buf), "N6MaNGOS3CbE");
    Require(std::string(buf) == "N6MaNGOS3CbE", "mangled name kept");
}

void RegionWindowTest()
{
    using tick_diag::RegionTickSample;
    using tick_diag::RegionWindow;
    std::vector<uint32_t> v = { 5, 1, 9, 3, 7 };
    Require(tick_diag::PercentileUs(v, 95) == 9 && tick_diag::PercentileUs(v, 50) == 5, "nearest-rank percentile");
    std::vector<uint32_t> none;
    Require(tick_diag::PercentileUs(none, 95) == 0, "empty percentile");
    Require(tick_diag::ClampUs(0x1FFFFFFFFull) == 0xFFFFFFFFu, "clamped to 32 bits");

    RegionWindow w;
    uint64_t const a = RegionWindow::Key(0, 1), b = RegionWindow::Key(0, 2), c = RegionWindow::Key(1, 1);
    RegionTickSample sa, sb, sc;
    sa.valid = true; sa.preUs = 10000; sa.postUs = 5000; sa.loops = 1; sa.arriveUs = 1000; sa.exitUs = 1600;
    sb.valid = true; sb.preUs = 20000; sb.postUs = 100; sb.loops = 0; sb.arriveUs = 1500; sb.exitUs = 1550;
    sc.valid = false; sc.preUs = 99999;
    w.AddTick({ { a, sa }, { b, sb }, { c, sc } });
    Require(w.regions.size() == 2 && !w.regions.count(c), "invalid samples ignored");
    Require(w.regions[b].heldBarrier == 1 && w.regions[a].heldBarrier == 0, "the region that arrived last held the barrier");
    Require(w.regions[a].overrunMaxUs == 100 && w.regions[b].overrunMaxUs == 50, "stay after the last arrival");
    Require(w.regions[a].extraLoops == 1, "extra loops summed");
    tick_diag::RegionStat const pre = w.regions[a].Pre();
    tick_diag::RegionStat const post = w.regions[a].Post();
    Require(pre.avgUs == 10000 && pre.p95Us == 10000 && pre.maxUs == 10000, "pre avg/p95/max");
    Require(post.avgUs == 5000 && post.maxUs == 5000, "post avg/max");

    RegionTickSample sa2 = sa;
    sa2.preUs = 30000; sa2.arriveUs = 9000; sa2.exitUs = 9000; sa2.loops = 2;
    w.AddTick({ { a, sa2 }, { b, sb } });
    Require(w.regions[a].heldBarrier == 1 && w.regions[a].extraLoops == 3, "second tick: a held");
    Require(w.regions[a].Pre().avgUs == 20000 && w.regions[a].Pre().maxUs == 30000, "average over ticks, exact max");
    w.AddTick({ { c, sc } });
    Require(w.regions.size() == 2, "a tick without valid samples adds nothing");

    w.Clear();
    Require(w.Empty(), "cleared");

    // Bounded: regions beyond MaxRegions are counted, not kept; samples beyond SamplesKept are not stored.
    for (uint32_t i = 0; i < RegionWindow::MaxRegions + 3; ++i)
    {
        RegionTickSample s = sa;
        s.arriveUs = 1000 + i;
        w.AddTick({ { RegionWindow::Key(0, i + 1), s } });
    }
    Require(w.regions.size() == RegionWindow::MaxRegions && w.overflow == 3, "region cap");
    w.Clear();
    for (std::size_t i = 0; i < RegionWindow::SamplesKept + 100; ++i)
    {
        RegionTickSample s = sa;
        s.preUs = uint32_t(i);
        w.AddTick({ { a, s } });
    }
    Require(w.regions[a].pre.size() == RegionWindow::SamplesKept && w.regions[a].ticks == RegionWindow::SamplesKept + 100,
        "sample cap, ticks exact");
    Require(w.regions[a].Pre().maxUs == RegionWindow::SamplesKept + 99, "max exact beyond the cap");
}
}

int main()
{
    RateLimiterTest();
    PhaseWindowStepsTest();
    WorldTickSlowLineTest();
    LogWaitTest();
    TerrainLoadTest();
    StallTrackerTest();
    CallbackTokenTest();
    RegionWindowTest();
    std::cout << "tick_diag_597_test passed\n";
    return 0;
}
