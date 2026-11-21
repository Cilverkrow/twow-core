// A3 R9 / R2 wiring pair R9-2 (twow-repo#575; R9-REPLAN-INTERFACE v5 9575095d, R9-PREREG C1/C2; DESIGN-R9-2-20261120.md D2-D13):
// the pure rules between the planner tick (PlannerShadow.cpp) and the progress meter (ReplanPolicy.h): the relocation latch of
// PlayerbotAI (site, hop mark, teleport ack, take, expiry), the relocation of a tick (death and revive edges, hops), the sample
// clock, the switch and scope, the XP step and the sink's view. The header names no game type, so this suite compiles it
// standalone; t/contracts.d/r9_replan_wiring_source_contract_tests.cmake pins the game side.

#include "ReplanWirePolicy.h"
#include "SupervisorPolicy.h"
#include "RotCastPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
using namespace ai::replan_wire;
using ai::replan::ClockState;
using ai::replan::Decision;
using ai::replan::Reason;
using ai::replan::RelocSource;

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

// ---- the latch -------------------------------------------------------------------------------------------------------------
void PackRoundTrip()
{
    std::uint64_t const l = Pack(RelocSource::Rescue, std::uint8_t(FlagAcked | FlagHop), 0xFFFFFFF0u);
    Require(SrcOf(l) == RelocSource::Rescue && Acked(l) && Hop(l) && MsOf(l) == 0xFFFFFFF0u, "pack: source, flags, ms round trip");
    Require(Pack(RelocSource::Unknown, 0, 0) == 0 && !Fresh(0, 0) && !Stale(0, 0), "pack: the empty latch is neither fresh nor stale");
    Require(IsSiteSource(RelocSource::Hearth) && IsSiteSource(RelocSource::HomeTeleport) && IsSiteSource(RelocSource::Rescue) &&
                IsSiteSource(RelocSource::FarMoveArrival),
            "site sources: hearth, home teleport, rescue, far-move arrival");
    Require(!IsSiteSource(RelocSource::Unknown) && !IsSiteSource(RelocSource::TeleportAck) && !IsSiteSource(RelocSource::Death) &&
                !IsSiteSource(RelocSource::Revive),
            "site sources: never unknown, teleport_ack, death, revive");
}

void HearthAckedThenTaken()
{
    std::uint32_t const t0 = 1000;
    std::uint64_t l = MergeSite(0, RelocSource::Hearth, t0);
    Require(SrcOf(l) == RelocSource::Hearth && !Acked(l) && Fresh(l, t0 + kSiteTtlMs) && !Fresh(l, t0 + kSiteTtlMs + 1),
            "hearth: an un-acked site lives kSiteTtlMs");
    l = MergeAck(l, t0 + 10500);   // the 10 s cast, then the ack
    Require(SrcOf(l) == RelocSource::Hearth && Acked(l) && MsOf(l) == t0 + 10500, "hearth: the ack confirms and restamps the site");
    Taken const a = Take(l, t0 + 13000);
    Require(a.source == RelocSource::Hearth && !a.hop, "hearth: the jump 2.5 s after the ack takes hearth");
    Taken const b = Take(l, t0 + 10500 + kAckTtlMs + 1);
    Require(b.source == RelocSource::Unknown && !b.hop, "hearth: after kAckTtlMs the entry is stale (unknown)");
    Require(Take(l, t0 + 10500 + kAckTtlMs).source == RelocSource::Hearth, "hearth: the ack window is inclusive");
}

void AckWithoutSite()
{
    std::uint64_t const l = MergeAck(0, 5000);
    Require(SrcOf(l) == RelocSource::TeleportAck && Acked(l) && !Hop(l), "ack: no site gives teleport_ack (D7)");
    Require(Take(l, 6000).source == RelocSource::TeleportAck, "ack: taken as teleport_ack");
}

void SiteServesOneAck()
{
    std::uint64_t l = MergeSite(0, RelocSource::Rescue, 1000);
    l = MergeAck(l, 2000);
    Require(SrcOf(l) == RelocSource::Rescue && Acked(l), "rescue: the 1st ack keeps rescue");
    l = MergeAck(l, 3000);
    Require(SrcOf(l) == RelocSource::TeleportAck && Acked(l) && MsOf(l) == 3000, "rescue: the 2nd ack is a teleport_ack");
}

