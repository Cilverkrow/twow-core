#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 R9 (re-plan on stall) and R2 (relocation), producer side: the per-bot progress meter, pure. Spec: ws-10 cli-pathfinding
// a3-step4-route/R9-REPLAN-INTERFACE-v5-20261107.md (sha 9575095d; v4 d44daade is normative in A3-BOT-LOGIK 4.3, v5 adds
// RelocationEvent.planned, A3-GERUEST-L60 E2). The meter measures counted time without progress per target key and asks the sink
// (the supervisor, which asks the planner) for a Keep / Replan / Close verdict; the planner decides, the meter only enforces the
// forced rules (keep_limit, second_stall, reloc_loop) and the hourly cap on planner-choice decisions. No game types, no config
// reads, no locks, no log calls; <cstdio> is only for the two line formatters. Nothing in the game calls this yet: R9-2 wires it
// into the planner tick behind its own switch (default 0) and a shadow key (R9-PREREG C2).
//
// planned = false everywhere gives exactly the v4 behaviour. Not here: the S4-3 rescue trigger and the rescue episode (v5 8.2
// conditions 1 and 2) come with the S4-3 pair; v5 8.2 condition 3 (Rescue relocations are not loop-counted) is here.
namespace ai::replan
{
enum class TargetKind : std::uint8_t { Objective = 0, Giver = 1, Taker = 2, Grind = 3, Rpg = 4, Profession = 5, Other = 6 };
enum class ClockState : std::uint8_t { Alive = 0, Combat = 1, Dead = 2, Taxi = 3, Transport = 4, Passive = 5, Rest = 6 };
enum class Decision : std::uint8_t { Keep = 0, Replan = 1, Close = 2 };
enum class Reason : std::uint8_t
{
    None = 0,
    LongRouteKnown = 1,
    WaitTransport = 2,
    GroupMember = 3,
    Escort = 4,
    RespawnWait = 5,
    KeepLimit = 6,
    SecondStall = 7,
    RateLimited = 8,
    PlannerChoice = 9,
    RelocLoop = 10,
};
// Unknown (0) = a position jump the detector saw without a source set by a teleport site (lastRelocSource default, P5 condition 1).
enum class RelocSource : std::uint8_t
{
    Unknown = 0,
    Hearth = 1,
    HomeTeleport = 2,
    Rescue = 3,
    TeleportAck = 4,
    FarMoveArrival = 5,
    Death = 6,
    Revive = 7,
};
enum class Trigger : std::uint8_t { Stall = 0, Relocate = 1 };

constexpr std::uint8_t NoLeg = 0xFF;   // RelocationEvent.legIndex when the route module confirmed no leg

inline char const* KindName(TargetKind k)
{
    switch (k)
    {
        case TargetKind::Objective: return "objective";
        case TargetKind::Giver: return "giver";
        case TargetKind::Taker: return "taker";
        case TargetKind::Grind: return "grind";
        case TargetKind::Rpg: return "rpg";
        case TargetKind::Profession: return "profession";
        case TargetKind::Other: return "other";
    }
    return "unknown";
}

inline char const* DecisionName(Decision d)
{
    switch (d)
    {
        case Decision::Keep: return "keep";
        case Decision::Replan: return "replan";
        case Decision::Close: return "close";
    }
    return "unknown";
}

inline char const* ReasonName(Reason r)
{
    switch (r)
    {
        case Reason::None: return "none";
        case Reason::LongRouteKnown: return "long_route_known";
        case Reason::WaitTransport: return "wait_transport";
        case Reason::GroupMember: return "group_member";
        case Reason::Escort: return "escort";
        case Reason::RespawnWait: return "respawn_wait";
        case Reason::KeepLimit: return "keep_limit";
        case Reason::SecondStall: return "second_stall";
        case Reason::RateLimited: return "rate_limited";
        case Reason::PlannerChoice: return "planner_choice";
        case Reason::RelocLoop: return "reloc_loop";
    }
    return "unknown";
}

inline char const* SourceName(RelocSource s)
{
    switch (s)
    {
        case RelocSource::Unknown: return "unknown";
        case RelocSource::Hearth: return "hearth";
        case RelocSource::HomeTeleport: return "home_teleport";
        case RelocSource::Rescue: return "rescue";
        case RelocSource::TeleportAck: return "teleport_ack";
        case RelocSource::FarMoveArrival: return "far_move_arrival";
        case RelocSource::Death: return "death";
        case RelocSource::Revive: return "revive";
    }
    return "unknown";
}

// A Keep is valid only with one of the planner's keep reasons (2.3, 6.3).
inline bool IsKeepReason(Reason r)
{
    return r == Reason::LongRouteKnown || r == Reason::WaitTransport || r == Reason::GroupMember || r == Reason::Escort ||
           r == Reason::RespawnWait;
}

inline bool IsForcedReason(Reason r)
{
    return r == Reason::KeepLimit || r == Reason::SecondStall || r == Reason::RelocLoop;
}

// Death, Revive and Rescue never count toward reloc_loop (7.2, v5 8.2 condition 3).
inline bool LoopCounted(RelocSource s)
{
    return s != RelocSource::Death && s != RelocSource::Revive && s != RelocSource::Rescue;
}

// Only a hearth or a teleport ack can be a planned leg, and only with a leg index the route module confirmed (v5 8.1).
inline bool EffectivePlanned(bool planned, std::uint8_t legIndex, RelocSource s)
{
    return planned && legIndex != NoLeg && (s == RelocSource::Hearth || s == RelocSource::TeleportAck);
}

// ---- parameters (the defaults are the spec's; R9-2 reads the keys) -------------------------------------------------------
struct Params
{
    std::uint32_t stallMs = 600000;            // N (5.2)
    float gainYd = 10.0f;                      // G (2.1)
    std::uint32_t restCapMs = 300000;          // RestCapS (2.1)
    std::uint32_t maxGapMs = 30000;            // max(2 x supervisor maxDelayMs, 30 s) (6.2); the caller computes it
    std::uint32_t maxReplansPerHour = 6;       // planner-choice cap (2.3, 7.3)
    std::uint32_t closeForS = 7200;            // Close default (2.3)
    std::uint32_t secondStallWindowMs = 7200000;   // the 2nd stall of a key within 2 h is a Close (2.3)
    std::uint32_t keepLimit = 2;               // at most 2 Keeps in a row per key (2.3, 7.2)
    std::uint32_t relocLoop = 3;               // the 3rd unplanned relocation without progress is forced (7.2)
    std::uint32_t plannedLoopRelocs = 4;       // the 4th planned relocation without credit is forced (v5 8.1)
};

// ---- target key: quest + target entry + cell (50 yd 2D + 10 yd z band) ---------------------------------------------------
constexpr float CellYd = 50.0f;
constexpr float CellZYd = 10.0f;

struct Cell
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

inline Cell CellOf(float x, float y, float z)
{
    Cell c;
    c.x = std::int32_t(std::floor(x / CellYd));
    c.y = std::int32_t(std::floor(y / CellYd));
    c.z = std::int32_t(std::floor(z / CellZYd));
    return c;
}

// FNV-1a 64 over the five 32-bit words; never 0 (0 = no target).
inline std::uint64_t TargetKey(std::uint32_t quest, std::int32_t entry, float x, float y, float z)
{
    Cell const c = CellOf(x, y, z);
    std::uint32_t const words[5] = {quest, std::uint32_t(entry), std::uint32_t(c.x), std::uint32_t(c.y), std::uint32_t(c.z)};
    std::uint64_t h = 1469598103934665603ull;
    for (std::uint32_t w : words)
        for (int b = 0; b < 4; ++b)
        {
            h ^= std::uint64_t((w >> (8 * b)) & 0xFFu);
            h *= 1099511628211ull;
        }
    return h == 0 ? 1 : h;
}

// Wrap-safe elapsed ms (the same modular difference as the core timer diff, spec 6.2).
inline std::uint32_t Elapsed(std::uint32_t from, std::uint32_t to)
{
    return to - from;
}

inline std::uint8_t Sat8(std::uint32_t v)
{
    return v > 255u ? std::uint8_t(255) : std::uint8_t(v);
}

inline std::uint16_t Sat16(std::uint32_t v)
{
    return v > 65535u ? std::uint16_t(65535) : std::uint16_t(v);
}

// ---- inputs and events ---------------------------------------------------------------------------------------------------
// One sample per supervisor tick (2.1, 6.1). Counters are cumulative per bot; the meter takes differences.
struct Sample
{
    std::uint32_t nowMs = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::uint64_t targetKey = 0;   // 0 = no target: the meter idles
    std::uint32_t quest = 0;
    std::int32_t targetEntry = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    TargetKind kind = TargetKind::Other;
    float remaining = 0.0f;        // route remaining when routeKnown, else straight 3D distance
    bool routeKnown = false;
    std::uint32_t creditSeq = 0;   // quest progress events of this quest (quest book)
    std::uint32_t xp = 0;          // GRIND: XP counter
    std::uint32_t kills = 0;       // GRIND: kill counter
    std::uint32_t skillUps = 0;    // PROFESSION: skill-ups + learned ranks
    std::uint32_t unreachable = 0, routePartial = 0, routeFails = 0;   // diagnosis counters (2.2)
    ClockState state = ClockState::Alive;
};

struct StallEvent
{
    std::uint32_t quest = 0;
    std::int32_t targetEntry = 0;
    std::uint32_t mapId = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    TargetKind kind = TargetKind::Other;
    std::uint32_t episodeAgeS = 0;
    std::uint32_t stallS = 0;
    float bestRemaining = 0.0f, curRemaining = 0.0f;
    bool routeKnown = false;
    std::uint64_t targetKey = 0;
    std::uint8_t stallsThisTarget = 0;   // incl. this one, last 2 h
    std::uint8_t keepsInRow = 0;         // across both triggers (7.2)
    std::uint32_t lastCreditAgeS = 0;    // 0 = no credit in this episode
    std::uint32_t xpGainInStall = 0;
    std::uint16_t unreachable = 0, routePartial = 0, routeFails = 0;
    std::uint16_t combatS = 0, deadS = 0;
    Reason forced = Reason::None;        // != None: Keep is not accepted (keep_limit) or the verdict is fixed (second_stall)
};

struct RelocationEvent
{
    // filled by the caller
    RelocSource source = RelocSource::Unknown;
    std::uint32_t fromMap = 0, toMap = 0;
    float fx = 0.0f, fy = 0.0f, fz = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::uint64_t targetKey = 0;   // the bot's target key at the arrival
    std::uint32_t atMs = 0;
    float remaining = 0.0f;        // remaining at the arrival (same measure as Sample.remaining)
    bool routeKnown = false;
    bool planned = false;          // set only by the route executor (v5 8.1)
    std::uint8_t legIndex = NoLeg;
    // filled by the meter before the sink sees it
    std::uint8_t relocsNoProgress = 0;
    std::uint8_t plannedNoProgress = 0;
    std::uint8_t keepsInRow = 0;
    bool plannedEffective = false;
    Reason forced = Reason::None;
};

struct Verdict
{
    Decision d = Decision::Keep;
    Reason why = Reason::None;
    std::uint32_t closeForS = 0;   // Close: 0 = Params.closeForS
};

struct IReplanSink
{
    virtual ~IReplanSink() = default;
    virtual Verdict OnStall(StallEvent const&) = 0;
    virtual Verdict OnRelocate(RelocationEvent const&) = 0;
};

// One decision, everything the [Replan] line needs.
struct Record
{
    Trigger trigger = Trigger::Stall;
    RelocSource source = RelocSource::Unknown;
    std::uint32_t episode = 0;          // C1(a): the first forced record per (bot, key, episode)
    std::uint64_t targetKey = 0;
    std::uint32_t quest = 0;
    std::int32_t targetEntry = 0;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    TargetKind kind = TargetKind::Other;
    std::uint32_t stallS = 0, ageS = 0;
    float bestRemaining = 0.0f, curRemaining = 0.0f;
    bool routeKnown = false;
    std::uint32_t creditAgeS = 0, xpGain = 0;
    std::uint32_t unreachable = 0, routePartial = 0, routeFails = 0, combatS = 0, deadS = 0;
    std::uint32_t stallsTarget = 0, keepsInRow = 0, relocsNoProgress = 0, plannedNoProgress = 0;
    bool planned = false;
    Verdict verdict;
    bool forced = false;
};

struct Outcome
{
    bool decided = false;
    Record rec;
};

// Per-minute counters for [ReplanMin]; the caller sums them per cohort.
struct Counters
{
    std::uint32_t stalls = 0, relocates = 0, keep = 0, replan = 0, close = 0, forced = 0, rateLimited = 0, gaps = 0,
                  plannedCredit = 0;
};

// ---- the meter (one per bot, held by the supervisor) ---------------------------------------------------------------------
class ProgressMeter
{
public:
    static constexpr std::size_t KeySlots = 8;     // per-key give-up history (2 h); a full table evicts the oldest last entry
    static constexpr std::size_t HourSlots = 32;   // Replan/Close times of the last hour (forced included)

