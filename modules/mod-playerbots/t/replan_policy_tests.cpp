// A3 R9 / R2 producer (R9-REPLAN-INTERFACE v5 9575095d; v4 d44daade normative in A3-BOT-LOGIK 4.3): the pure progress meter
// (ReplanPolicy.h). The header names no game type, so this suite compiles it standalone. Cases 1-7 are v4 7.5, cases 8-11 and
// 14 are v5 8.3; cases 12-13 (rescue trigger and rescue episode) come with the S4-3 pair. Since R9-2 (twow-repo#575) the planner
// tick calls the meter (PlannerShadow.cpp, AiPlayerbot.Replan / Replan.Shadow, default 0; t/replan_wire_policy_tests.cpp);
// t/contracts.d/r9_replan_policy_source_contract_tests.cmake pins the header pure and its callers.

#include "ReplanPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace ai::replan;

int g_checks = 0;

void Require(bool condition, char const* message)
{
    ++g_checks;
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Scripted sink: answers every call with `answer`, records what it saw.
struct Sink : IReplanSink
{
    Verdict answer;
    std::vector<StallEvent> stalls;
    std::vector<RelocationEvent> relocs;

    Sink(Decision d, Reason why)
    {
        answer.d = d;
        answer.why = why;
    }
    Verdict OnStall(StallEvent const& e) override
    {
        stalls.push_back(e);
        return answer;
    }
    Verdict OnRelocate(RelocationEvent const& e) override
    {
        relocs.push_back(e);
        return answer;
    }
};

std::uint64_t const K = TargetKey(218, 808, 100.0f, 200.0f, 30.0f);
std::uint64_t const K2 = TargetKey(218, 808, 400.0f, 200.0f, 30.0f);

Sample S(std::uint32_t now, std::uint64_t key = K, float remaining = 500.0f)
{
    Sample s;
    s.nowMs = now;
    s.targetKey = key;
    s.quest = 218;
    s.targetEntry = 808;
    s.tx = 100.0f;
    s.ty = 200.0f;
    s.tz = 30.0f;
    s.kind = TargetKind::Objective;
    s.remaining = remaining;
    s.state = ClockState::Alive;
    return s;
}

RelocationEvent R(std::uint32_t at, RelocSource src, std::uint64_t key = K, float remaining = 500.0f, bool planned = false,
                  std::uint8_t leg = NoLeg)
{
    RelocationEvent e;
    e.source = src;
    e.atMs = at;
    e.targetKey = key;
    e.remaining = remaining;
    e.planned = planned;
    e.legIndex = leg;
    return e;
}

std::string Line(Record const& r, bool shadow = false)
{
    LineCtx c;
    c.guid = 166;
    c.cohort = 'B';
    c.arbiter = 2;
    c.shadow = shadow;
    c.level = 12;
    char buf[1024];
    FormatReplanLine(buf, sizeof(buf), c, r);
    return buf;
}

bool Has(std::string const& line, char const* part)
{
    return line.find(part) != std::string::npos;
}

// Feeds 1 s samples from `from` (exclusive) to `to` (inclusive); stops at the first decision and returns it (or none).
Outcome FeedRange(ProgressMeter& m, Sink& sink, std::uint32_t from, std::uint32_t to, std::uint64_t key = K, float rem = 500.0f,
                  ClockState st = ClockState::Alive, std::uint32_t* at = nullptr)
{
    for (std::uint32_t t = from + 1000; Elapsed(from, t) <= Elapsed(from, to); t += 1000)
    {
        Sample s = S(t, key, rem);
        s.state = st;
        Outcome o = m.Feed(s, sink);
        if (o.decided)
        {
            if (at)
                *at = t;
            return o;
        }
    }
    return Outcome();
}

// Uses up the planner-choice budget of the hour: n relocations, each at a fresh key, answered Replan.
void FillHour(ProgressMeter& m, std::uint32_t at, std::uint32_t n)
{
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    for (std::uint32_t i = 0; i < n; ++i)
        m.OnRelocate(R(at, RelocSource::Unknown, TargetKey(9000 + i, 1, 0, 0, 0)), replan);
}

// ---- v4 7.5 -----------------------------------------------------------------------------------------------------------
void Case1ArrivalLoop()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    Outcome o1 = m.OnRelocate(R(30000, RelocSource::FarMoveArrival, K, 480.0f), keep);
    Outcome o2 = m.OnRelocate(R(60000, RelocSource::FarMoveArrival, K, 480.0f), keep);
    Outcome o3 = m.OnRelocate(R(90000, RelocSource::FarMoveArrival, K, 480.0f), keep);
    Require(o1.decided && o1.rec.verdict.d == Decision::Keep && !o1.rec.forced, "case 1: 1st arrival is a normal Keep");
    Require(o2.decided && o2.rec.verdict.d == Decision::Keep && !o2.rec.forced, "case 1: 2nd arrival is a normal Keep");
    Require(keep.relocs[2].forced == Reason::RelocLoop, "case 1: the sink sees the 3rd arrival as forced reloc_loop");
    Require(o3.rec.verdict.d == Decision::Replan && o3.rec.verdict.why == Reason::RelocLoop && o3.rec.forced,
            "case 1: the 3rd arrival gives replan reason=reloc_loop (a Keep answer is turned into Replan)");
    std::string const l = Line(o3.rec);
    Require(Has(l, "trigger=relocate:far_move_arrival") && Has(l, "decision=replan") && Has(l, "reason=reloc_loop") &&
                Has(l, "forced=1") && Has(l, "relocs_np=3") && Has(l, "planned=0"),
            "case 1: the [Replan] line of the 3rd arrival");
    Require(m.BestRemaining() == 480.0f, "case 1: arrivals re-base bestRemaining without credit");
}