void OldSiteLost()
{
    std::uint64_t l = MergeSite(0, RelocSource::HomeTeleport, 1000);
    l = MergeAck(l, 1000 + kSiteTtlMs + 1);
    Require(SrcOf(l) == RelocSource::TeleportAck, "site: older than kSiteTtlMs at the ack gives teleport_ack");
}

void NewestSiteWins()
{
    std::uint64_t l = MergeSite(0, RelocSource::Hearth, 1000);
    l = MergeSite(l, RelocSource::Rescue, 1200);
    Require(SrcOf(l) == RelocSource::Rescue && MsOf(l) == 1200, "site: the newest site wins");
    l = MergeAck(l, 1500);
    l = MergeSite(l, RelocSource::FarMoveArrival, 1600);
    Require(SrcOf(l) == RelocSource::FarMoveArrival && !Acked(l), "site: a new site replaces an acked one");
}

void HopMark()
{
    std::uint64_t l = MergeHop(0, 1000);
    Require(Hop(l) && !Acked(l) && SrcOf(l) == RelocSource::Unknown, "hop: the mark (D6)");
    l = MergeAck(l, 1400);
    Require(SrcOf(l) == RelocSource::TeleportAck && Acked(l) && Hop(l), "hop: the ack keeps the mark as teleport_ack");
    Taken const t = Take(l, 2000);
    Require(t.hop && t.source == RelocSource::TeleportAck, "hop: taken with the mark");
    l = MergeHop(MergeSite(0, RelocSource::Hearth, 1000), 1100);
    Require(Hop(l) && SrcOf(l) == RelocSource::Unknown, "hop: a later hop replaces a site");
    std::uint64_t const stale = MergeHop(0, 1000);
    Require(!Hop(MergeAck(stale, 1000 + kSiteTtlMs + 1)), "hop: a stale mark is lost at the ack");
}

void StaleRules()
{
    std::uint64_t const acked = MergeAck(0, 1000);
    Require(!Stale(acked, 1000 + kAckTtlMs) && Stale(acked, 1000 + kAckTtlMs + 1), "stale: an ack after kAckTtlMs");
    std::uint64_t const site = MergeSite(0, RelocSource::Hearth, 1000);
    Require(!Stale(site, 1000 + kSiteTtlMs) && Stale(site, 1000 + kSiteTtlMs + 1), "stale: a site after kSiteTtlMs");
}

void WrapSafe()
{
    std::uint32_t const t0 = 0xFFFFF000u;
    std::uint64_t l = MergeSite(0, RelocSource::Hearth, t0);
    l = MergeAck(l, 0x00000400u);   // 5120 ms later, across the wrap
    Require(SrcOf(l) == RelocSource::Hearth && Acked(l), "wrap: the site is fresh across the 2^32 wrap");
    Require(Take(l, 0x00000800u).source == RelocSource::Hearth, "wrap: taken across the wrap");
    Require(Take(0, 12345).source == RelocSource::Unknown && !Take(0, 12345).hop, "take: the empty latch is unknown");
}

// ---- the relocation of a tick (D5, D6) ------------------------------------------------------------------------------------
void Relocations()
{
    Taken hearth;
    hearth.source = RelocSource::Hearth;
    Taken hop;
    hop.source = RelocSource::TeleportAck;
    hop.hop = true;
    Taken none;
    RelocSource out = RelocSource::Unknown;
    Require(RelocationOf(true, false, true, false, hearth, out) && out == RelocSource::Death, "reloc: the death edge");
    Require(RelocationOf(true, true, false, false, none, out) && out == RelocSource::Death, "reloc: death before revive");
    Require(RelocationOf(false, true, true, true, hearth, out) && out == RelocSource::Revive, "reloc: the revive edge");
    Require(RelocationOf(false, false, true, true, hearth, out) && out == RelocSource::Hearth, "reloc: a living jump takes the source");
    Require(RelocationOf(false, false, true, true, none, out) && out == RelocSource::Unknown, "reloc: a jump outside the funnel");
    Require(!RelocationOf(false, false, true, true, hop, out), "reloc: a movement hop is no relocation (D6)");
    Require(!RelocationOf(false, false, true, false, hearth, out), "reloc: a jump while dead is none (frozen clock)");
    Require(!RelocationOf(false, false, false, true, hearth, out), "reloc: no jump, no relocation");
}