    explicit ProgressMeter(Params const& p = Params()) : m_p(p) {}

    Params const& GetParams() const { return m_p; }
    std::uint64_t Key() const { return m_key; }
    std::uint32_t Episode() const { return m_episode; }
    std::uint32_t StallMs() const { return m_stallMs; }
    std::uint32_t KeepsInRow() const { return m_keepsInRow; }
    std::uint32_t RelocsNoProgress() const { return m_relocsNp; }
    std::uint32_t PlannedNoProgress() const { return m_plannedNp; }
    float BestRemaining() const { return m_best; }

    Counters TakeCounters()
    {
        Counters c = m_counters;
        m_counters = Counters();
        return c;
    }

    // Once per supervisor tick. Returns a decision when the counted stall time reached N.
    Outcome Feed(Sample const& s, IReplanSink& sink)
    {
        Outcome out;
        if (s.targetKey == 0)
        {
            ClearEpisode();   // no target: the meter idles
            return out;
        }
        if (s.targetKey != m_key)
            StartEpisode(s);
        else
            Account(s);
        m_hasLast = true;
        m_last = s;
        if (m_stallMs < m_p.stallMs)
            return out;

        ++m_counters.stalls;
        StallEvent ev;
        ev.quest = s.quest;
        ev.targetEntry = s.targetEntry;
        ev.mapId = s.mapId;
        ev.tx = s.tx;
        ev.ty = s.ty;
        ev.tz = s.tz;
        ev.kind = s.kind;
        ev.episodeAgeS = Elapsed(m_episodeStartMs, s.nowMs) / 1000u;
        ev.stallS = m_stallMs / 1000u;
        ev.bestRemaining = m_best;
        ev.curRemaining = s.remaining;
        ev.routeKnown = s.routeKnown;
        ev.targetKey = m_key;
        // second_stall (2.3): an earlier stall of this key within 2 h that ended in Replan or Close. A Keep-answered stall does
        // not count, so the Keep limit (2 in a row) stays reachable (7.5 case 7).
        std::uint32_t const prior = StallsInWindow(m_key, s.nowMs);
        std::uint32_t const stalls = prior + 1;
        ev.stallsThisTarget = Sat8(stalls);
        ev.keepsInRow = Sat8(m_keepsInRow);
        ev.lastCreditAgeS = m_hasCredit ? Elapsed(m_creditAtMs, s.nowMs) / 1000u : 0u;
        ev.xpGainInStall = s.xp - m_winXp;
        ev.unreachable = Sat16(s.unreachable - m_epUnreach);
        ev.routePartial = Sat16(s.routePartial - m_epPartial);
        ev.routeFails = Sat16(s.routeFails - m_epFails);
        ev.combatS = Sat16(m_winCombatMs / 1000u);
        ev.deadS = Sat16(m_winDeadMs / 1000u);
        if (prior >= 1)
            ev.forced = Reason::SecondStall;
        else if (m_keepsInRow >= m_p.keepLimit)
            ev.forced = Reason::KeepLimit;

        Verdict const v = Resolve(sink.OnStall(ev), ev.forced, s.nowMs);

        Record& r = out.rec;
        out.decided = true;
        r.trigger = Trigger::Stall;
        r.episode = m_episode;
        r.targetKey = m_key;
        r.quest = s.quest;
        r.targetEntry = s.targetEntry;
        r.tx = s.tx;
        r.ty = s.ty;
        r.tz = s.tz;
        r.kind = s.kind;
        r.stallS = ev.stallS;
        r.ageS = ev.episodeAgeS;
        r.bestRemaining = m_best;
        r.curRemaining = s.remaining;
        r.routeKnown = s.routeKnown;
        r.creditAgeS = ev.lastCreditAgeS;
        r.xpGain = ev.xpGainInStall;
        r.unreachable = ev.unreachable;
        r.routePartial = ev.routePartial;
        r.routeFails = ev.routeFails;
        r.combatS = ev.combatS;
        r.deadS = ev.deadS;
        r.stallsTarget = stalls;
        r.keepsInRow = m_keepsInRow;
        r.relocsNoProgress = m_relocsNp;
        r.plannedNoProgress = m_plannedNp;
        r.verdict = v;
        r.forced = ev.forced != Reason::None;
        Apply(v, s.nowMs, r);
        if (v.d != Decision::Keep)
            NoteGiveUp(m_key, s.nowMs);
        NewWindow(s);   // at most one stall trigger per N (7.3): the next window starts now
        return out;
    }

