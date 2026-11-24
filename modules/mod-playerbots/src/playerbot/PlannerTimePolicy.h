#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3 planner, S5 plan time (twow-repo#726; S5-FIX-ENTWURF-v0.1 sections 2 and 6; AiPlayerbot.Planner.TimeTrace, default 0):
// the time of one plan by stage, as one [PlanTime] line per plan. Shadow run 1 measured the plan only in whole milliseconds
// (getMSTime, a logged ms means a true time in (ms-1, ms+1)) and only as a sum; this line gives microseconds per stage and the
// thread CPU time, so the next fix is chosen by data. It only reads clocks and counts: no plan, pick or line of the planner
// changes. The header names no game type, so t/planner_time_policy_tests.cpp compiles it standalone.
namespace ai::planner
{
// 0 off (default), 1 on: every plan of every bot writes one [PlanTime] line (no line budget, no cohort). Any other value is off
// with one error line at load.
inline bool ValidTimeTrace(std::uint32_t value)
{
    return value <= 1;
}

// One fetch stage of the plan (turn-ins, objectives, pickups): the destination fetch, the sort by distance and the candidate
// loop, in microseconds; walk = destinations the fetch looked at, ret = destinations it returned (TravelMgr DestFetchStats).
// A stage that did not run stays all 0 and ran = false.
struct StageTime
{
    bool ran = false;
    std::uint32_t fetchUs = 0;
    std::uint32_t sortUs = 0;
    std::uint32_t loopUs = 0;
    std::uint32_t walk = 0;
    std::uint32_t ret = 0;
};

struct PlanTime
{
    std::uint32_t guid = 0;
    char cohort = 'A';
    StageTime take;
    StageTime obj;
    StageTime give;
    std::uint32_t headUs = 0;    // the plan start (startMs) to the first fetch: the band, the snapshot facts, the slot setup
    std::uint32_t restUs = 0;    // after the last fetch stage to the ms read: grind slots, scoring, ranking, hysteresis
    std::uint32_t totalUs = 0;   // the plan start to the ms read, the same span as the [Plan] ms (steady clock, microseconds)
    std::uint32_t ms = 0;        // the [Plan] ms of the same plan (getMSTime difference)
    std::int64_t cpuUs = -1;     // the CPU time of the thread over the same span; -1 = not available (cpu_us=na)
    std::uint32_t cands = 0;
    std::uint32_t cut = 0;    // slots the cap or the budget refused (the [Plan] cut): a loop that broke early
    std::uint32_t done = 0;   // complete quests asked of the turn-in fetch (1 = one key lookup, 2+ = the key walk)
    std::uint32_t open = 0;   // open quests asked of the objective fetch (same)
    bool overBudget = false;
    // S5 F1 (twow-repo#726, AiPlayerbot.Planner.GiverPrefilter): set when the giver fetch of this plan ran with the prefilter; the
    // line then ends with prefilter=1 give_pre= give_xmap= give_inbox= verify= (GiverPrefilterPolicy.h Counts; verify = this plan
    // also ran the original list for the [PlanPrefilter] check, after its clocks were read). Off: the line is as before.
    bool givePrefilter = false;
    std::uint32_t givePre = 0;
    std::uint32_t giveXmap = 0;
    std::uint32_t giveInbox = 0;
    bool giveVerify = false;
    // S5 F1-IX (twow-repo#726, IX-ENTWURF-v0.2; AiPlayerbot.Planner.GiverIndex): giveIx = the giver fetch of this plan ran through the
    // giver index; the line then ends (after the F1 part) with ix=1 give_ixall= give_ixmaps= give_ixportals= give_ixbuckets=
    // give_ixwide= give_ixt=portal/collect/sort/append (GiverIndexPolicy.h Counts, microseconds). giveIxMiss / giveIxStale = the
    // fetch asked the index but walked (give_ixmiss=1 / give_ixstale=1). All false: the line is as before.
    bool giveIx = false;
    bool giveIxMiss = false;
    bool giveIxStale = false;
    std::uint32_t giveIxAll = 0;
    std::uint32_t giveIxMaps = 0;
    std::uint32_t giveIxPortals = 0;
    std::uint32_t giveIxBuckets = 0;
    std::uint32_t giveIxWide = 0;
    std::uint32_t giveIxPortalUs = 0;
    std::uint32_t giveIxCollectUs = 0;
    std::uint32_t giveIxSortUs = 0;
    std::uint32_t giveIxAppendUs = 0;
};

// A microsecond difference of two steady clock readings in nanoseconds, clamped to 0..UINT32_MAX microseconds.
inline std::uint32_t UsBetween(std::int64_t fromNs, std::int64_t toNs)
{
    if (toNs <= fromNs)
        return 0;
    std::int64_t const us = (toNs - fromNs) / 1000;
    return us > std::int64_t(0xFFFFFFFFu) ? 0xFFFFFFFFu : std::uint32_t(us);
}

// The [PlanTime] buffer: every field at its maximum fits (the unit test checks it).
constexpr std::size_t kPlanTimeBuf = 832;

// [PlanTime] bot= cohort= us= cpu_us= ms= over_budget= cands= cut= done= open= head= take= obj= give= rest=; a stage is
// fetch/sort/loop/walk/ret in microseconds and counts, or "-" when it did not run.
inline std::size_t FormatPlanTimeLine(char* buf, std::size_t n, PlanTime const& t)
{
    if (!buf || n == 0)
        return 0;
    char cpu[24];
    if (t.cpuUs < 0)
        std::snprintf(cpu, sizeof(cpu), "na");
    else
        std::snprintf(cpu, sizeof(cpu), "%lld", static_cast<long long>(t.cpuUs));
    char stage[3][64];
    StageTime const* const stages[3] = { &t.take, &t.obj, &t.give };
    for (std::size_t i = 0; i < 3; ++i)
    {
        StageTime const& s = *stages[i];
        if (s.ran)
            std::snprintf(stage[i], sizeof(stage[i]), "%u/%u/%u/%u/%u", s.fetchUs, s.sortUs, s.loopUs, s.walk, s.ret);
        else
            std::snprintf(stage[i], sizeof(stage[i]), "-");
    }
    int const w = std::snprintf(buf, n, "[PlanTime] bot=%u cohort=%c us=%u cpu_us=%s ms=%u over_budget=%u cands=%u cut=%u done=%u "
        "open=%u head=%u take=%s obj=%s give=%s rest=%u",
        t.guid, t.cohort, t.totalUs, cpu, t.ms, t.overBudget ? 1u : 0u, t.cands, t.cut, t.done, t.open, t.headUs, stage[0], stage[1],
        stage[2], t.restUs);
    if (w < 0)
        return 0;
    std::size_t len = std::size_t(w) < n ? std::size_t(w) : n - 1;
    if (t.givePrefilter && len + 1 < n)
    {
        int const x = std::snprintf(buf + len, n - len, " prefilter=1 give_pre=%u give_xmap=%u give_inbox=%u verify=%u", t.givePre,
            t.giveXmap, t.giveInbox, t.giveVerify ? 1u : 0u);
        if (x > 0)
            len = len + std::size_t(x) < n ? len + std::size_t(x) : n - 1;
    }
    if (t.giveIx && len + 1 < n)
    {
        int const x = std::snprintf(buf + len, n - len, " ix=1 give_ixall=%u give_ixmaps=%u give_ixportals=%u give_ixbuckets=%u "
            "give_ixwide=%u give_ixt=%u/%u/%u/%u", t.giveIxAll, t.giveIxMaps, t.giveIxPortals, t.giveIxBuckets, t.giveIxWide,
            t.giveIxPortalUs, t.giveIxCollectUs, t.giveIxSortUs, t.giveIxAppendUs);
        if (x > 0)
            len = len + std::size_t(x) < n ? len + std::size_t(x) : n - 1;
    }
    else if ((t.giveIxMiss || t.giveIxStale) && len + 1 < n)
    {
        int const x = std::snprintf(buf + len, n - len, "%s", t.giveIxStale ? " give_ixstale=1" : " give_ixmiss=1");
        if (x > 0)
            len = len + std::size_t(x) < n ? len + std::size_t(x) : n - 1;
    }
    return len;
}
}