// ---- the sample ----------------------------------------------------------------------------------------------------------
void Clock()
{
    Require(ClockOf(false, true, true, true, true, true) == ClockState::Dead, "clock: dead first");
    Require(ClockOf(true, true, true, true, true, true) == ClockState::Taxi, "clock: taxi before transport");
    Require(ClockOf(true, false, true, true, true, true) == ClockState::Transport, "clock: transport before passive");
    Require(ClockOf(true, false, false, true, true, true) == ClockState::Passive, "clock: passive before combat (D8)");
    Require(ClockOf(true, false, false, false, true, true) == ClockState::Combat, "clock: combat before rest");
    Require(ClockOf(true, false, false, false, false, true) == ClockState::Rest, "clock: rest");
    Require(ClockOf(true, false, false, false, false, false) == ClockState::Alive, "clock: alive");
}

void Remaining()
{
    Require(RemainingYd(false, 1.0f, 1.0f, 1.0f) == kOffMapYd, "remaining: another map");
    Require(RemainingYd(true, 3.0f, 4.0f, 0.0f) == 5.0f, "remaining: straight 3D distance");
    Require(RemainingYd(true, 0.0f, 0.0f, 0.0f) == 0.0f, "remaining: at the target");
    Require(RemainingYd(true, 0.0f, 0.0f, -12.0f) == 12.0f, "remaining: the z part counts");
}

void Xp()
{
    Require(XpStep(0, 0, 10, 500) == 0, "xp: the first sample gives nothing");
    Require(XpStep(10, 500, 10, 650) == 150, "xp: the gain inside a level");
    Require(XpStep(10, 650, 10, 600) == 0, "xp: a loss inside a level is no gain");
    Require(XpStep(10, 900, 11, 40) == 40, "xp: a level-up counts the XP of the new level");
    Require(XpStep(10, 900, 11, 0) == 1, "xp: a level-up with 0 XP still counts 1");
    Require(XpStep(11, 40, 10, 900) == 0, "xp: a level going down gives nothing");
}

// ---- switch and scope (D2, D4) ---------------------------------------------------------------------------------------------
void SwitchAndScope()
{
    for (std::uint32_t mode = 0; mode <= 4; ++mode)
        for (std::uint32_t shadow = 0; shadow <= 2; ++shadow)
            for (std::uint32_t guid = 10; guid <= 11; ++guid)
                for (int scope = 0; scope <= 1; ++scope)
                {
                    bool const odd = (guid & 1u) != 0;
                    bool const active = mode == 1 || (mode == 2 && odd) || (mode == 3 && !odd);
                    bool const on = scope && (active || shadow == 1);
                    Require(Active(mode, guid) == active, "switch: the active arm is the cohort mode (0 off, 1 all, 2 B, 3 A, else off)");
                    Require(MeterOn(mode, shadow, guid, scope != 0) == on,
                            "switch: the meter runs in scope for the active arm, or for every bot with Replan.Shadow 1");
                }
}

// ---- the sink (D13) ------------------------------------------------------------------------------------------------------
void Sink()
{
    PlannerView v;
    ai::replan::Verdict r = PlannerVerdict(v);
    Require(r.d == Decision::Replan && r.why == Reason::PlannerChoice, "sink: no keep reason gives Replan planner_choice");
    v.respawnWait = true;
    r = PlannerVerdict(v);
    Require(r.d == Decision::Keep && r.why == Reason::RespawnWait, "sink: the W8 wait keeps (respawn_wait)");
    v.groupMember = true;
    r = PlannerVerdict(v);
    Require(r.d == Decision::Keep && r.why == Reason::GroupMember, "sink: a group member keeps first (group_member)");
}