    // Every relocation goes to the sink and gives one decision (7.3: never silent).
    Outcome OnRelocate(RelocationEvent const& in, IReplanSink& sink)
    {
        Outcome out;
        ++m_counters.relocates;
        RelocationEvent ev = in;
        bool const planned = EffectivePlanned(in.planned, in.legIndex, in.source);
        ev.plannedEffective = planned;
        ev.forced = Reason::None;

        if (in.targetKey == 0 || in.targetKey != m_key)
        {
            // a key change ends the episode (7.1); the new key starts with zero counters
            ClearEpisode();
            if (in.targetKey != 0)
            {
                Sample s;
                s.nowMs = in.atMs;
                s.mapId = in.toMap;
                s.x = in.tx;
                s.y = in.ty;
                s.z = in.tz;
                s.targetKey = in.targetKey;
                s.remaining = in.remaining;
                s.routeKnown = in.routeKnown;
                s.state = ClockState::Passive;   // the counters of the new episode start at the next Feed
                StartEpisode(s);
                m_hasLast = true;
                m_last = s;
                m_needBaseline = true;
            }
        }
        else
        {
            bool credited = false;
            if (in.routeKnown != m_last.routeKnown)
                m_best = in.remaining;   // scale change: re-base without credit
            else if (planned && in.remaining <= m_best - m_p.gainYd)
            {
                credited = true;   // credit only against the best (v5 8.1 guard 1)
                m_best = in.remaining;
                ++m_counters.plannedCredit;
                Progress(in.atMs);
            }
            else if (in.remaining < m_best)
                m_best = in.remaining;   // 7.1: re-base without credit
            m_last.routeKnown = in.routeKnown;
            m_last.remaining = in.remaining;

            if (!credited && LoopCounted(in.source))
            {
                if (planned)
                    ++m_plannedNp;
                else
                    ++m_relocsNp;
            }
            if (!credited && LoopCounted(in.source) &&
                ((!planned && m_relocsNp >= m_p.relocLoop) || (planned && m_plannedNp >= m_p.plannedLoopRelocs)))
                ev.forced = Reason::RelocLoop;
            else if (m_keepsInRow >= m_p.keepLimit)
                ev.forced = Reason::KeepLimit;
        }
        ev.relocsNoProgress = Sat8(m_relocsNp);
        ev.plannedNoProgress = Sat8(m_plannedNp);
        ev.keepsInRow = Sat8(m_keepsInRow);

        Verdict const v = Resolve(sink.OnRelocate(ev), ev.forced, in.atMs);

        Record& r = out.rec;
        out.decided = true;
        r.trigger = Trigger::Relocate;
        r.source = in.source;
        r.episode = m_episode;
        r.targetKey = in.targetKey;
        if (m_key != 0)
        {
            r.quest = m_last.quest;
            r.targetEntry = m_last.targetEntry;
            r.tx = m_last.tx;
            r.ty = m_last.ty;
            r.tz = m_last.tz;
            r.kind = m_last.kind;
            r.ageS = Elapsed(m_episodeStartMs, in.atMs) / 1000u;
            r.creditAgeS = m_hasCredit ? Elapsed(m_creditAtMs, in.atMs) / 1000u : 0u;
        }
        r.stallS = m_stallMs / 1000u;
        r.bestRemaining = m_best;
        r.curRemaining = in.remaining;
        r.routeKnown = in.routeKnown;
        r.combatS = m_winCombatMs / 1000u;
        r.deadS = m_winDeadMs / 1000u;
        r.stallsTarget = StallsInWindow(in.targetKey, in.atMs);
        r.keepsInRow = m_keepsInRow;
        r.relocsNoProgress = m_relocsNp;
        r.plannedNoProgress = m_plannedNp;
        r.planned = planned;
        r.verdict = v;
        r.forced = ev.forced != Reason::None;
        Apply(v, in.atMs, r);
        // 7.1: the clock belongs to the key, not to the decision. A Replan/Close ends the episode only when the key changes
        // (at the next Feed); in shadow mode the key stays and the clock keeps running.
        return out;
    }

private:
    struct KeySlot
    {
        std::uint64_t key = 0;
        std::uint32_t stallMs[2] = {0, 0};   // the last two stalls of this key that ended in Replan or Close
        std::uint8_t n = 0;                  // valid entries in stallMs (0..2)
    };