void Case2LoopWithCap()
{
    ProgressMeter m;
    FillHour(m, 0, 6);
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    m.Feed(S(1000), replan);
    m.TakeCounters();
    Outcome o1 = m.OnRelocate(R(30000, RelocSource::FarMoveArrival), replan);
    Outcome o2 = m.OnRelocate(R(60000, RelocSource::FarMoveArrival), replan);
    Outcome o3 = m.OnRelocate(R(90000, RelocSource::FarMoveArrival), replan);
    Require(o1.rec.verdict.why == Reason::RateLimited && o2.rec.verdict.why == Reason::RateLimited,
            "case 2: planner-choice Replans above the hourly cap become rate_limited Keeps");
    Require(o3.rec.verdict.d == Decision::Replan && o3.rec.verdict.why == Reason::RelocLoop,
            "case 2: the forced reloc_loop is never rate_limited");
    Counters const c = m.TakeCounters();
    Require(c.rateLimited == 2 && c.forced == 1, "case 2: rate_limited 2, forced 1");
    Require(c.forcedAfterRateLimited == 1, "case 2: the forced reloc_loop right after a rate_limited Keep (OB-10 nit 2)");
}

void Case3ClockContinuity()
{
    // Reading of 7.5 case 3 (the spec's "progress after each 2nd arrival" would reset the clock as well): continuity is shown
    // with arrivals that do not end the episode - Rescue arrivals (not loop-counted) and unplanned far-move arrivals whose
    // forced reloc_loop verdicts leave the key unchanged (shadow-like). The stall fires when the counted time reaches N.
    for (RelocSource src : {RelocSource::Rescue, RelocSource::FarMoveArrival})
    {
        ProgressMeter m;
        Sink keep(Decision::Keep, Reason::LongRouteKnown);
        m.Feed(S(0), keep);
        std::uint32_t stallAt = 0;
        for (std::uint32_t t = 1000; t <= 700000 && stallAt == 0; t += 1000)
        {
            if (t % 30000 == 0)
                m.OnRelocate(R(t, src), keep);
            if (m.Feed(S(t), keep).decided)
                stallAt = t;
        }
        Require(stallAt == 600000, "case 3: OnStall fires once the counted stall time reaches N, not later");
        Require(keep.stalls.size() == 1 && keep.stalls[0].stallS == 600, "case 3: one stall event, stall_s=600");
    }
}

void Case4ProgressResets()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    m.OnRelocate(R(30000, RelocSource::FarMoveArrival, K, 495.0f), keep);
    Require(m.RelocsNoProgress() == 1 && m.KeepsInRow() == 1, "case 4: one arrival, one Keep");
    m.Feed(S(31000, K, 485.0f), keep);   // 495 - 10 = 485: a shrink of G
    Require(m.RelocsNoProgress() == 0 && m.KeepsInRow() == 0 && m.StallMs() == 0, "case 4: progress resets the counters");
}