void Respawn()
{
    Require(RespawnWait(1000, true, 77, 77, 120, 1000 + 180000), "respawn: inside respawn + 60 s");
    Require(!RespawnWait(1000, true, 77, 77, 120, 1000 + 180001), "respawn: after respawn + 60 s");
    Require(!RespawnWait(0, true, 77, 77, 120, 2000), "respawn: no wait");
    Require(!RespawnWait(1000, false, 77, 77, 120, 2000), "respawn: no pick");
    Require(!RespawnWait(1000, true, 0, 0, 120, 2000), "respawn: a pick without a destination");
    Require(!RespawnWait(1000, true, 77, 78, 120, 2000), "respawn: the live target is not the pick");
    Require(RespawnWait(0xFFFFFF00u, true, 77, 77, 0, 0x00001000u), "respawn: wrap-safe");
}

// ---- OB-10 conditions R-2, R-3, R-5, R-6, R-7 ------------------------------------------------------------------------------
void HopOneShot()
{
    // R-3: a hop, then a hearth in the next tick: hearth.
    std::uint64_t l = MergeHop(0, 1000);
    l = MergeAck(l, 1200);
    Require(!DropAtTickEnd(MergeHop(0, 1000), 1100), "R-3: an un-acked hop mark waits for its ack");
    Require(DropAtTickEnd(l, 1300), "R-3: an acked hop mark is dropped at the end of a tick without a jump");
    l = MergeSite(l, RelocSource::Hearth, 2000);
    l = MergeAck(l, 12000);
    Require(Take(l, 13000).source == RelocSource::Hearth && !Take(l, 13000).hop, "R-3: a hop, then a hearth: hearth");
    Require(!DropAtTickEnd(l, 13000), "R-3: an acked site stays until its jump (or stale)");
    Require(DropAtTickEnd(l, 12000 + kAckTtlMs + 1) && !DropAtTickEnd(0, 5000), "tick end: stale dropped, empty untouched");

    // R-3, the guarded sequence: hop, ack, a tick without a jump (the latch is cleared as ExpireLastRelocSource does), then a jump
    // without an ack: unknown, no hop, and it is a relocation.
    std::uint64_t g = MergeAck(MergeHop(0, 1000), 1200);
    if (DropAtTickEnd(g, 1300))
        g = 0;
    Require(g == 0, "R-3: the acked hop mark is cleared at the tick end");
    Taken const jump = Take(g, 1800);
    RelocSource out = RelocSource::Hearth;
    Require(jump.source == RelocSource::Unknown && !jump.hop && RelocationOf(false, false, true, true, jump, out) &&
                out == RelocSource::Unknown,
            "R-3: a later jump without an ack is an unknown relocation, not a hop");
    // The counter-case: without the drop the acked mark is still fresh at that jump and hides it.
    Taken const hidden = Take(MergeAck(MergeHop(0, 1000), 1200), 1800);
    Require(hidden.hop && !RelocationOf(false, false, true, true, hidden, out), "R-3: without the drop the mark would hide the jump");
}

// Known limit (plan verify-0): the hearth label is set at cast start (D12). A zero-distance resync ack during the 10 s cast (the
// fell-off or transport near teleport of PlayerbotAI) takes the label, so the real arrival reads teleport_ack. Both sources are
// loop-counted, so reloc_loop is unchanged; only the relocate_src name differs.
void HearthResyncDuringCast()
{
    std::uint32_t const t0 = 1000;
    std::uint64_t l = MergeSite(0, RelocSource::Hearth, t0);
    l = MergeAck(l, t0 + 3000);   // the resync ack, no jump
    Require(SrcOf(l) == RelocSource::Hearth && Acked(l), "hearth resync: the resync ack takes the hearth label");
    Require(!DropAtTickEnd(l, t0 + 4000), "hearth resync: a tick without a jump keeps the acked label");
    l = MergeAck(l, t0 + 10500);   // the real arrival
    Taken const t = Take(l, t0 + 12000);
    Require(t.source == RelocSource::TeleportAck && !t.hop, "hearth resync: the real arrival reads teleport_ack");
    Require(ai::replan::LoopCounted(RelocSource::Hearth) && ai::replan::LoopCounted(RelocSource::TeleportAck),
            "hearth resync: hearth and teleport_ack are both loop-counted (reloc_loop unchanged)");
}