    Params m_p;
    std::uint64_t m_key = 0;
    std::uint32_t m_episode = 0;
    std::uint32_t m_episodeStartMs = 0;
    bool m_hasLast = false;
    bool m_needBaseline = false;
    Sample m_last;
    float m_best = 0.0f;
    std::uint32_t m_stallMs = 0;
    std::uint32_t m_restMs = 0;
    std::uint32_t m_keepsInRow = 0, m_relocsNp = 0, m_plannedNp = 0;
    bool m_hasCredit = false;
    std::uint32_t m_creditAtMs = 0;
    std::uint32_t m_epUnreach = 0, m_epPartial = 0, m_epFails = 0;
    std::uint32_t m_winXp = 0, m_winCombatMs = 0, m_winDeadMs = 0;
    KeySlot m_keys[KeySlots];
    std::uint32_t m_hour[HourSlots] = {};
    std::size_t m_hourNext = 0, m_hourUsed = 0;
    Counters m_counters;

    // Ends the episode without starting one: no key, no clock, no per-episode counters (the per-key give-up history and the
    // hourly decisions stay).
    void ClearEpisode()
    {
        m_key = 0;
        m_hasLast = false;
        m_needBaseline = false;
        m_best = 0.0f;
        m_stallMs = 0;
        m_restMs = 0;
        m_keepsInRow = 0;
        m_relocsNp = 0;
        m_plannedNp = 0;
        m_hasCredit = false;
        m_winCombatMs = 0;
        m_winDeadMs = 0;
    }