void Case5KeyChange()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    FeedRange(m, keep, 0, 100000);
    m.OnRelocate(R(100000, RelocSource::FarMoveArrival), keep);
    std::uint32_t const ep = m.Episode();
    Require(m.StallMs() == 100000, "case 5: counted time before the key change");
    Outcome o = m.OnRelocate(R(101000, RelocSource::FarMoveArrival, K2, 300.0f), keep);
    Require(m.Episode() == ep + 1 && m.Key() == K2 && m.StallMs() == 0 && m.RelocsNoProgress() == 0 && keep.relocs.back().keepsInRow == 0 &&
                keep.relocs.back().relocsNoProgress == 0,
            "case 5: an arrival at a new key ends the episode, new clock");
    Require(!o.rec.forced && o.rec.targetKey == K2, "case 5: the arrival is still delivered and logged");
    m.Feed(S(102000, K2, 300.0f), keep);   // baseline sample, no time
    m.Feed(S(103000, K2, 300.0f), keep);
    Require(m.StallMs() == 1000, "case 5: the new clock counts from the first sample at the new key");
}

void Case6DeathRevive()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::RespawnWait);
    m.Feed(S(0), keep);
    FeedRange(m, keep, 0, 10000);
    m.OnRelocate(R(10000, RelocSource::Death), keep);
    std::uint32_t const before = m.StallMs();
    FeedRange(m, keep, 10000, 200000, K, 500.0f, ClockState::Dead);
    Require(m.StallMs() == before + 1000, "case 6: Dead time is frozen (only the interval before the first Dead sample counts)");
    m.OnRelocate(R(200000, RelocSource::Revive), keep);
    Require(m.RelocsNoProgress() == 0 && keep.relocs.back().relocsNoProgress == 0,
            "case 6: Death and Revive are not counted in relocsNoProgress");
}

void Case7CappedReplans()
{
    ProgressMeter m;
    FillHour(m, 0, 6);
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    m.Feed(S(0), replan);
    m.TakeCounters();
    std::vector<Outcome> got;
    std::uint32_t t = 0;
    while (got.size() < 3)
    {
        t += 1000;
        Outcome o = m.Feed(S(t), replan);
        if (o.decided)
            got.push_back(o);
    }
    Require(got[0].rec.verdict.why == Reason::RateLimited && got[1].rec.verdict.why == Reason::RateLimited,
            "case 7: two capped planner Replans become rate_limited Keeps");
    Require(replan.stalls[2].forced == Reason::KeepLimit && got[2].rec.verdict.d == Decision::Replan &&
                got[2].rec.verdict.why == Reason::KeepLimit,
            "case 7: the 3rd is keep_limit (forced), never a 3rd rate_limited");
    Counters const c = m.TakeCounters();
    Require(c.rateLimited == 2 && c.forced == 1 && c.forcedAfterRateLimited == 1,
            "case 7: rate_limited exactly 2, the keep_limit right after them counts as forced_after_rate_limited");
}

// ---- v5 8.3 -----------------------------------------------------------------------------------------------------------
void Case8PlannedChain()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::WaitTransport);
    m.Feed(S(0, K, 2000.0f), keep);
    Outcome a = m.OnRelocate(R(10000, RelocSource::Hearth, K, 2100.0f, true, 0), keep);        // planned, no credit
    Outcome b = m.OnRelocate(R(20000, RelocSource::TeleportAck, K, 900.0f, true, 1), keep);    // planned, credit
    Outcome c = m.OnRelocate(R(30000, RelocSource::FarMoveArrival, K, 880.0f, true, 2), keep); // never planned
    Require(a.rec.planned && a.rec.plannedNoProgress == 1 && !a.rec.forced, "case 8: planned hearth without credit is neutral");
    Require(b.rec.planned && b.rec.plannedNoProgress == 0 && b.rec.relocsNoProgress == 0 && m.BestRemaining() <= 900.0f,
            "case 8: a planned transport arrival that brings the bot closer is credited");
    Require(!c.rec.planned && c.rec.relocsNoProgress == 1, "case 8: a far-move arrival is never planned");
    for (Outcome const* o : {&a, &b, &c})
        Require(o->rec.verdict.why != Reason::RelocLoop, "case 8: no reloc_loop in the chain");
    Require(m.TakeCounters().plannedCredit == 1, "case 8: planned_credit 1");
}

void Case9PlannedLoop()
{
    ProgressMeter m;
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    m.Feed(S(0, K, 500.0f), replan);
    std::vector<Outcome> got;
    for (std::uint32_t i = 1; i <= 4; ++i)
        got.push_back(m.OnRelocate(R(i * 60000, RelocSource::Hearth, K, 495.0f, true, 0), replan));
    for (std::size_t i = 0; i < 3; ++i)
        Require(!got[i].rec.forced, "case 9: planned hearths 1-3 are not forced");
    Require(got[3].rec.forced && got[3].rec.verdict.why == Reason::RelocLoop && got[3].rec.planned &&
                got[3].rec.plannedNoProgress == 4,
            "case 9: the 4th planned hearth without credit is a forced reloc_loop");
    Require(Has(Line(got[3].rec), "planned=1") && Has(Line(got[3].rec), "planned_np=4"), "case 9: line planned=1 planned_np=4");
}