// D11: forced records are never dropped, every one of them (no apply before P-3, so a later stall of the same key and episode is
// forced again); an unforced record takes the [Replan] budget.
void ForcedOutsideBudget()
{
    ai::replan::Record r;
    Require(!OutsideLineBudget(r), "D11: an unforced record takes the budget");
    r.forced = true;
    r.targetKey = 42;
    r.episode = 3;
    Require(OutsideLineBudget(r), "D11: the first forced record of a (key, episode) is outside the budget (the C2 (a) base)");
    Require(OutsideLineBudget(r), "D11: a second forced record of the same (key, episode) is outside it too");
    r.episode = 4;
    Require(OutsideLineBudget(r), "D11: a forced record of a new episode is outside it");
}


// R9-a (OB-10 review core#1100 36d45289): forced lines have a global safety ceiling above the 300 / min [Replan] budget; the tick
// takes it with the same per-minute window as every line budget (RotCastPolicy.h BudgetTake). A refused forced line is
// forced_dropped=, the decision stays in forced=.
void ForcedLineCeiling()
{
    static_assert(kForcedLinesPerMinute == 600, "R9-a: the forced-line ceiling is 600 per minute");
    static_assert(kForcedLinesPerMinute > 300, "R9-a: the ceiling sits above the [Replan] budget");
    std::uint32_t window = 0, used = 0;
    std::uint32_t const t0 = 1000000;
    std::uint32_t taken = 0;
    for (std::uint32_t i = 0; i < kForcedLinesPerMinute + 5; ++i)
        taken += ai::rot_cast::BudgetTake(window, used, t0 + (i % 50), kForcedLinesPerMinute) ? 1u : 0u;
    Require(taken == kForcedLinesPerMinute, "R9-a: at most kForcedLinesPerMinute forced lines in one minute");
    Require(ai::rot_cast::BudgetTake(window, used, t0 + 60, kForcedLinesPerMinute), "R9-a: a new minute opens the ceiling again");
    ai::replan::Counters a, b;
    a.forced = 3;
    a.forcedDropped = 1;
    b.forced = 2;
    b.forcedDropped = 2;
    ai::replan::Add(a, b);
    Require(a.forced == 5 && a.forcedDropped == 3, "R9-a: forced_dropped sums per cohort, forced keeps every decision");
}
void GrindAndOffMap()
{
    Require(GrindNoKill(ai::replan::TargetKind::Grind), "R-5: a grind target is not fed without a kill counter");
    for (ai::replan::TargetKind k : {ai::replan::TargetKind::Objective, ai::replan::TargetKind::Giver, ai::replan::TargetKind::Taker,
                                     ai::replan::TargetKind::Rpg, ai::replan::TargetKind::Profession, ai::replan::TargetKind::Other})
        Require(!GrindNoKill(k), "R-5: every other kind is fed");
    Require(OffMap(5, kOffMapYd) && OffMap(5, RemainingYd(false, 0.0f, 0.0f, 0.0f)), "R-6: a target on another map is off-map");
    Require(!OffMap(5, 99999.0f) && !OffMap(0, kOffMapYd), "R-6: a same-map target or no target is not off-map");
}

void DeadHold()
{
    ai::replan::Sample held;
    ai::replan::Sample live;
    live.nowMs = 1000;
    live.targetKey = 42;
    live.quest = 7;
    live.targetEntry = 9;
    live.tx = 1.0f;
    live.kind = ai::replan::TargetKind::Objective;
    live.remaining = 300.0f;
    live.creditSeq = 3;
    HoldWhileDead(live, held, true);
    Require(held.targetKey == 42 && held.remaining == 300.0f && live.targetKey == 42, "R-2: alive, the sample is held unchanged");

    ai::replan::Sample ghost;   // the ghost: no live target, or a closer one
    ghost.nowMs = 2000;
    ghost.state = ClockState::Dead;
    ghost.remaining = 50.0f;
    HoldWhileDead(ghost, held, false);
    Require(ghost.targetKey == 42 && ghost.remaining == 300.0f && ghost.creditSeq == 3 && ghost.quest == 7 && ghost.nowMs == 2000 &&
                ghost.state == ClockState::Dead,
            "R-2: dead, the sample takes the last living target and distance (no ghost credit), keeps its own time and state");

    ai::replan::Sample none;
    HoldWhileDead(none, held, true);
    Require(held.targetKey == 0, "R-2: alive without a target drops the held one");
    ai::replan::Sample ghost2;
    ghost2.remaining = 77.0f;
    HoldWhileDead(ghost2, held, false);
    Require(ghost2.targetKey == 0, "R-2: dead with nothing held and no live target, the meter idles");
    ai::replan::Sample ghost3;   // dead with nothing held, but a live key (the meter made while dead, or died with key 0)
    ghost3.targetKey = 42;
    ghost3.remaining = 77.0f;
    ghost3.state = ClockState::Dead;
    HoldWhileDead(ghost3, held, false);
    Require(ghost3.targetKey == 0, "R-2: dead with nothing held, a live key idles the meter (no live ghost distance)");
}