    void StartEpisode(Sample const& s)
    {
        m_key = s.targetKey;
        ++m_episode;
        m_episodeStartMs = s.nowMs;
        m_best = s.remaining;
        m_stallMs = 0;
        m_restMs = 0;
        m_keepsInRow = 0;
        m_relocsNp = 0;
        m_plannedNp = 0;
        m_hasCredit = false;
        m_epUnreach = s.unreachable;
        m_epPartial = s.routePartial;
        m_epFails = s.routeFails;
        m_needBaseline = false;
        NewWindow(s);
    }

    void NewWindow(Sample const& s)
    {
        m_stallMs = 0;
        m_winXp = s.xp;
        m_winCombatMs = 0;
        m_winDeadMs = 0;
    }

    void Progress(std::uint32_t nowMs)
    {
        m_stallMs = 0;
        m_winCombatMs = 0;
        m_winDeadMs = 0;
        m_keepsInRow = 0;
        m_relocsNp = 0;
        m_plannedNp = 0;
        m_hasCredit = true;
        m_creditAtMs = nowMs;
    }

    // The interval since the last sample, in the state of the last sample (6.2).
    void Account(Sample const& s)
    {
        if (m_needBaseline)
        {
            // first Feed after a relocation started this key: take the counters as the episode's baseline, no time
            m_needBaseline = false;
            m_epUnreach = s.unreachable;
            m_epPartial = s.routePartial;
            m_epFails = s.routeFails;
            m_winXp = s.xp;
            if (s.routeKnown != m_last.routeKnown)
                m_best = s.remaining;
            return;   // Feed stores s as the last sample
        }
        std::uint32_t const dt = m_hasLast ? Elapsed(m_last.nowMs, s.nowMs) : 0u;
        if (dt > m_p.maxGapMs)
            ++m_counters.gaps;
        else
            Count(m_last.state, dt);

        bool progress = false;
        if (s.routeKnown != m_last.routeKnown)
            m_best = s.remaining;   // scale change: re-base without credit
        else
        {
            bool const routeKind = s.kind != TargetKind::Grind;
            if (routeKind && s.remaining <= m_best - m_p.gainYd)
            {
                progress = true;
                m_best = s.remaining;
            }
        }
        switch (s.kind)
        {
            case TargetKind::Objective:
            case TargetKind::Giver:
            case TargetKind::Taker:
                progress = progress || s.creditSeq != m_last.creditSeq;
                break;
            case TargetKind::Grind:
                progress = s.xp != m_last.xp || s.kills != m_last.kills;
                break;
            case TargetKind::Profession:
                progress = progress || s.skillUps != m_last.skillUps;
                break;
            default:
                break;
        }
        if (progress)
        {
            Progress(s.nowMs);
            m_winXp = s.xp;
        }
    }