void Case10CreditOnce()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::WaitTransport);
    m.Feed(S(0, K, 500.0f), keep);
    Outcome a1 = m.OnRelocate(R(10000, RelocSource::TeleportAck, K, 500.0f, true, 0), keep);
    Outcome b = m.OnRelocate(R(20000, RelocSource::TeleportAck, K, 300.0f, true, 1), keep);
    Outcome a2 = m.OnRelocate(R(30000, RelocSource::TeleportAck, K, 500.0f, true, 2), keep);
    Require(a1.rec.plannedNoProgress == 1 && b.rec.plannedNoProgress == 0 && a2.rec.plannedNoProgress == 1,
            "case 10: B is credited once, the return to A is not");
    Require(m.TakeCounters().plannedCredit == 1 && m.BestRemaining() == 300.0f, "case 10: one credit, best = B");
}

void Case11UnconfirmedLeg()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    Outcome o;
    for (std::uint32_t i = 1; i <= 3; ++i)
        o = m.OnRelocate(R(i * 30000, RelocSource::TeleportAck, K, 500.0f, true, NoLeg), keep);
    Require(!keep.relocs[0].plannedEffective, "case 11: planned=true without a leg index is unplanned");
    Require(o.rec.verdict.why == Reason::RelocLoop && !o.rec.planned && o.rec.relocsNoProgress == 3,
            "case 11: v4 rule - the 3rd such arrival is reloc_loop");
}

void Case14RescueTeleports()
{
    ProgressMeter m;
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    m.Feed(S(0), replan);
    int lines = 0;
    for (std::uint32_t i = 1; i <= 3; ++i)
    {
        Outcome o = m.OnRelocate(R(i * 20000, RelocSource::Rescue), replan);
        Require(o.decided && o.rec.verdict.why != Reason::RelocLoop && o.rec.relocsNoProgress == 0,
                "case 14: rescue teleports are not loop-counted");
        if (Has(Line(o.rec), "trigger=relocate:rescue"))
            ++lines;
    }
    Require(lines == 3, "case 14: three [Replan] trigger=relocate:rescue lines");
}

// ---- further rules -------------------------------------------------------------------------------------------------
void V4Equivalence()
{
    // planned=false and an unconfirmed planned flag give identical records (v5 reduces to v4).
    auto run = [](bool plannedFlag)
    {
        ProgressMeter m;
        Sink keep(Decision::Keep, Reason::LongRouteKnown);
        m.Feed(S(0), keep);
        std::vector<std::string> out;
        RelocSource const srcs[] = {RelocSource::Hearth, RelocSource::TeleportAck, RelocSource::FarMoveArrival,
                                    RelocSource::HomeTeleport, RelocSource::Death, RelocSource::Unknown};
        std::uint32_t t = 0;
        for (RelocSource src : srcs)
        {
            t += 20000;
            FeedRange(m, keep, t - 20000, t - 1000);
            out.push_back(Line(m.OnRelocate(R(t, src, K, 490.0f, plannedFlag, NoLeg), keep).rec));
        }
        return out;
    };
    Require(run(false) == run(true), "v4: an unconfirmed planned flag changes nothing");
    Require(!EffectivePlanned(true, 0, RelocSource::FarMoveArrival) && !EffectivePlanned(true, 0, RelocSource::HomeTeleport) &&
                !EffectivePlanned(true, 0, RelocSource::Rescue) && !EffectivePlanned(true, 0, RelocSource::Unknown) &&
                EffectivePlanned(true, 0, RelocSource::Hearth) && EffectivePlanned(true, 3, RelocSource::TeleportAck),
            "v5 8.1: only a hearth or a teleport ack with a confirmed leg is planned");
}

void UnknownSourceCounts()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    Outcome o;
    for (std::uint32_t i = 1; i <= 3; ++i)
        o = m.OnRelocate(R(i * 30000, RelocSource::Unknown), keep);
    Require(o.rec.verdict.why == Reason::RelocLoop && Has(Line(o.rec), "trigger=relocate:unknown"),
            "Unknown (a position jump without a source) counts as unplanned");
}