// A sink that keeps with a keep reason and counts the calls.
struct KeepSink : ai::replan::IReplanSink
{
    int calls = 0;
    ai::replan::Verdict OnStall(ai::replan::StallEvent const&) override
    {
        ++calls;
        return Keep();
    }
    ai::replan::Verdict OnRelocate(ai::replan::RelocationEvent const&) override
    {
        ++calls;
        return Keep();
    }
    static ai::replan::Verdict Keep()
    {
        ai::replan::Verdict v;
        v.d = Decision::Keep;
        v.why = Reason::RespawnWait;
        return v;
    }
};

ai::replan::Sample Tick(std::uint32_t now, float remaining, bool alive)
{
    ai::replan::Sample s;
    s.nowMs = now;
    s.targetKey = 42;
    s.quest = 7;
    s.targetEntry = 9;
    s.tx = 1.0f;
    s.kind = ai::replan::TargetKind::Objective;
    s.remaining = remaining;
    s.state = alive ? ClockState::Alive : ClockState::Dead;
    return s;
}

// OB-10 R-2 through the wiring: the dead hold in front of the meter. Alive at 500 yd for 300 s (the stall clock runs), then a ghost
// run from 500 yd down to 50 yd over 700 s (longer than N). With the hold: no stall, no progress, the clock stays paused. Without
// it (the counter-case) the shrinking ghost distance is credited as progress and resets the clock.
void GhostRunThroughHold()
{
    for (int withHold = 0; withHold <= 1; ++withHold)
    {
        ai::replan::ProgressMeter m;
        KeepSink sink;
        ai::replan::Sample held;
        std::uint32_t t = 0;
        for (; t <= 300000; t += 1000)
        {
            ai::replan::Sample s = Tick(t, 500.0f, true);
            HoldWhileDead(s, held, true);
            m.Feed(s, sink);
        }
        std::uint32_t const before = m.StallMs();
        Require(before >= 290000, "ghost: the stall clock ran while alive");
        bool decided = false;
        bool reset = false;
        for (std::uint32_t i = 1; i <= 700; ++i, t += 1000)
        {
            ai::replan::Sample s = Tick(t, 500.0f - 450.0f * float(i) / 700.0f, false);
            if (withHold)
                HoldWhileDead(s, held, false);
            decided = m.Feed(s, sink).decided || decided;
            reset = reset || m.StallMs() < before;
        }
        if (!withHold)
        {
            Require(reset, "ghost: without the dead hold the ghost distance is credited (the hold is what prevents it)");
            continue;
        }
        Require(!decided && !reset && sink.calls == 0 && m.StallMs() <= before + 1000,
                "R-2: a ghost run longer than N through the hold gives no stall and no progress");
        // The revive at a spirit healer farther away (800 yd > best 500): the meter re-bases only downward (v5 7.1), so the best
        // stays 500 and the walk back counts as stall time. Recorded for OB-10: "Revive re-bases" means min-only.
        ai::replan::RelocationEvent e;
        e.source = RelocSource::Revive;
        e.atMs = t;
        e.targetKey = 42;
        e.remaining = 800.0f;
        m.OnRelocate(e, sink);
        Require(m.BestRemaining() == 500.0f, "R-2: a revive farther away keeps the best distance (min-only re-base)");
        Require(m.StallMs() <= before + 1000 && m.StallMs() >= before, "R-2: the revive gives no credit");
    }
}