    void Count(ClockState st, std::uint32_t dt)
    {
        switch (st)
        {
            case ClockState::Alive:
                m_stallMs += dt;
                break;
            case ClockState::Combat:
                m_stallMs += dt;
                m_winCombatMs += dt;
                break;
            case ClockState::Rest:
            {
                std::uint32_t const room = m_restMs < m_p.restCapMs ? m_p.restCapMs - m_restMs : 0u;
                std::uint32_t const frozen = dt < room ? dt : room;
                m_restMs += frozen;
                m_stallMs += dt - frozen;
                break;
            }
            case ClockState::Dead:
                m_winDeadMs += dt;
                break;
            default:
                break;   // Taxi, Transport, Passive: frozen
        }
    }

    // Records a stall of key at now that ended in Replan or Close.
    void NoteGiveUp(std::uint64_t key, std::uint32_t now)
    {
        KeySlot* slot = nullptr;
        for (KeySlot& k : m_keys)
            if (k.n != 0 && k.key == key)
                slot = &k;
        if (!slot)
        {
            // a free slot, else the slot with the oldest last entry (entries outside the 2 h window are the oldest)
            for (KeySlot& k : m_keys)
                if (k.n == 0)
                {
                    slot = &k;
                    break;
                }
            if (!slot)
            {
                std::uint32_t oldestAge = 0;
                for (KeySlot& k : m_keys)
                {
                    std::uint32_t const age = Elapsed(k.stallMs[k.n - 1], now);
                    if (!slot || age > oldestAge)
                    {
                        slot = &k;
                        oldestAge = age;
                    }
                }
            }
            *slot = KeySlot();
            slot->key = key;
        }
        if (slot->n == 2)
        {
            slot->stallMs[0] = slot->stallMs[1];
            slot->n = 1;
        }
        slot->stallMs[slot->n++] = now;
    }