void NoTargetClears()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    FeedRange(m, keep, 0, 20000);
    m.OnRelocate(R(30000, RelocSource::FarMoveArrival), keep);
    m.OnRelocate(R(60000, RelocSource::FarMoveArrival), keep);
    std::uint32_t const ep = m.Episode();
    Outcome o = m.OnRelocate(R(90000, RelocSource::Hearth, 0), keep);
    Require(keep.relocs.back().relocsNoProgress == 0 && keep.relocs.back().forced == Reason::None && o.rec.stallS == 0,
            "no target: a relocation without a key carries no counters of the old episode");
    Require(m.Key() == 0 && m.StallMs() == 0 && m.RelocsNoProgress() == 0, "no target: the meter idles");
    m.Feed(S(91000), keep);
    Require(m.Episode() == ep + 1 && m.StallMs() == 0 && m.RelocsNoProgress() == 0,
            "no target: the same key afterwards is a new episode");
    m.Feed(S(92000, 0), keep);
    Require(m.Key() == 0 && m.StallMs() == 0, "no target: a sample without a key idles the meter");
}

void ShadowSaturation()
{
    // Shadow: the verdict is not applied, the key never changes. 300 arrivals (q218: 180-290 in 2 h): every arrival from the
    // 3rd is forced, the uint8 event counter saturates at 255 and never wraps back below the limit.
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    std::uint32_t forced = 0;
    std::uint32_t const ep = m.Episode();
    for (std::uint32_t i = 1; i <= 300; ++i)
    {
        Outcome o = m.OnRelocate(R(i * 25000, RelocSource::FarMoveArrival), keep);
        if (o.rec.forced)
            ++forced;
        Require(i < 3 || o.rec.verdict.why == Reason::RelocLoop, "shadow: every arrival from the 3rd is reloc_loop");
        Require(o.rec.episode == ep, "shadow: one episode while the key stays");
    }
    Require(forced == 298, "shadow: 298 forced decisions");
    Require(keep.relocs.back().relocsNoProgress == 255, "shadow: the uint8 field saturates");
    Require(m.RelocsNoProgress() == 300, "shadow: the meter's own counter keeps counting");
}

void SecondStallClose()
{
    ProgressMeter m;
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    std::uint32_t at1 = 0, at2 = 0;
    m.Feed(S(0), replan);
    Outcome o1 = FeedRange(m, replan, 0, 700000, K, 500.0f, ClockState::Alive, &at1);
    Outcome o2 = FeedRange(m, replan, at1, at1 + 700000, K, 500.0f, ClockState::Alive, &at2);
    Require(o1.rec.verdict.d == Decision::Replan && !o1.rec.forced, "second stall: the 1st stall is a planner Replan");
    Require(o2.rec.verdict.d == Decision::Close && o2.rec.verdict.why == Reason::SecondStall && o2.rec.verdict.closeForS == 7200 &&
                o2.rec.stallsTarget == 2,
            "second stall: the 2nd stall of the key within 2 h is a Close by rule, 7200 s");
    Require(at2 - at1 == 600000, "second stall: one OnStall per N");
}

void KeepWithoutReason()
{
    ProgressMeter m;
    Sink bad(Decision::Keep, Reason::None);
    m.Feed(S(0), bad);
    Outcome o = m.OnRelocate(R(1000, RelocSource::Hearth), bad);
    Require(o.rec.verdict.d == Decision::Replan && o.rec.verdict.why == Reason::PlannerChoice,
            "a Keep without a keep reason is a planner Replan");
}

void WrapSafeClock()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    std::uint32_t const start = 0xFFFFFFFFu - 100000u;
    m.Feed(S(start), keep);
    std::uint32_t at = 0;
    Outcome o = FeedRange(m, keep, start, start + 700000u, K, 500.0f, ClockState::Alive, &at);
    Require(o.decided && Elapsed(start, at) == 600000u, "wrap: the stall fires after 600 s across the uint32 wrap");
}

void GapAndRest()
{
    Params p;
    p.maxGapMs = 30000;
    ProgressMeter m(p);
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    m.Feed(S(60000), keep);   // a 60 s gap: not counted
    Require(m.StallMs() == 0 && m.TakeCounters().gaps == 1, "gap: a dt above MaxGapMs is not counted, gaps=1");
    FeedRange(m, keep, 60000, 460000, K, 500.0f, ClockState::Rest);
    // the first Rest interval is counted in the Alive state of the sample before; then 399 s of Rest, 300 s frozen
    Require(m.StallMs() == 1000 + 99000, "rest: Rest freezes at most RestCapS per episode");
    std::uint32_t const before = m.StallMs();
    FeedRange(m, keep, 460000, 560000, K, 500.0f, ClockState::Taxi);
    Require(m.StallMs() == before + 1000, "taxi: frozen");
}

