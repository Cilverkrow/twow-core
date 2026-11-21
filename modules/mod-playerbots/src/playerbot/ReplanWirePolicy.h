#pragma once

#include "ReplanPolicy.h"
#include "CohortPolicy.h"

#include <cmath>
#include <cstdint>

// A3 R9 / R2 wiring pair R9-2 (twow-repo#575; R9-REPLAN-INTERFACE v5 9575095d, R9-PREREG C1/C2; design DESIGN-R9-2-20261120.md
// D1-D13): the pure rules between the planner tick (PlannerShadow.cpp) and the meter of ReplanPolicy.h. No game types, no config
// reads, no locks, no log calls. The game side keeps the relocation latch in PlayerbotAI (one atomic number) and passes plain
// numbers here; unit suite t/replan_wire_policy_tests.cpp.
namespace ai::replan_wire
{
constexpr std::uint32_t kSiteTtlMs = 15000;   // a teleport site until its ack (hearth cast 10 s, then the near ack of the holder)
constexpr std::uint32_t kAckTtlMs = 30000;    // an ack until the jump the planner tick sees (AI delay 1-5 s after the ack)
constexpr float kOffMapYd = 100000.0f;        // remaining while the target is on another map (no route estimate)

// ---- the relocation latch: ms << 16 | flags << 8 | source (0 = empty) ------------------------------------------------------
constexpr std::uint8_t FlagAcked = 1u << 0;   // a teleport ack confirmed the source
constexpr std::uint8_t FlagHop = 1u << 1;     // D6: a movement hop of a non-detailed bot, not a relocation

inline std::uint64_t Pack(ai::replan::RelocSource src, std::uint8_t flags, std::uint32_t ms)
{
    return (std::uint64_t(ms) << 16) | (std::uint64_t(flags) << 8) | std::uint64_t(std::uint8_t(src));
}

inline ai::replan::RelocSource SrcOf(std::uint64_t latch)
{
    return ai::replan::RelocSource(std::uint8_t(latch & 0xFFu));
}

inline std::uint8_t FlagsOf(std::uint64_t latch)
{
    return std::uint8_t((latch >> 8) & 0xFFu);
}

inline std::uint32_t MsOf(std::uint64_t latch)
{
    return std::uint32_t(latch >> 16);
}

inline bool Acked(std::uint64_t latch)
{
    return (FlagsOf(latch) & FlagAcked) != 0;
}

inline bool Hop(std::uint64_t latch)
{
    return (FlagsOf(latch) & FlagHop) != 0;
}

// The sources a teleport site sets (D12). Death and Revive come from the snapshot edges (D5), TeleportAck from the ack (D7).
inline bool IsSiteSource(ai::replan::RelocSource s)
{
    return s == ai::replan::RelocSource::Hearth || s == ai::replan::RelocSource::HomeTeleport ||
           s == ai::replan::RelocSource::Rescue || s == ai::replan::RelocSource::FarMoveArrival;
}

// Fresh: an acked entry for kAckTtlMs after its ack, an un-acked one for kSiteTtlMs after its site (wrap-safe).
inline bool Fresh(std::uint64_t latch, std::uint32_t now)
{
    if (!latch)
        return false;
    return ai::replan::Elapsed(MsOf(latch), now) <= (Acked(latch) ? kAckTtlMs : kSiteTtlMs);
}

inline bool Stale(std::uint64_t latch, std::uint32_t now)
{
    return latch != 0 && !Fresh(latch, now);
}

// A planner tick without a jump drops a stale entry, and an acked hop mark at once (OB-10 R-3: the mark is one-shot; the ack has
// moved the bot, so a tick without a jump means the hop was too short to count, and the mark must not hide a later jump).
inline bool DropAtTickEnd(std::uint64_t latch, std::uint32_t now)
{
    return Stale(latch, now) || (latch != 0 && Acked(latch) && Hop(latch));
}

// A teleport site: its source replaces whatever the latch holds (the newest site wins), not acked yet.
inline std::uint64_t MergeSite(std::uint64_t latch, ai::replan::RelocSource src, std::uint32_t now)
{
    (void)latch;
    return Pack(src, 0, now);
}

// D6: a movement hop marks the latch; the newest mark or site wins, as for a site.
inline std::uint64_t MergeHop(std::uint64_t latch, std::uint32_t now)
{
    (void)latch;
    return Pack(ai::replan::RelocSource::Unknown, FlagHop, now);
}

// The teleport ack (every bot teleport ends in PlayerbotAI::HandleTeleportAck). A fresh, un-acked site source stays and is acked
// (restamped); a fresh, un-acked hop mark becomes an acked TeleportAck with the mark; anything else (empty, stale, already acked)
// becomes an acked TeleportAck (D7). An ack never overwrites a fresh site source, and one site serves exactly one ack.
inline std::uint64_t MergeAck(std::uint64_t latch, std::uint32_t now)
{
    if (latch && !Acked(latch) && Fresh(latch, now))
    {
        if (Hop(latch))
            return Pack(ai::replan::RelocSource::TeleportAck, std::uint8_t(FlagAcked | FlagHop), now);
        if (IsSiteSource(SrcOf(latch)))
            return Pack(SrcOf(latch), FlagAcked, now);
    }
    return Pack(ai::replan::RelocSource::TeleportAck, FlagAcked, now);
}

// What the planner tick takes at a jump: the source and the hop mark while fresh. Otherwise Unknown: a jump outside the ack
// funnel (P5 condition 1).
struct Taken
{
    ai::replan::RelocSource source = ai::replan::RelocSource::Unknown;
    bool hop = false;
};

inline Taken Take(std::uint64_t latch, std::uint32_t now)
{
    Taken t;
    if (!Fresh(latch, now))
        return t;
    t.source = SrcOf(latch);
    t.hop = Hop(latch);
    return t;
}

// D5: the one relocation of a tick. The death edge, else the revive edge, else a jump (same-map relocation or map change) of a
// living bot that is no movement hop (D6). A jump while dead (repop, ghost moves) gives none: the clock is frozen while dead.
inline bool RelocationOf(bool died, bool revived, bool jumped, bool alive, Taken const& taken, ai::replan::RelocSource& out)
{
    if (died)
    {
        out = ai::replan::RelocSource::Death;
        return true;
    }
    if (revived)
    {
        out = ai::replan::RelocSource::Revive;
        return true;
    }
    if (!jumped || !alive || taken.hop)
        return false;
    out = taken.source;
    return true;
}

// ---- the sample ------------------------------------------------------------------------------------------------------------
// The clock state of a sample (v5 6.2), precedence Dead > Taxi > Transport > Passive > Combat > Rest > Alive. Passive comes from
// the facts (D8: no quest book scope, battleground, parked; as Supervisor::ArmEnforce), never from the supervisor mode.
inline ai::replan::ClockState ClockOf(bool alive, bool taxi, bool transport, bool passive, bool combat, bool resting)
{
    if (!alive)
        return ai::replan::ClockState::Dead;
    if (taxi)
        return ai::replan::ClockState::Taxi;
    if (transport)
        return ai::replan::ClockState::Transport;
    if (passive)
        return ai::replan::ClockState::Passive;
    if (combat)
        return ai::replan::ClockState::Combat;
    if (resting)
        return ai::replan::ClockState::Rest;
    return ai::replan::ClockState::Alive;
}

// The straight 3D distance to the target, or kOffMapYd while it is on another map.
inline float RemainingYd(bool sameMap, float dx, float dy, float dz)
{
    if (!sameMap)
        return kOffMapYd;
    float const d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 0.0f ? std::sqrt(d2) : 0.0f;
}

// The XP gained since the last sample (the caller sums it for Sample.xp). 0 on the first sample (lastLevel 0) and when the level
// went down; a level-up counts the XP of the new level, at least 1, so it always reads as progress.
inline std::uint32_t XpStep(std::uint32_t lastLevel, std::uint32_t lastXp, std::uint32_t level, std::uint32_t xp)
{
    if (!lastLevel || level < lastLevel)
        return 0;
    if (level > lastLevel)
        return xp ? xp : 1u;
    return xp > lastXp ? xp - lastXp : 0u;
}

// OB-10 R-5: no kill counter in the tree (D10), so a grind target is not fed: without kills a grind meter would stall silently
// where the XP stops (grey mobs, the level cap). The caller counts those ticks (grind_nokill=).
inline bool GrindNoKill(ai::replan::TargetKind kind)
{
    return kind == ai::replan::TargetKind::Grind;
}

// OB-10 R-6: a target on another map has no route estimate; its decisions carry offmap=1 and stay out of C1/C2.
inline bool OffMap(std::uint64_t targetKey, float remaining)
{
    return targetKey != 0 && remaining >= kOffMapYd;
}

// OB-10 R-2: the stall clock pauses from the death edge to the revive edge. Alive, `held` keeps the whole sample (key 0 included,
// which drops a held target). Dead with a held target, the sample takes that target, its distance and its progress counters: the
// Dead state freezes the clock and the frozen distance gives no credit for the ghost run; the revive relocation then re-bases
// without credit (the meter re-bases only downward, v5 7.1: after a revive farther away the walk back counts as stall time).
// Dead with nothing held (the meter made while dead, or the bot died without a fed target), the meter idles (key 0), so a dead
// sample never carries a live ghost distance.
inline void HoldWhileDead(ai::replan::Sample& s, ai::replan::Sample& held, bool alive)
{
    if (alive)
    {
        held = s;
        return;
    }
    if (!held.targetKey)
    {
        s.targetKey = 0;
        return;
    }
    s.targetKey = held.targetKey;
    s.quest = held.quest;
    s.targetEntry = held.targetEntry;
    s.tx = held.tx;
    s.ty = held.ty;
    s.tz = held.tz;
    s.kind = held.kind;
    s.remaining = held.remaining;
    s.routeKnown = held.routeKnown;
    s.creditSeq = held.creditSeq;
    s.skillUps = held.skillUps;
    s.kills = held.kills;
}

// ---- the [Replan] line budget (D11) ----------------------------------------------------------------------------------------
// Forced records are never dropped: every forced record is outside the 300 / min budget, not only the first of a (key, episode).
// Before P-3 no verdict acts (D1), so the key stays after a forced Close and a later stall of the same episode is forced again
// (second_stall). The bound per bot: at most one stall decision per N, plus one reloc_loop record per loop-counted relocation of
// the episode from the third on (the loop count resets only with progress or a new episode), so a forced line needs a teleport.
// The C2 (a) base (the first forced record per bot, key and episode) is then always in the log.
inline bool OutsideLineBudget(ai::replan::Record const& r)
{
    return r.forced;
}

// ---- switch and scope (D2, D4) ---------------------------------------------------------------------------------------------
// AiPlayerbot.Replan is a cohort mode (0 off, 1 all, 2 cohort B, 3 cohort A); AiPlayerbot.Replan.Shadow 1 runs the meter in
// shadow for every other bot. Only the quest book population holds a meter (roster bot, no real player master, not in a
// battleground, not parked).
inline bool MeterOn(std::uint32_t replanMode, std::uint32_t shadowKey, std::uint32_t guidLow, bool inScope)
{
    return inScope && (ai::cohort::Applies(replanMode, guidLow) || shadowKey == 1);
}

// The active arm (shadow=0 in the line). Before P-3 no verdict acts in either arm (D1).
inline bool Active(std::uint32_t replanMode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(replanMode, guidLow);
}

// ---- the sink (Supervisor::OnStall / OnRelocate) ---------------------------------------------------------------------------
struct PlannerView
{
    bool groupMember = false;   // the live target is a group copy, or the supervisor holds a group
    bool respawnWait = false;   // W8 waits at the pick for a respawn (respawn + 60 s, v5 2.3)
};

// The planner's answer in R9-2: Keep with a keep reason, else Replan(planner_choice). Escort, long_route_known and wait_transport
// need the route module and an escort fact, which the tree does not have yet.
inline ai::replan::Verdict PlannerVerdict(PlannerView const& view)
{
    ai::replan::Verdict v;
    if (view.groupMember)
    {
        v.d = ai::replan::Decision::Keep;
        v.why = ai::replan::Reason::GroupMember;
    }
    else if (view.respawnWait)
    {
        v.d = ai::replan::Decision::Keep;
        v.why = ai::replan::Reason::RespawnWait;
    }
    else
    {
        v.d = ai::replan::Decision::Replan;
        v.why = ai::replan::Reason::PlannerChoice;
    }
    return v;
}

// The W8 respawn wait as a keep reason: the planner waits at its objective pick, the live target is that pick, and the wait is
// not older than respawn + 60 s.
inline bool RespawnWait(std::uint32_t deadWaitSinceMs, bool pickHas, std::uint32_t pickDestKey, std::uint32_t liveDestKey,
                        std::uint32_t respawnSec, std::uint32_t nowMs)
{
    return deadWaitSinceMs && pickHas && pickDestKey && pickDestKey == liveDestKey &&
           ai::replan::Elapsed(deadWaitSinceMs, nowMs) <= (respawnSec + 60u) * 1000u;
}
}   // namespace ai::replan_wire