    std::uint32_t StallsInWindow(std::uint64_t key, std::uint32_t now) const
    {
        for (KeySlot const& k : m_keys)
            if (k.n != 0 && k.key == key)
            {
                std::uint32_t c = 0;
                for (std::uint8_t i = 0; i < k.n; ++i)
                    if (Elapsed(k.stallMs[i], now) <= m_p.secondStallWindowMs)
                        ++c;
                return c;
            }
        return 0;
    }

    std::uint32_t DecisionsLastHour(std::uint32_t now) const
    {
        std::uint32_t c = 0;
        for (std::size_t i = 0; i < m_hourUsed; ++i)
            if (Elapsed(m_hour[i], now) < 3600000u)
                ++c;
        return c;
    }

    // The forced rules and the planner-choice cap (2.3, 7.2, 7.3).
    Verdict Resolve(Verdict v, Reason forced, std::uint32_t now) const
    {
        if (forced == Reason::SecondStall)
        {
            Verdict f;
            f.d = Decision::Close;
            f.why = Reason::SecondStall;
            f.closeForS = (v.d == Decision::Close && v.closeForS != 0) ? v.closeForS : m_p.closeForS;
            return f;
        }
        if (forced != Reason::None)
        {
            if (v.d == Decision::Keep)
                v.d = Decision::Replan;
            v.why = forced;
        }
        else if (v.d == Decision::Keep)
        {
            if (!IsKeepReason(v.why))
            {
                v.d = Decision::Replan;   // a Keep needs a keep reason
                v.why = Reason::PlannerChoice;
            }
        }
        else
            v.why = Reason::PlannerChoice;

        if (forced == Reason::None && v.d != Decision::Keep && DecisionsLastHour(now) >= m_p.maxReplansPerHour)
        {
            v.d = Decision::Keep;
            v.why = Reason::RateLimited;
        }
        if (v.d == Decision::Close && v.closeForS == 0)
            v.closeForS = m_p.closeForS;
        if (v.d != Decision::Close)
            v.closeForS = 0;
        return v;
    }