void RouteKnownRebase()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    Sample s = S(0, K, 500.0f);
    m.Feed(s, keep);
    s = S(1000, K, 900.0f);
    s.routeKnown = true;
    m.Feed(s, keep);
    Require(m.BestRemaining() == 900.0f && m.StallMs() == 1000, "route: a flip of routeKnown re-bases without credit");
    s = S(2000, K, 895.0f);
    s.routeKnown = true;
    m.Feed(s, keep);
    Require(m.StallMs() == 2000, "route: a shrink below G is no progress");
    s = S(3000, K, 889.0f);
    s.routeKnown = true;
    m.Feed(s, keep);
    Require(m.StallMs() == 0 && m.BestRemaining() == 889.0f, "route: best - G is progress");
}

void KindProgress()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    Sample s = S(0);
    s.kind = TargetKind::Grind;
    m.Feed(s, keep);
    s.nowMs = 5000;
    s.remaining = 100.0f;   // a grind does not progress by distance
    m.Feed(s, keep);
    Require(m.StallMs() == 5000, "grind: remaining does not count");
    s.nowMs = 6000;
    s.xp = 50;
    m.Feed(s, keep);
    Require(m.StallMs() == 0, "grind: an XP gain is progress");
    s.nowMs = 7000;
    s.kills = 1;
    s.kind = TargetKind::Grind;
    m.Feed(s, keep);
    Require(m.StallMs() == 0, "grind: a kill is progress");

    ProgressMeter q;
    Sample o = S(0);
    q.Feed(o, keep);
    o.nowMs = 4000;
    q.Feed(o, keep);
    o.nowMs = 5000;
    o.creditSeq = 1;
    q.Feed(o, keep);
    Require(q.StallMs() == 0, "objective: a credit step is progress");
}

void KeyEviction()
{
    Params p;
    p.maxReplansPerHour = 100;
    ProgressMeter m(p);
    Sink replan(Decision::Replan, Reason::PlannerChoice);
    std::uint32_t t = 0;
    std::vector<std::uint64_t> keys;
    for (std::uint32_t i = 0; i < 9; ++i)
        keys.push_back(TargetKey(100 + i, 1, 0, 0, 0));
    for (std::uint64_t k : keys)
    {
        std::uint32_t at = 0;
        m.Feed(S(t, k), replan);
        FeedRange(m, replan, t, t + 600000, k, 500.0f, ClockState::Alive, &at);
        t = at;
    }
    // 9 keys, 8 slots: the first key's give-up was evicted, the 9th is still known
    std::uint32_t at = 0;
    m.Feed(S(t, keys[0]), replan);
    Outcome o0 = FeedRange(m, replan, t, t + 600000, keys[0], 500.0f, ClockState::Alive, &at);
    Require(!o0.rec.forced, "eviction: the oldest key was evicted, its next stall is no second_stall");
    t = at;
    m.Feed(S(t, keys[8]), replan);
    Outcome o8 = FeedRange(m, replan, t, t + 600000, keys[8], 500.0f);
    Require(o8.rec.verdict.why == Reason::SecondStall, "eviction: a recent key keeps its give-up");
}

void KeysAndCells()
{
    Cell a = CellOf(-1.0f, -50.0f, -0.5f);
    Require(a.x == -1 && a.y == -1 && a.z == -1, "cell: floor for negative coordinates");
    Cell b = CellOf(-50.1f, 49.9f, 10.0f);
    Require(b.x == -2 && b.y == 0 && b.z == 1, "cell: borders");
    Require(TargetKey(1, 2, 1.0f, 1.0f, 1.0f) == TargetKey(1, 2, 49.0f, 49.0f, 9.0f), "key: same cell, same key");
    Require(TargetKey(1, 2, 1.0f, 1.0f, 1.0f) != TargetKey(1, 2, -1.0f, 1.0f, 1.0f), "key: x=-1 and x=1 are different cells");
    Require(TargetKey(1, 2, 1.0f, 1.0f, 1.0f) != TargetKey(1, 2, 1.0f, 1.0f, 11.0f), "key: the 10 yd z band");
    Require(TargetKey(1, 2, 1.0f, 1.0f, 1.0f) != TargetKey(1, 3, 1.0f, 1.0f, 1.0f), "key: the entry");
    Require(TargetKey(0, 0, 0.0f, 0.0f, 0.0f) != 0, "key: never 0");
}