void PlanLineGolden()
{
    // R-7 (golden, text level only): the [Plan] relocate_src names of P-2b's placeholder enum are unchanged under
    // ai::replan::SourceName, so with both R9 keys 0 (the latch stays empty: unknown) the [Plan] line is the P-2b line. The runtime
    // golden ([Plan] / [PlanMin] of a test realm at Planner 1 and both R9 keys 0 against a P-2b run) is not covered here.
    Require(std::string(ai::replan::SourceName(RelocSource::Unknown)) == "unknown", "golden: unknown");
    Require(std::string(ai::replan::SourceName(RelocSource::Hearth)) == "hearth", "golden: hearth");
    Require(std::string(ai::replan::SourceName(RelocSource::HomeTeleport)) == "home_teleport", "golden: home_teleport");
    Require(std::string(ai::replan::SourceName(RelocSource::Rescue)) == "rescue", "golden: rescue");
    Require(std::string(ai::replan::SourceName(RelocSource::FarMoveArrival)) == "far_move_arrival", "golden: far_move_arrival");
    Require(std::string(ai::replan::SourceName(RelocSource::TeleportAck)) == "teleport_ack", "golden: teleport_ack");
    Require(Take(0, 1000).source == RelocSource::Unknown, "golden: the empty latch of the off path is unknown");

    // The whole [Plan] line with the off-path source equals the P-2b line of t/supervisor_policy_tests.cpp byte for byte.
    ai::supervisor::PlanLine p;
    p.guid = 7;
    p.cohort = 'A';
    p.mode = ai::supervisor::Mode::Quest;
    p.kind = "quest_turnin";
    p.id = 783;
    p.destId = 5;
    p.value = 12.5f;
    p.xpm = 30.4f;
    p.routeSec = 60;
    p.turnIn = true;
    p.reason = "turnin_first";
    p.cands = 4;
    p.alt[0].kind = "grind";
    p.alt[0].id = 3;
    p.alt[0].value = 4.5f;
    p.level = 6;
    p.zone = 85;
    p.band = 85;
    p.tier = "A";
    p.ev = "quest_complete";
    p.fb = "band_table_off";
    p.marginPct = 15.0f;
    p.cut = 2;
    p.skip = 3;
    p.relocSrc = ai::replan::SourceName(Take(0, 1000).source);
    p.floor = "ok";
    p.hold = "switch";
    p.ms = 1;
    char buf[1024];
    ai::supervisor::FormatPlanLine(buf, sizeof(buf), p);
    Require(std::string(buf) ==
                "[Plan] bot=7 cohort=A arbiter=0 mode=QUEST pick=quest_turnin:783:5 value=12.50 xpm=30.4 route_s=60 work_s=0 rest_s=0 "
                "danger=0.00 turnin=1 brain=0 reason=turnin_first cands=4 alt1=grind:3:4.50 alt2=- alt3=- blocked=0 budget_ms=0 lvl=6 "
                "zone=85 band=85 tier=A ev=quest_complete route=est fb=band_table_off margin=15 cut=2 skip=3 planner_limit=- wait=0 "
                "cycles=0 relocate_src=unknown floor=ok hold=switch write=0 ms=1",
            "golden: the [Plan] line of the off path is the P-2b line");
}
}   // namespace

int main()
{
    PackRoundTrip();
    HearthAckedThenTaken();
    AckWithoutSite();
    SiteServesOneAck();
    OldSiteLost();
    NewestSiteWins();
    HopMark();
    StaleRules();
    WrapSafe();
    Relocations();
    Clock();
    Remaining();
    Xp();
    SwitchAndScope();
    Sink();
    Respawn();
    HopOneShot();
    HearthResyncDuringCast();
    ForcedOutsideBudget();
    ForcedLineCeiling();
    GrindAndOffMap();
    DeadHold();
    GhostRunThroughHold();
    PlanLineGolden();
    std::cout << "replan_wire_policy: " << g_checks << " checks passed\n";
    return 0;
}