    void Apply(Verdict const& v, std::uint32_t now, Record& r)
    {
        if (r.forced)
            ++m_counters.forced;
        switch (v.d)
        {
            case Decision::Keep:
                ++m_counters.keep;
                if (v.why == Reason::RateLimited)
                    ++m_counters.rateLimited;
                ++m_keepsInRow;
                break;
            case Decision::Replan:
            case Decision::Close:
                if (v.d == Decision::Replan)
                    ++m_counters.replan;
                else
                    ++m_counters.close;
                m_keepsInRow = 0;
                m_hour[m_hourNext] = now;
                m_hourNext = (m_hourNext + 1) % HourSlots;
                if (m_hourUsed < HourSlots)
                    ++m_hourUsed;
                break;
        }
    }
};

// ---- lines (6.5, 7.4, v5 8.1; R9-PREREG C1/C2: shadow=, episode=, forced=) -----------------------------------------------
inline std::size_t SanitizeLine(char* buf, int written, std::size_t n)
{
    if (n == 0)
        return 0;
    std::size_t len = written < 0 ? 0 : std::size_t(written);
    if (len >= n)
        len = n - 1;
    buf[len] = '\0';
    for (std::size_t i = 0; i < len; ++i)
        if (buf[i] == '\n' || buf[i] == '\r')
            buf[i] = '_';
    return len;
}

struct LineCtx
{
    std::uint32_t guid = 0;
    char cohort = 'A';
    std::uint32_t arbiter = 0;
    bool shadow = false;
    std::uint32_t level = 0;
};

inline std::size_t FormatReplanLine(char* buf, std::size_t n, LineCtx const& c, Record const& r)
{
    if (n == 0)
        return 0;
    char trig[40];
    if (r.trigger == Trigger::Stall)
        std::snprintf(trig, sizeof(trig), "stall");
    else
        std::snprintf(trig, sizeof(trig), "relocate:%s", SourceName(r.source));
    Cell const cell = CellOf(r.tx, r.ty, r.tz);
    int const w = std::snprintf(buf, n,
        "[Replan] bot=%u cohort=%c arbiter=%u shadow=%u trigger=%s level=%u quest=%u target_entry=%d cell=%d:%d:%d kind=%s "
        "episode=%u stall_s=%u age_s=%u best_rem=%.0f cur_rem=%.0f route=%u credit_age_s=%u xp_gain=%u unreach=%u "
        "route_partial=%u route_fails=%u combat_s=%u dead_s=%u stalls_target=%u keeps_in_row=%u relocs_np=%u planned_np=%u "
        "planned=%u decision=%s reason=%s forced=%u close_s=%u",
        c.guid, c.cohort, c.arbiter, c.shadow ? 1u : 0u, trig, c.level, r.quest, r.targetEntry, cell.x, cell.y, cell.z,
        KindName(r.kind), r.episode, r.stallS, r.ageS, double(r.bestRemaining), double(r.curRemaining), r.routeKnown ? 1u : 0u,
        r.creditAgeS, r.xpGain, r.unreachable, r.routePartial, r.routeFails, r.combatS, r.deadS, r.stallsTarget, r.keepsInRow,
        r.relocsNoProgress, r.plannedNoProgress, r.planned ? 1u : 0u, DecisionName(r.verdict.d), ReasonName(r.verdict.why),
        r.forced ? 1u : 0u, r.verdict.closeForS);
    return SanitizeLine(buf, w, n);
}

inline std::size_t FormatReplanMinLine(char* buf, std::size_t n, char cohort, bool shadow, Counters const& k,
                                       std::uint32_t dropped)
{
    if (n == 0)
        return 0;
    int const w = std::snprintf(buf, n,
        "[ReplanMin] cohort=%c shadow=%u stalls=%u relocates=%u keep=%u replan=%u close=%u forced=%u rate_limited=%u gaps=%u "
        "planned_credit=%u dropped=%u",
        cohort, shadow ? 1u : 0u, k.stalls, k.relocates, k.keep, k.replan, k.close, k.forced, k.rateLimited, k.gaps,
        k.plannedCredit, dropped);
    return SanitizeLine(buf, w, n);
}
}   // namespace ai::replan