// ---- R9-2 (twow-repo#575, OB-10 nits 1 and 2) ------------------------------------------------------------------------------
// Nit 1: a forced reloc_loop Replan is no give-up, so the next stall of the same key is its first (no second_stall Close).
void RelocLoopNoGiveUp()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    FeedRange(m, keep, 0, 30000);
    m.OnRelocate(R(30000, RelocSource::FarMoveArrival), keep);
    FeedRange(m, keep, 30000, 60000);
    m.OnRelocate(R(60000, RelocSource::FarMoveArrival), keep);
    FeedRange(m, keep, 60000, 90000);
    Outcome const loop = m.OnRelocate(R(90000, RelocSource::FarMoveArrival), keep);
    Require(loop.rec.forced && loop.rec.verdict.why == Reason::RelocLoop, "nit 1: the 3rd arrival is the forced reloc_loop");
    Outcome const o = FeedRange(m, keep, 90000, 700000);
    Require(o.decided && keep.stalls.size() == 1, "nit 1: the stall of the same key fires");
    Require(keep.stalls[0].stallsThisTarget == 1 && keep.stalls[0].forced != Reason::SecondStall &&
                o.rec.verdict.why != Reason::SecondStall,
            "nit 1: a reloc_loop Replan is no give-up (stalls_target 1, no second_stall)");
}

// Nit 2: forced_after_rate_limited counts a forced decision right after a rate_limited Keep; progress or another Keep in between
// clears the mark.
void ForcedAfterRateLimitedResets()
{
    for (int variant = 0; variant < 3; ++variant)
    {
        Params p;
        p.maxReplansPerHour = 1;
        ProgressMeter m(p);
        Sink replan(Decision::Replan, Reason::PlannerChoice);
        Sink keep(Decision::Keep, Reason::LongRouteKnown);
        m.Feed(S(0), replan);
        Outcome const first = FeedRange(m, replan, 0, 600000);
        Require(first.decided && first.rec.verdict.d == Decision::Replan, "nit 2: the 1st stall is a planner Replan");
        Outcome const capped = m.OnRelocate(R(600500, RelocSource::FarMoveArrival), replan);
        Require(capped.rec.verdict.why == Reason::RateLimited, "nit 2: the relocation's Replan is over the cap");
        std::uint32_t from = 600000;
        float rem = 500.0f;
        if (variant == 1)
        {
            rem = 480.0f;   // progress clears the mark
            m.Feed(S(601000, K, rem), replan);
            from = 601000;
        }
        else if (variant == 2)
            m.OnRelocate(R(600700, RelocSource::FarMoveArrival), keep);   // a Keep with a keep reason clears the mark
        Outcome const second = FeedRange(m, replan, from, from + 700000, K, rem);
        Require(second.decided && second.rec.forced && second.rec.verdict.why == Reason::SecondStall,
                "nit 2: the 2nd stall of the key is the forced second_stall");
        Counters const c = m.TakeCounters();
        std::uint32_t const want = variant == 0 ? 1u : 0u;
        Require(c.forced == 1 && c.forcedAfterRateLimited == want, "nit 2: forced_after_rate_limited only right after rate_limited");
    }
}

// OB-10 R-2, the meter side: the Dead clock is frozen at a constant held distance, and a revive closer by re-bases downward
// without credit. The shrinking ghost distance through the dead hold (and a revive farther away) is tested in
// t/replan_wire_policy_tests.cpp GhostRunThroughHold.
void GhostRunNoStall()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::RespawnWait);
    m.Feed(S(0), keep);
    FeedRange(m, keep, 0, 20000);
    m.OnRelocate(R(20000, RelocSource::Death), keep);
    std::uint32_t const before = m.StallMs();
    Outcome const ghost = FeedRange(m, keep, 20000, 20000 + 2 * 600000, K, 500.0f, ClockState::Dead);
    Require(!ghost.decided && keep.stalls.empty(), "R-2: a ghost run longer than N gives no stall");
    Require(m.StallMs() <= before + 1000, "R-2: the clock pauses while dead");
    m.OnRelocate(R(20000 + 2 * 600000 + 500, RelocSource::Revive, K, 300.0f), keep);
    Require(m.BestRemaining() == 300.0f && m.StallMs() == before + 1000, "R-2: the revive re-bases the best distance");
    Counters const c = m.TakeCounters();
    Require(c.plannedCredit == 0 && m.RelocsNoProgress() == 0, "R-2: the revive gives no credit and is not loop-counted");
    m.Feed(S(20000 + 2 * 600000 + 1000, K, 300.0f), keep);
    Require(m.StallMs() == before + 1000, "R-2: no progress reset after the revive (re-based, not credited)");
}

void AddCounters()
{
    Counters a;
    Counters b;
    a.stalls = 1; a.relocates = 2; a.keep = 3; a.replan = 4; a.close = 5;
    a.forced = 6; a.rateLimited = 7; a.forcedAfterRateLimited = 8; a.gaps = 9; a.plannedCredit = 10;
    a.hops = 11; a.grindNoKill = 12; a.offMap = 13;
    b.stalls = 10; b.relocates = 20; b.keep = 30; b.replan = 40; b.close = 50;
    b.forced = 60; b.rateLimited = 70; b.forcedAfterRateLimited = 80; b.gaps = 90; b.plannedCredit = 100;
    b.hops = 110; b.grindNoKill = 120; b.offMap = 130;
    Add(a, b);
    Require(a.stalls == 11 && a.relocates == 22 && a.keep == 33 && a.replan == 44 && a.close == 55 && a.forced == 66 &&
                a.rateLimited == 77 && a.forcedAfterRateLimited == 88 && a.gaps == 99 && a.plannedCredit == 110 && a.hops == 121 &&
                a.grindNoKill == 132 && a.offMap == 143,
            "counters: Add sums every field");
}

void Lines()
{
    ProgressMeter m;
    Sink keep(Decision::Keep, Reason::LongRouteKnown);
    m.Feed(S(0), keep);
    Outcome o = FeedRange(m, keep, 0, 600000);
    std::string const l = Line(o.rec, true);
    Require(l.rfind("[Replan] bot=166 cohort=B arbiter=2 shadow=1 trigger=stall level=12 quest=218 target_entry=808 cell=2:4:3 "
                    "kind=objective episode=1 stall_s=600 ",
                    0) == 0,
            "line: tag first, then bot, cohort, arbiter, shadow");
    Require(Has(l, " decision=keep reason=long_route_known forced=0 close_s=0") && Has(l, " keeps_in_row=0 "),
            "line: decision fields");
    Require(l.size() > 9 && l.compare(l.size() - 9, 9, " offmap=0") == 0, "line: offmap=0 last (R9-2, OB-10 R-6)");
    LineCtx off;
    off.offmap = true;
    char wide[1024];
    FormatReplanLine(wide, sizeof(wide), off, o.rec);
    std::string const lo = wide;
    Require(lo.size() > 9 && lo.compare(lo.size() - 9, 9, " offmap=1") == 0, "line: an off-map target is tagged offmap=1");
    Counters k;
    k.stalls = 1;
    k.forced = 2;
    k.forcedDropped = 4;
    char buf[256];
    FormatReplanMinLine(buf, sizeof(buf), 'A', false, k, 3);
    Require(std::string(buf) == "[ReplanMin] cohort=A shadow=0 stalls=1 relocates=0 keep=0 replan=0 close=0 forced=2 "
                                "rate_limited=0 forced_after_rate_limited=0 gaps=0 planned_credit=0 hops=0 grind_nokill=0 offmap=0 "
                                "dropped=3 forced_dropped=4",
            "line: [ReplanMin]");
    char small[16];
    std::size_t volatile smallN = sizeof(small);   // runtime size: the truncation is the point of the check
    std::size_t const n = FormatReplanMinLine(small, smallN, 'A', false, k, 0);
    Require(n == 15 && std::strlen(small) == 15, "line: truncated to the buffer");
    Require(FormatReplanMinLine(small, 0, 'A', false, k, 0) == 0, "line: n == 0 writes nothing");
}
}   // namespace

int main()
{
    Case1ArrivalLoop();
    Case2LoopWithCap();
    Case3ClockContinuity();
    Case4ProgressResets();
    Case5KeyChange();
    Case6DeathRevive();
    Case7CappedReplans();
    Case8PlannedChain();
    Case9PlannedLoop();
    Case10CreditOnce();
    Case11UnconfirmedLeg();
    Case14RescueTeleports();
    V4Equivalence();
    UnknownSourceCounts();
    NoTargetClears();
    ShadowSaturation();
    SecondStallClose();
    KeepWithoutReason();
    WrapSafeClock();
    GapAndRest();
    RouteKnownRebase();
    KindProgress();
    KeyEviction();
    KeysAndCells();
    Lines();
    RelocLoopNoGiveUp();
    ForcedAfterRateLimitedResets();
    GhostRunNoStall();
    AddCounters();
    std::cout << "replan_policy: " << g_checks << " checks passed\n";
    return 0;
}
