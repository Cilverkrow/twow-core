#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3 planner P-2 (A3-S3-PLANER-ENTWURF.md v0.3 sections 3-5, 7 and 13; AiPlayerbot.Planner, default 0): the pure rules of
// the planner in the supervisor tick. The planner finds its own events by comparing a small snapshot of the bot per tick (P5 (a),
// the mailbox stays untouched), picks a tier (A quest work in the plan zone, B grind at the plan zone's hub, C the next band zone),
// and keeps its pick with a margin and a bind time. Numbers and ids only: no game types, no config reads, no locks, no log calls.
// The adapter (PlannerShadow.cpp) fills the snapshot and the candidates on the bot's map thread.
namespace ai::planner
{
// ---- snapshot and events (section 4) ---------------------------------------------------------------------------------------
constexpr std::size_t kQuestSlots = 20;

enum class QuestState : std::uint8_t
{
    None = 0,
    Incomplete = 1,
    Complete = 2,
    Failed = 3,
};

// The phase of the bot's own travel target, mapped by the adapter from the travel status.
enum class TravelPhase : std::uint8_t
{
    None = 0,      // no target, NONE / PREPARE / READY
    Travel = 1,
    Work = 2,
    Cooldown = 3,
    Expired = 4,
};

struct QuestSlot
{
    std::uint32_t id = 0;
    QuestState state = QuestState::None;
};

struct Snapshot
{
    bool valid = false;
    std::uint32_t atMs = 0;
    std::uint32_t level = 0;
    std::uint32_t map = 0;
    std::uint32_t zone = 0;
    bool alive = true;
    bool onTransport = false;   // taxi flight or transport: no relocate test
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float runSpeed = 7.0f;      // yd/s
    std::uint8_t mode = 0;      // supervisor Mode
    std::uint8_t lane = 0;      // supervisor lane owner
    TravelPhase travel = TravelPhase::None;
    std::uint32_t travelDest = 0;
    QuestSlot quests[kQuestSlots];
};

// One bit per event; several can fire in one tick.
enum Ev : std::uint32_t
{
    EvNone = 0,
    EvStart = 1u << 0,
    EvLevel = 1u << 1,
    EvQuestAccept = 1u << 2,
    EvQuestComplete = 1u << 3,
    EvQuestFail = 1u << 4,
    EvQuestReward = 1u << 5,
    EvQuestDrop = 1u << 6,
    EvQuestGone = 1u << 7,
    EvDied = 1u << 8,
    EvAlive = 1u << 9,
    EvMap = 1u << 10,
    EvZone = 1u << 11,
    EvMode = 1u << 12,
    EvLane = 1u << 13,
    EvArrive = 1u << 14,
    EvRelocate = 1u << 15,
    EvDeadTarget = 1u << 16,
    EvClosed = 1u << 17,
    EvTimer = 1u << 18,
};

// The name of the first event in this order (the ev= field); the rest are counted.
inline char const* EvName(std::uint32_t mask)
{
    struct Entry { std::uint32_t bit; char const* name; };
    static Entry const order[] = {
        {EvStart, "start"}, {EvAlive, "alive"}, {EvDied, "died"}, {EvMap, "map"}, {EvZone, "zone"}, {EvRelocate, "relocate"},
        {EvLevel, "level"}, {EvQuestReward, "quest_reward"}, {EvQuestDrop, "quest_drop"}, {EvQuestGone, "quest_gone"},
        {EvQuestComplete, "quest_complete"}, {EvQuestFail, "quest_fail"}, {EvQuestAccept, "quest_accept"},
        {EvDeadTarget, "dead_target"}, {EvClosed, "closed"}, {EvArrive, "arrive"}, {EvMode, "mode"}, {EvLane, "lane"},
        {EvTimer, "timer"},
    };
    for (Entry const& e : order)
        if (mask & e.bit)
            return e.name;
    return "-";
}

// Quests that left the log between two snapshots; the adapter labels each (LabelGone) with the core's reward status.
struct GoneList
{
    std::uint32_t ids[kQuestSlots] = {};
    std::size_t n = 0;
};

inline bool HasQuest(Snapshot const& s, std::uint32_t id)
{
    for (QuestSlot const& q : s.quests)
        if (q.id == id)
            return true;
    return false;
}

inline QuestState StateOf(Snapshot const& s, std::uint32_t id)
{
    for (QuestSlot const& q : s.quests)
        if (q.id == id)
            return q.state;
    return QuestState::None;
}

// A relocation: a 2D jump on the same map larger than max(minYards, run speed x elapsed x slack), never on a taxi or transport
// (CLI-PATH P5 condition 1, e22e741c). A map change is its own event.
inline bool Relocated(Snapshot const& a, Snapshot const& b, float minYards, float slack)
{
    if (!a.valid || !b.valid || a.map != b.map || a.onTransport || b.onTransport)
        return false;
    float const dx = b.x - a.x;
    float const dy = b.y - a.y;
    float const dt = b.atMs >= a.atMs ? float(b.atMs - a.atMs) / 1000.0f : 0.0f;
    float limit = (b.runSpeed > 0.0f ? b.runSpeed : 0.0f) * dt * slack;
    if (limit < minYards)
        limit = minYards;
    return dx * dx + dy * dy > limit * limit;
}

// The events from prev to cur. A first snapshot is EvStart only. Gone quests go to gone (labelled by the adapter).
inline std::uint32_t Diff(Snapshot const& prev, Snapshot const& cur, GoneList& gone, float relocMinYards, float relocSlack)
{
    gone.n = 0;
    if (!cur.valid)
        return EvNone;
    if (!prev.valid)
        return EvStart;
    std::uint32_t ev = EvNone;
    if (cur.level > prev.level)
        ev |= EvLevel;
    if (prev.alive && !cur.alive)
        ev |= EvDied;
    if (!prev.alive && cur.alive)
        ev |= EvAlive;
    if (cur.map != prev.map)
        ev |= EvMap;
    else if (cur.zone != prev.zone)
        ev |= EvZone;
    if (cur.mode != prev.mode)
        ev |= EvMode;
    if (cur.lane != prev.lane)
        ev |= EvLane;
    if (prev.travel == TravelPhase::Travel && (cur.travel == TravelPhase::Work || cur.travel == TravelPhase::Cooldown) &&
        cur.travelDest == prev.travelDest)
        ev |= EvArrive;
    if (Relocated(prev, cur, relocMinYards, relocSlack))
        ev |= EvRelocate;
    for (QuestSlot const& q : cur.quests)
    {
        if (!q.id)
            continue;
        QuestState const was = StateOf(prev, q.id);
        if (!HasQuest(prev, q.id))
            ev |= EvQuestAccept;
        if (q.state == QuestState::Complete && was != QuestState::Complete)
            ev |= EvQuestComplete;
        if (q.state == QuestState::Failed && was != QuestState::Failed)
            ev |= EvQuestFail;
    }
    for (QuestSlot const& q : prev.quests)
        if (q.id && !HasQuest(cur, q.id) && gone.n < kQuestSlots)
            gone.ids[gone.n++] = q.id;
    return ev;
}

// The label of a quest that left the log: the core keeps no reward status for repeatable quests (OB-10), so those are "gone",
// unless the bot's own reward note saw the turn-in (OB-10 B2: the #1078 hook feeds the planner's note too).
inline std::uint32_t LabelGone(bool repeatable, bool rewardStatus, bool rewardNoted)
{
    if (rewardNoted)
        return EvQuestReward;
    if (repeatable)
        return EvQuestGone;
    return rewardStatus ? EvQuestReward : EvQuestDrop;
}

// A plan is due on any event except a pending death (planned at the alive edge), or after maxIdleMs without one.
inline bool PlanDue(std::uint32_t ev, bool alive, std::uint32_t nowMs, std::uint32_t lastPlanMs, std::uint32_t maxIdleMs)
{
    if (!alive)
        return false;
    if (ev & ~std::uint32_t(EvDied))
        return true;
    return maxIdleMs && nowMs - lastPlanMs >= maxIdleMs;
}

// ---- tiers (section 3) -----------------------------------------------------------------------------------------------------
enum class Tier : std::uint8_t
{
    A = 0,   // turn-in, objective, pickup in the plan zone (turn-ins anywhere)
    B = 1,   // grind at the plan zone's hub
    C = 2,   // the next band zone
};

inline char const* TierName(Tier t)
{
    switch (t)
    {
        case Tier::A: return "A";
        case Tier::B: return "B";
        case Tier::C: return "C";
    }
    return "-";
}

enum class Fallback : std::uint8_t
{
    None = 0,
    R1Hold = 1,         // below StartZoneMinLevel: C is closed
    BandTableOff = 2,   // no band table: the plan zone is the current zone, C is closed
    Arbiter = 3,        // Planner = 2 but arbiter != 2: shadow this tick
    Cohort = 4,         // P-3a: Planner = 2 but the bot is not in Planner.WriteCohort: shadow
};

inline char const* FallbackName(Fallback f)
{
    switch (f)
    {
        case Fallback::None: return "-";
        case Fallback::R1Hold: return "r1_hold";
        case Fallback::BandTableOff: return "band_table_off";
        case Fallback::Arbiter: return "arbiter";
        case Fallback::Cohort: return "cohort";
    }
    return "-";
}

struct TierInput
{
    std::uint32_t eligibleA = 0;     // tier A candidates that pass every gate
    std::uint32_t waitingA = 0;      // tier A offers only held back for now (W8 wait, respawn, a close with an end)
    bool bandTable = false;
    bool r1Allows = false;           // R1 / start_zone lets the bot leave its zone
    bool nextZoneKnown = false;      // the band table has a next zone for the bot
};

struct TierPick
{
    Tier tier = Tier::B;
    Fallback fb = Fallback::None;
};

// "No candidate" never means waiting (REQ-LQ-13): A, else B while A is only held back, else C if allowed, else B at the hub.
inline TierPick PickTier(TierInput const& in)
{
    TierPick p;
    if (in.eligibleA)
    {
        p.tier = Tier::A;
        if (!in.bandTable)
            p.fb = Fallback::BandTableOff;
        return p;
    }
    if (in.waitingA)
    {
        p.tier = Tier::B;
        if (!in.bandTable)
            p.fb = Fallback::BandTableOff;
        return p;
    }
    if (!in.bandTable)
    {
        p.tier = Tier::B;
        p.fb = Fallback::BandTableOff;
        return p;
    }
    if (!in.r1Allows)
    {
        p.tier = Tier::B;
        p.fb = Fallback::R1Hold;
        return p;
    }
    p.tier = in.nextZoneKnown ? Tier::C : Tier::B;
    return p;
}

// ---- hysteresis (section 4) ------------------------------------------------------------------------------------------------
struct Hysteresis
{
    float marginPct = 15.0f;
    std::uint32_t minBindMs = 120000;
};

enum class Hold : std::uint8_t
{
    Switch = 0,     // take the new pick
    Same = 1,       // the new pick is the incumbent
    Margin = 2,     // keep: the new pick does not beat the incumbent by the margin
    Bind = 3,       // keep: the incumbent is younger than the bind time
    Forced = 4,     // switch without margin and bind (max_hold; P-3a: back_to_band sets its pick outside the hysteresis, V-1)
};

inline char const* HoldName(Hold h)
{
    switch (h)
    {
        case Hold::Switch: return "switch";
        case Hold::Same: return "same";
        case Hold::Margin: return "margin";
        case Hold::Bind: return "bind";
        case Hold::Forced: return "forced";
    }
    return "-";
}

struct Incumbent
{
    bool has = false;
    bool stillEligible = false;   // the incumbent passed every gate in this plan
    std::uint32_t key = 0;        // TargetKey of the pick
    float value = 0.0f;           // its value in this plan
    std::uint32_t sinceMs = 0;
};

inline Hold HoldDecision(Incumbent const& inc, std::uint32_t newKey, float newValue, bool forced, std::uint32_t nowMs, Hysteresis const& h)
{
    if (!inc.has || !inc.stillEligible)
        return Hold::Switch;
    if (newKey == inc.key)
        return Hold::Same;
    if (forced)
        return Hold::Forced;
    if (nowMs - inc.sinceMs < h.minBindMs)
        return Hold::Bind;
    float const margin = h.marginPct > 0.0f ? h.marginPct : 0.0f;
    float const bar = inc.value > 0.0f ? inc.value * (1.0f + margin / 100.0f) : inc.value;
    return newValue > bar ? Hold::Switch : Hold::Margin;
}

// The pre-check "cannot win": a candidate's value without danger is an upper bound of its value with danger, so one that cannot
// beat the bar without danger needs no danger query (skip=).
inline bool CannotWin(float valueNoDanger, Incumbent const& inc, Hysteresis const& h)
{
    if (!inc.has || !inc.stillEligible)
        return false;
    float const margin = h.marginPct > 0.0f ? h.marginPct : 0.0f;
    float const bar = inc.value > 0.0f ? inc.value * (1.0f + margin / 100.0f) : inc.value;
    return !(valueNoDanger > bar);
}

// One number per target: kind, quest and destination (stable across plans).
inline std::uint32_t TargetKey(std::uint8_t kind, std::uint32_t questId, std::uint32_t destId)
{
    std::uint32_t h = 2166136261u;
    std::uint32_t const parts[3] = {kind, questId, destId};
    for (std::uint32_t p : parts)
        for (int i = 0; i < 4; ++i)
        {
            h ^= (p >> (8 * i)) & 0xffu;
            h *= 16777619u;
        }
    return h ? h : 1u;
}

// ---- maximum holding time (BOT-16 point 2, QB4) ----------------------------------------------------------------------------
// A complete quest with a routable turn-in (canReward) wins without margin and bind once it is complete for maxHoldMs.
inline bool MaxHoldDue(std::uint32_t completeSinceMs, std::uint32_t nowMs, std::uint32_t maxHoldMs, bool canReward)
{
    return canReward && maxHoldMs && completeSinceMs && nowMs - completeSinceMs >= maxHoldMs;
}

// ---- repeatable quests (BOT-16 point 3) ------------------------------------------------------------------------------------
struct RepeatEntry
{
    std::uint32_t questId = 0;
    std::uint32_t lastRewardMs = 0;   // 0 = no turn-in seen
    std::uint32_t hourStartMs = 0;
    std::uint8_t takesThisHour = 0;
    std::uint32_t usedMs = 0;
};
constexpr std::size_t kRepeatSlots = 8;
constexpr std::uint32_t kHourMs = 3600000u;

inline RepeatEntry* FindRepeat(RepeatEntry* log, std::size_t n, std::uint32_t questId)
{
    for (std::size_t i = 0; i < n; ++i)
        if (log[i].questId == questId)
            return &log[i];
    return nullptr;
}

inline RepeatEntry& RepeatSlot(RepeatEntry* log, std::size_t n, std::uint32_t questId)
{
    if (RepeatEntry* e = FindRepeat(log, n, questId))
        return *e;
    std::size_t oldest = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (!log[i].questId)
        {
            oldest = i;
            break;
        }
        if (log[i].usedMs < log[oldest].usedMs)
            oldest = i;
    }
    log[oldest] = RepeatEntry{};
    log[oldest].questId = questId;
    return log[oldest];
}

inline void NoteRepeatReward(RepeatEntry* log, std::size_t n, std::uint32_t questId, std::uint32_t nowMs)
{
    RepeatEntry& e = RepeatSlot(log, n, questId);
    e.lastRewardMs = nowMs ? nowMs : 1u;
    e.usedMs = nowMs;
}

inline void NoteRepeatAccept(RepeatEntry* log, std::size_t n, std::uint32_t questId, std::uint32_t nowMs)
{
    RepeatEntry& e = RepeatSlot(log, n, questId);
    if (!e.hourStartMs || nowMs - e.hourStartMs >= kHourMs)
    {
        e.hourStartMs = nowMs ? nowMs : 1u;
        e.takesThisHour = 0;
    }
    if (e.takesThisHour < 255)
        ++e.takesThisHour;
    e.usedMs = nowMs;
}

// A repeatable quest may be taken again only retakeMs after its last turn-in and at most maxPerHour times per bot hour
// (blockedReason repeat_cap).
inline bool RepeatBlocked(RepeatEntry const* e, std::uint32_t nowMs, std::uint32_t retakeMs, std::uint32_t maxPerHour)
{
    if (!e)
        return false;
    if (e->lastRewardMs && nowMs - e->lastRewardMs < retakeMs)
        return true;
    bool const sameHour = e->hourStartMs && nowMs - e->hourStartMs < kHourMs;
    return maxPerHour && sameHour && e->takesThisHour >= maxPerHour;
}

// ---- zone level floor and the way back (OB-00 P6, CLI-PATH 0d72cc0b) --------------------------------------------------------
enum class FloorVerdict : std::uint8_t
{
    Ok = 0,
    Unknown = 1,   // the zone has no band table row: counted, not blocked
    Below = 2,     // the bot is below the zone's floor
};

inline char const* FloorName(FloorVerdict v)
{
    switch (v)
    {
        case FloorVerdict::Ok: return "ok";
        case FloorVerdict::Unknown: return "unknown";
        case FloorVerdict::Below: return "below";
    }
    return "-";
}

// floor = the lowest min_level of the band table rows of the zone (0 = no row).
inline FloorVerdict FloorCheck(std::uint32_t floor, std::uint32_t level)
{
    if (!floor)
        return FloorVerdict::Unknown;
    return level < floor ? FloorVerdict::Below : FloorVerdict::Ok;
}

// The way back to the bot's band: the travel choice found nothing with an empty quest log, and the bot is below the floor of
// its zone or its zone is not its plan zone.
inline bool BackToBandDue(bool noDestination, std::uint32_t questsInLog, FloorVerdict here, bool inPlanZone, bool hasPlanZone)
{
    if (!noDestination || questsInLog || !hasPlanZone)
        return false;
    return here == FloorVerdict::Below || !inPlanZone;
}

// Loop guard (OB-10 B4): at most one back_to_band per bot and bind time; a second inside it is back_to_band_repeat.
inline bool BackToBandAllowed(std::uint32_t lastBackMs, std::uint32_t nowMs, std::uint32_t minBindMs)
{
    return !lastBackMs || nowMs - lastBackMs >= minBindMs;
}

// The WORK lock and its break (OB-10 B3): a plan change never expires a target in COOLDOWN; in WORK only max_hold and back_to_band
// may (expire_work_override). Every other phase may expire. P-3a (OB-10 V-1): back_to_band never reaches the expire (its pick is
// held by Bind), so in the write path only max_hold breaks WORK.
enum class ExpireGate : std::uint8_t
{
    Allowed = 0,
    Cooldown = 1,
    Work = 2,
    WorkOverride = 3,
};

inline ExpireGate ExpireCheck(TravelPhase phase, bool forcedReason)
{
    if (phase == TravelPhase::Cooldown)
        return ExpireGate::Cooldown;
    if (phase == TravelPhase::Work)
        return forcedReason ? ExpireGate::WorkOverride : ExpireGate::Work;
    return ExpireGate::Allowed;
}

// ---- shadow comparison after SetBestTarget (OB-10 B1) ----------------------------------------------------------------------
enum class Compare : std::uint8_t
{
    Agree = 0,
    Disagree = 1,
    NaPurpose = 2,   // the search was not for quest or grind
    NaStale = 3,     // no fresh plan pick (older than the bind time, or none)
};

inline Compare ComparePick(bool questOrGrindPurpose, bool hasPick, std::uint32_t pickAtMs, std::uint32_t nowMs,
    std::uint32_t freshMs, std::uint32_t pickQuest, std::uint32_t pickDest, std::uint32_t chosenQuest, std::uint32_t chosenDest)
{
    if (!questOrGrindPurpose)
        return Compare::NaPurpose;
    if (!hasPick || nowMs - pickAtMs >= freshMs)
        return Compare::NaStale;
    return pickQuest == chosenQuest && pickDest == chosenDest ? Compare::Agree : Compare::Disagree;
}

// ---- W8: dead target (section 5) -------------------------------------------------------------------------------------------
inline std::uint32_t DeadWaitMs(std::uint32_t respawnSec, std::uint32_t deadWaitMaxSec)
{
    std::uint64_t const two = std::uint64_t(respawnSec) * 2u;
    std::uint64_t const sec = two < deadWaitMaxSec ? two : deadWaitMaxSec;
    return std::uint32_t(sec * 1000u);
}

enum class DeadStep : std::uint8_t
{
    Alive = 0,    // the probe found a living target: no wait
    Wait = 1,     // nothing living yet: grind nearby and wait
    Close = 2,    // the wait is over: close the target
};

inline DeadStep DeadProbe(std::uint32_t living, std::uint32_t waitSinceMs, std::uint32_t nowMs, std::uint32_t waitMs)
{
    if (living)
        return DeadStep::Alive;
    if (waitSinceMs && nowMs - waitSinceMs >= waitMs)
        return DeadStep::Close;
    return DeadStep::Wait;
}

// ---- target memory (section 5, 4.5 (8)) ------------------------------------------------------------------------------------
enum class CloseReason : std::uint8_t
{
    None = 0,
    Dead = 1,
    R9 = 2,
    Suppressed = 3,
};

inline char const* CloseReasonName(CloseReason r)
{
    switch (r)
    {
        case CloseReason::None: return "none";
        case CloseReason::Dead: return "dead";
        case CloseReason::R9: return "r9";
        case CloseReason::Suppressed: return "suppressed";
    }
    return "-";
}

// Every close has an end and a reason, never permanent (CLI-Deep 19.11.: permanent only through QB5 or a QB2 mark).
struct MemorySlot
{
    std::uint32_t key = 0;   // TargetKey
    std::uint32_t questId = 0;
    std::uint8_t kind = 0;
    CloseReason reason = CloseReason::None;
    std::uint8_t deadCycles = 0;
    std::uint32_t closedAtMs = 0;
    std::uint32_t closedUntilMs = 0;
    std::uint16_t closes = 0;   // P-3a (P3-ENTWURF-v0.2 section 5.3): closes of this key in this slot, saturating at 65535
};
// P-3a (P3-ENTWURF-v0.2 section 5.3, OB-00 F4 (a)): the field has 16 slots (the book's rules). A writing bot (Planner = 2 with
// Planner.WriteCohort) uses all 16 and saturates deadCycles (N1); the shadow (Planner = 1, a bot that does not write) uses the
// first kShadowMemorySlots with the old 255 -> 0 reset, so its picks and lines stay byte-identical to P-2b.
constexpr std::size_t kMemorySlots = 16;
constexpr std::size_t kShadowMemorySlots = 8;

inline bool Expired(MemorySlot const& m, std::uint32_t nowMs)
{
    return !m.key || static_cast<std::int32_t>(nowMs - m.closedUntilMs) >= 0;
}

inline bool IsClosed(MemorySlot const* mem, std::size_t n, std::uint32_t key, std::uint32_t nowMs)
{
    for (std::size_t i = 0; i < n; ++i)
        if (mem[i].key == key && !Expired(mem[i], nowMs))
            return true;
    return false;
}

// The replacement rule: the same key, else the first expired slot in slot order (B19: the first, not the oldest; P-3a fixed this
// comment, the rule is unchanged), else the oldest slot. closeMs 0 is not a close.
// P-3a (P3-ENTWURF-v0.2 section 5.4): saturate (a writing bot) keeps deadCycles at 255 ("always dead", N1, as the book) instead of
// the shadow's 255 -> 0 reset; evicted (when given) is set when a slot of another key that had not expired is replaced (N2,
// mem_evict: its counts are lost). closes counts the closes of the slot's key.
inline void Close(MemorySlot* mem, std::size_t n, std::uint32_t key, std::uint32_t questId, std::uint8_t kind, CloseReason reason,
    std::uint32_t nowMs, std::uint32_t closeMs, bool saturate = false, bool* evicted = nullptr)
{
    if (evicted)
        *evicted = false;
    if (!mem || !n || !key || !closeMs || reason == CloseReason::None)
        return;
    std::size_t at = n;
    for (std::size_t i = 0; i < n && at == n; ++i)
        if (mem[i].key == key)
            at = i;
    for (std::size_t i = 0; i < n && at == n; ++i)
        if (Expired(mem[i], nowMs))
            at = i;
    if (at == n)
    {
        at = 0;
        for (std::size_t i = 1; i < n; ++i)
            if (static_cast<std::int32_t>(mem[i].closedAtMs - mem[at].closedAtMs) < 0)
                at = i;
        if (evicted)
            *evicted = true;   // N2: every slot holds another key that has not expired
    }
    bool const same = mem[at].key == key;
    std::uint8_t const cycles = same && (saturate || mem[at].deadCycles < 255) ? mem[at].deadCycles : 0;
    std::uint16_t const closes = same ? mem[at].closes : 0;
    mem[at].key = key;
    mem[at].questId = questId;
    mem[at].kind = kind;
    mem[at].reason = reason;
    if (reason != CloseReason::Dead)
        mem[at].deadCycles = cycles;
    else
        mem[at].deadCycles = saturate && cycles == 255 ? cycles : std::uint8_t(cycles + 1);
    mem[at].closedAtMs = nowMs;
    mem[at].closedUntilMs = nowMs + closeMs;
    mem[at].closes = closes < 65535 ? std::uint16_t(closes + 1) : closes;
}

// The memory's reads (section 5.3 PlanMemory: Close / IsClosed / DeadCycles / Closes): 0 for a key it does not hold.
inline std::uint8_t DeadCycles(MemorySlot const* mem, std::size_t n, std::uint32_t key)
{
    for (std::size_t i = 0; i < n; ++i)
        if (key && mem[i].key == key)
            return mem[i].deadCycles;
    return 0;
}

inline std::uint16_t Closes(MemorySlot const* mem, std::size_t n, std::uint32_t key)
{
    for (std::size_t i = 0; i < n; ++i)
        if (key && mem[i].key == key)
            return mem[i].closes;
    return 0;
}

// ---- the minute line (section 7) -------------------------------------------------------------------------------------------
// Increments per cohort and minute. P-2 counts the shadow only: no expired / applied / fallback (those come with P-3).
struct PlanMinCounts
{
    std::uint32_t plans = 0;
    std::uint32_t keeps = 0;
    std::uint32_t switches = 0;
    std::uint32_t tierA = 0;
    std::uint32_t tierB = 0;
    std::uint32_t tierC = 0;
    std::uint32_t deadWait = 0;
    std::uint32_t deadClose = 0;
    std::uint32_t zoneChange = 0;
    std::uint32_t zoneChangeDead = 0;
    std::uint32_t cut = 0;
    std::uint32_t skip = 0;
    std::uint32_t timer = 0;
    std::uint32_t overBudget = 0;
    std::uint32_t overwritten = 0;
    std::uint32_t overwrittenGroup = 0;
    std::uint32_t overwrittenNeutral = 0;   // P-3 (section 13.2); 0 in the shadow
    std::uint32_t switchMsMax = 0;          // P-3 (section 13.1): plan switch to the expiry, the slowest; 0 in the shadow
    std::uint32_t agree = 0;
    std::uint32_t disagree = 0;
    std::uint32_t r1Mismatch = 0;
    std::uint32_t maxHold = 0;
    std::uint32_t repeatCap = 0;
    std::uint32_t backToBand = 0;
    std::uint32_t backToBandWould = 0;
    std::uint32_t backToBandRepeat = 0;
    std::uint32_t expireWorkOverrideWould = 0;
    std::uint32_t overwrittenShadow = 0;
    std::uint32_t naPurpose = 0;
    std::uint32_t naStale = 0;
    std::uint32_t floorBelow = 0;
    std::uint32_t floorUnknown = 0;
    std::uint32_t relocate = 0;
    std::uint32_t relocUnknown = 0;
    std::uint32_t planDropped = 0;
    std::uint32_t msP50 = 0;
    std::uint32_t msMax = 0;
    // P-2b (P2-BUILD-LOG B6, B7, section 14 B4): ticks in PASSIVE (the planner plans only in QUEST or GRIND, so a shadow without
    // Arbiter 2 shows 0 plans and these ticks), the grind comparison by zone (a planner hub never matches a mob target by entry),
    // and back_to_band_would by the purpose of the failed choice (quest, grind; the rest = total - quest - grind).
    std::uint32_t passiveTicks = 0;
    std::uint32_t agreeGrind = 0;
    std::uint32_t disagreeGrind = 0;
    std::uint32_t backToBandWouldQuest = 0;
    std::uint32_t backToBandWouldGrind = 0;
    // P-3a (P3-ENTWURF-v0.2 section 6.1, Nachtrag 1 F8 (ii)): the write path; printed only at Planner = 2 (FormatPlanMin p3).
    std::uint32_t expired = 0;
    std::uint32_t expireDenied = 0;   // the sum of the five below
    std::uint32_t expireDeniedCooldown = 0;
    std::uint32_t expireDeniedWork = 0;
    std::uint32_t expireDeniedLane = 0;
    std::uint32_t expireDeniedGroup = 0;
    std::uint32_t expireDeniedThread = 0;
    std::uint32_t expireDeniedZoneUnknown = 0;
    std::uint32_t expireWorkOverride = 0;
    std::uint32_t expireRepeat = 0;
    std::uint32_t expireBlockedFb = 0;
    std::uint32_t expirePending = 0;   // bots with a pending expire, once per bot and minute
    std::uint32_t expireAfterWork = 0;
    std::uint32_t fed = 0;             // plan attempts SetBestTarget took (applied + denied + parked + early returns after it)
    std::uint32_t appliedQuest = 0;
    std::uint32_t appliedGrind = 0;
    std::uint32_t appliedLiveQuest = 0;
    std::uint32_t appliedLiveGrind = 0;
    std::uint32_t fallbackReject = 0;
    std::uint32_t fallbackStale = 0;
    std::uint32_t fallbackNone = 0;
    std::uint32_t fallbackPurpose = 0;
    std::uint32_t fallbackBudget = 0;
    std::uint32_t fallbackDenied = 0;
    std::uint32_t fallbackParked = 0;
    std::uint32_t fallbackZoneEmpty = 0;
    std::uint32_t fbArbiter = 0;
    std::uint32_t fbCohort = 0;
    std::uint32_t turninUnreached = 0;
    std::uint32_t memEvict = 0;
    std::uint32_t modeQuestGrind = 0;
    std::uint32_t modeGrindQuest = 0;
    std::uint32_t modeDenied = 0;
    // twow-repo#726 (OB-60 review 9f5669b7, OB-10 5a445aa4 + Nachtrag 1 1e3b50d1), printed after mode_denied at Planner = 2 only:
    // F3 the quest pick's target not in the choice's list (non-locking, disjoint from fallback_reject); a grind pick's zone empty
    // again for the same pick (non-locking, disjoint from fallback_zone_empty, which counts the first one per pick); F1 the
    // FeedTakes filter loop's microseconds, the sum and the slowest choice of the minute.
    std::uint32_t fallbackRejectEmpty = 0;
    std::uint32_t zoneEmptyHeld = 0;
    std::uint32_t feedUs = 0;
    std::uint32_t feedUsMax = 0;
};

// The [PlanMin] buffer of the minute emitter: every field at 4294967295 with the P-3a fields fits (the unit test checks it).
constexpr std::size_t kPlanMinBuf = 2560;

// msP50 is not added: a median is not additive. The minute aggregator (PlannerShadow.cpp) takes it from the plan times of the
// minute when the minute rolls; msMax is the maximum.
inline void Add(PlanMinCounts& into, PlanMinCounts const& c)
{
    into.plans += c.plans;
    into.keeps += c.keeps;
    into.switches += c.switches;
    into.tierA += c.tierA;
    into.tierB += c.tierB;
    into.tierC += c.tierC;
    into.deadWait += c.deadWait;
    into.deadClose += c.deadClose;
    into.zoneChange += c.zoneChange;
    into.zoneChangeDead += c.zoneChangeDead;
    into.cut += c.cut;
    into.skip += c.skip;
    into.timer += c.timer;
    into.overBudget += c.overBudget;
    into.overwritten += c.overwritten;
    into.overwrittenGroup += c.overwrittenGroup;
    into.overwrittenNeutral += c.overwrittenNeutral;
    if (c.switchMsMax > into.switchMsMax)
        into.switchMsMax = c.switchMsMax;
    into.agree += c.agree;
    into.disagree += c.disagree;
    into.r1Mismatch += c.r1Mismatch;
    into.maxHold += c.maxHold;
    into.repeatCap += c.repeatCap;
    into.backToBand += c.backToBand;
    into.backToBandWould += c.backToBandWould;
    into.backToBandRepeat += c.backToBandRepeat;
    into.expireWorkOverrideWould += c.expireWorkOverrideWould;
    into.overwrittenShadow += c.overwrittenShadow;
    into.naPurpose += c.naPurpose;
    into.naStale += c.naStale;
    into.floorBelow += c.floorBelow;
    into.floorUnknown += c.floorUnknown;
    into.relocate += c.relocate;
    into.relocUnknown += c.relocUnknown;
    into.planDropped += c.planDropped;
    into.passiveTicks += c.passiveTicks;
    into.agreeGrind += c.agreeGrind;
    into.disagreeGrind += c.disagreeGrind;
    into.backToBandWouldQuest += c.backToBandWouldQuest;
    into.backToBandWouldGrind += c.backToBandWouldGrind;
    if (c.msMax > into.msMax)
        into.msMax = c.msMax;
    into.expired += c.expired;
    into.expireDenied += c.expireDenied;
    into.expireDeniedCooldown += c.expireDeniedCooldown;
    into.expireDeniedWork += c.expireDeniedWork;
    into.expireDeniedLane += c.expireDeniedLane;
    into.expireDeniedGroup += c.expireDeniedGroup;
    into.expireDeniedThread += c.expireDeniedThread;
    into.expireDeniedZoneUnknown += c.expireDeniedZoneUnknown;
    into.expireWorkOverride += c.expireWorkOverride;
    into.expireRepeat += c.expireRepeat;
    into.expireBlockedFb += c.expireBlockedFb;
    into.expirePending += c.expirePending;
    into.expireAfterWork += c.expireAfterWork;
    into.fed += c.fed;
    into.appliedQuest += c.appliedQuest;
    into.appliedGrind += c.appliedGrind;
    into.appliedLiveQuest += c.appliedLiveQuest;
    into.appliedLiveGrind += c.appliedLiveGrind;
    into.fallbackReject += c.fallbackReject;
    into.fallbackStale += c.fallbackStale;
    into.fallbackNone += c.fallbackNone;
    into.fallbackPurpose += c.fallbackPurpose;
    into.fallbackBudget += c.fallbackBudget;
    into.fallbackDenied += c.fallbackDenied;
    into.fallbackParked += c.fallbackParked;
    into.fallbackZoneEmpty += c.fallbackZoneEmpty;
    into.fbArbiter += c.fbArbiter;
    into.fbCohort += c.fbCohort;
    into.turninUnreached += c.turninUnreached;
    into.memEvict += c.memEvict;
    into.modeQuestGrind += c.modeQuestGrind;
    into.modeGrindQuest += c.modeGrindQuest;
    into.modeDenied += c.modeDenied;
    into.fallbackRejectEmpty += c.fallbackRejectEmpty;
    into.zoneEmptyHeld += c.zoneEmptyHeld;
    into.feedUs += c.feedUs;
    if (c.feedUsMax > into.feedUsMax)
        into.feedUsMax = c.feedUsMax;
}

// p3 (AiPlayerbot.Planner = 2, the key value only, never the cohort; section 7.5): the P-3a fields after the P-2b ones. Without
// p3 the line is byte-identical to P-2b.
inline std::size_t FormatPlanMin(char* buf, std::size_t n, char cohort, std::uint32_t minute, PlanMinCounts const& c, bool p3 = false)
{
    if (!buf || n == 0)
        return 0;
    int const w = std::snprintf(buf, n, "[PlanMin] cohort=%c min=%u plans=%u keeps=%u switches=%u tierA=%u tierB=%u tierC=%u "
        "dead_wait=%u dead_close=%u zone_change=%u zone_change_dead=%u cut=%u skip=%u timer=%u over_budget=%u overwritten=%u "
        "overwritten_group=%u agree=%u disagree=%u r1_mismatch=%u max_hold=%u repeat_cap=%u back_to_band=%u floor_below=%u "
        "floor_unknown=%u relocate=%u reloc_unknown=%u plan_dropped=%u ms_p50=%u ms_max=%u back_to_band_would=%u "
        "back_to_band_repeat=%u expire_work_override_would=%u overwritten_shadow=%u na_purpose=%u na_stale=%u overwritten_neutral=%u switch_ms=%u "
        "passive_ticks=%u agree_grind=%u disagree_grind=%u back_to_band_would_quest=%u back_to_band_would_grind=%u",
        cohort, minute, c.plans, c.keeps, c.switches, c.tierA, c.tierB, c.tierC, c.deadWait, c.deadClose, c.zoneChange,
        c.zoneChangeDead, c.cut, c.skip, c.timer, c.overBudget, c.overwritten, c.overwrittenGroup, c.agree, c.disagree,
        c.r1Mismatch, c.maxHold, c.repeatCap, c.backToBand, c.floorBelow, c.floorUnknown, c.relocate, c.relocUnknown,
        c.planDropped, c.msP50, c.msMax, c.backToBandWould, c.backToBandRepeat, c.expireWorkOverrideWould,
        c.overwrittenShadow, c.naPurpose, c.naStale, c.overwrittenNeutral, c.switchMsMax, c.passiveTicks, c.agreeGrind,
        c.disagreeGrind, c.backToBandWouldQuest, c.backToBandWouldGrind);
    if (w < 0)
        return 0;
    std::size_t len = std::size_t(w) < n ? std::size_t(w) : n - 1;
    if (!p3 || len + 1 >= n)
        return len;
    int const t = std::snprintf(buf + len, n - len, " expired=%u expire_denied=%u expire_denied_cooldown=%u expire_denied_work=%u "
        "expire_denied_lane=%u expire_denied_group=%u expire_denied_thread=%u expire_denied_zone_unknown=%u expire_work_override=%u "
        "expire_repeat=%u expire_blocked_fb=%u expire_pending=%u expire_after_work=%u fed=%u applied_quest=%u applied_grind=%u "
        "applied_live_quest=%u applied_live_grind=%u fallback_reject=%u fallback_stale=%u fallback_none=%u fallback_purpose=%u "
        "fallback_budget=%u fallback_denied=%u fallback_parked=%u fallback_zone_empty=%u fb_arbiter=%u fb_cohort=%u "
        "turnin_unreached=%u mem_evict=%u mode_quest_grind=%u mode_grind_quest=%u mode_denied=%u"
        " fallback_reject_empty=%u zone_empty_held=%u feed_us=%u feed_us_max=%u",
        c.expired, c.expireDenied, c.expireDeniedCooldown, c.expireDeniedWork, c.expireDeniedLane, c.expireDeniedGroup,
        c.expireDeniedThread, c.expireDeniedZoneUnknown, c.expireWorkOverride, c.expireRepeat, c.expireBlockedFb, c.expirePending,
        c.expireAfterWork, c.fed, c.appliedQuest, c.appliedGrind, c.appliedLiveQuest, c.appliedLiveGrind, c.fallbackReject,
        c.fallbackStale, c.fallbackNone, c.fallbackPurpose, c.fallbackBudget, c.fallbackDenied, c.fallbackParked,
        c.fallbackZoneEmpty, c.fbArbiter, c.fbCohort, c.turninUnreached, c.memEvict, c.modeQuestGrind, c.modeGrindQuest,
        c.modeDenied, c.fallbackRejectEmpty, c.zoneEmptyHeld, c.feedUs, c.feedUsMax);
    if (t < 0)
        return len;
    len += std::size_t(t);
    return len < n ? len : n - 1;
}

// ---- P-3a: the write path (P3-ENTWURF-v0.2, frozen d45c4961, Nachtraege 1-3; AiPlayerbot.Planner = 2, default 0) -------------
// Planner.WriteCohort (OB-00 F2 (a), section 8): 0 no bot writes (default), 1 every bot, 2 cohort B (odd low guid), 3 cohort A.
// Above 3 is 0: fail closed, never "all". The parity rule is CohortPolicy.h Applies (the unit test compares the two).
constexpr std::uint32_t kWriteCohortMax = 3;

inline std::uint32_t WriteCohortOf(std::uint32_t raw)
{
    return raw > kWriteCohortMax ? 0u : raw;
}

// The bot is in the write cohort of Planner = 2. Planner 0 / 1 never write; Planner = 2 alone writes nothing (WriteCohort 0).
inline bool WritesFor(std::uint32_t planner, std::uint32_t writeCohort, std::uint32_t guidLow)
{
    if (planner != 2)
        return false;
    bool const b = (guidLow & 1u) != 0;
    return writeCohort == 1 || (writeCohort == 2 && b) || (writeCohort == 3 && !b);
}

// The expire of the live target (section 2.3, conditions 2-5; the adapter PlannerWrite.cpp adds the thread, the lane (6) and the
// group copy (7)). The live facts are numbers: the adapter fills them on the bot's thread.
enum class ExpireStep : std::uint8_t
{
    Go = 0,              // conditions 2-5 hold
    NotOpen = 1,         // the live target is not READY, TRAVEL, WORK or COOLDOWN (nothing to expire; a pending expire ends)
    AppliedLive = 2,     // condition 2: the live target already is the pick (applied_live_quest / _grind)
    ZoneUnknown = 3,     // P3-1: a grind pick and the live target's zone is unknown: nothing expires
    DeniedCooldown = 4,  // condition 3: COOLDOWN is never expired
    DeniedWork = 5,      // condition 3: WORK only with a forced pick (max_hold; back_to_band never reaches the expire, OB-10 V-1)
    Repeat = 6,          // condition 4: this pick expired once (guard = 0)
    BlockedFb = 7,       // condition 5: a fallback less than MinBindSec ago
    DeniedLane = 8,      // condition 6: the lane check (Questbook, site "planner") refused
    DeniedGroup = 9,     // condition 7: the live target is a group copy (P3-2)
    DeniedThread = 10,   // not the bot's own map thread (expected 0)
    Expired = 11,        // the adapter expired the live target
};

struct ExpireFacts
{
    TravelPhase phase = TravelPhase::None;   // the adapter maps READY to Travel (open, not yet travelling)
    bool liveNull = true;                    // no destination or the null target
    std::uint32_t liveDestKey = 0;
    std::uint32_t liveQuest = 0;
    std::uint32_t liveZone = 0;              // the zone of the live target's point (0 = unknown)
    bool pickGrind = false;
    std::uint32_t pickDestKey = 0;
    std::uint32_t pickQuest = 0;
    std::uint32_t pickZone = 0;
    bool repeat = false;                     // condition 4: the pick (key and since) already expired once
    bool fallbackBlock = false;              // condition 5
};

// Conditions 2-5 in the design's order. Quest kinds compare destination AND quest (OB-60 H1: several quests can share a
// destination); a grind pick compares the zone of the live target (P3-1: a grind pick has destKey 0 and never matches a mob
// target by key).
inline ExpireStep ExpireDecide(ExpireFacts const& f, ExpireGate gate)
{
    bool const open = f.phase == TravelPhase::Travel || f.phase == TravelPhase::Work || f.phase == TravelPhase::Cooldown;
    if (f.liveNull || !open)
        return ExpireStep::NotOpen;
    if (f.pickGrind)
    {
        if (!f.liveZone)
            return ExpireStep::ZoneUnknown;
        if (f.liveZone == f.pickZone)
            return ExpireStep::AppliedLive;
    }
    else if (f.liveDestKey == f.pickDestKey && f.liveQuest == f.pickQuest)
        return ExpireStep::AppliedLive;
    if (gate == ExpireGate::Cooldown)
        return ExpireStep::DeniedCooldown;
    if (gate == ExpireGate::Work)
        return ExpireStep::DeniedWork;
    if (f.repeat)
        return ExpireStep::Repeat;
    if (f.fallbackBlock)
        return ExpireStep::BlockedFb;
    return ExpireStep::Go;
}

// Conditions 6 and 7 after Go, in this order (the adapter asks LaneFree first, then the group copy).
inline ExpireStep ExpireAdmit(bool laneFree, bool groupCopy)
{
    if (!laneFree)
        return ExpireStep::DeniedLane;
    if (groupCopy)
        return ExpireStep::DeniedGroup;
    return ExpireStep::Go;
}

// Rule 5: a fallback (the plan pick was not applied) blocks an expire for MinBindSec.
inline bool FallbackBlocks(std::uint32_t fallbackAtMs, std::uint32_t nowMs, std::uint32_t minBindMs)
{
    return fallbackAtMs && nowMs - fallbackAtMs < minBindMs;
}

// The fallback reasons of the feed (one fallback_* each). OB-10 V-3 (pre-review 20.11., deviation 6 rejected): rule 5 locks only
// after a fallback in which the attempt with the current pick failed - reject, budget, parked, denied, zone_empty. none, stale and
// purpose never set the lock: a bot whose quest pick meets grind searches, or whose pick goes stale between plan ticks, would
// renew it for ever and starve an open expire (condition 4, once per pick, already stops churn).
enum class FeedFallback : std::uint8_t
{
    None = 0,
    Stale = 1,
    Purpose = 2,
    Reject = 3,
    Budget = 4,
    Parked = 5,
    Denied = 6,
    ZoneEmpty = 7,
    // twow-repo#726 (OB-60 review 9f5669b7, OB-10 5a445aa4): two reasons that never lock. BudgetHeld: the feed skipped because a
    // budget abort of this pick is held or a full-list resume is pending (F2: only the abort itself, Budget, locks - a held skip
    // on every choice would renew rule 5 for ever). RejectEmpty: the quest pick's target is not in this choice's list (F3: the
    // plan attempt never ran; on every choice it would renew rule 5 for ever).
    BudgetHeld = 8,
    RejectEmpty = 9,
    // OB-10 Nachtrag 1 (1e3b50d1, Q2 (c)): a grind pick's zone empty locks (ZoneEmpty, V-3) once per pick; again for the same pick
    // (key and sinceMs) it is ZoneEmptyHeld: zone_empty_held, no lock. A new pick locks once again.
    ZoneEmptyHeld = 10,
};

inline bool FallbackLocks(FeedFallback f)
{
    return f == FeedFallback::Reject || f == FeedFallback::Budget || f == FeedFallback::Parked || f == FeedFallback::Denied ||
        f == FeedFallback::ZoneEmpty;
}

// The one setter of the rule 5 time (PlannerWrite.cpp calls it for every fallback; 0 stays "no fallback").
inline void NoteFallback(std::uint32_t& fallbackAtMs, FeedFallback f, std::uint32_t nowMs)
{
    if (FallbackLocks(f))
        fallbackAtMs = nowMs ? nowMs : 1u;
}

// twow-repo#726 F2 (OB-10 5a445aa4): the #416 hold of a pick whose plan attempt ran out of time. It holds only for the same pick
// (key and sinceMs) and only for MinBindSec after the abort (wrap-safe); otherwise FeedBegin clears it and the pick is fed again.
inline bool BudgetHolds(std::uint32_t heldKey, std::uint32_t heldSinceMs, std::uint32_t heldAtMs, std::uint32_t pickKey,
    std::uint32_t pickSinceMs, std::uint32_t nowMs, std::uint32_t minBindMs)
{
    return heldKey && heldKey == pickKey && heldSinceMs == pickSinceMs && nowMs - heldAtMs < minBindMs;
}

// OB-10 Nachtrag 1 (Q2 (c)): the zone-empty fallback of this pick already locked once (same key and sinceMs).
inline bool ZoneEmptySeen(std::uint32_t seenKey, std::uint32_t seenSinceMs, std::uint32_t pickKey, std::uint32_t pickSinceMs)
{
    return seenKey && seenKey == pickKey && seenSinceMs == pickSinceMs;
}

// The back_to_band pick of a writing bot (section 2.5, deviation 5): the forced band hub is not in the tier's pool, so it holds
// its bind time like an eligible incumbent. OB-10 V-1: the next plan therefore keeps it (Hold::Bind, or Same when the hub is the
// top candidate), never Switch - back_to_band never reaches the mode switch (ModeSwitchFrom) nor the expire of the switch tick:
// the mode stays as it is (the mode line stays QUEST), the grind comes through the existing grind fallback, fed with the pick
// (V-2). A direct mode switch from back_to_band would be a new behaviour (P-3c, its own OB-10 OK). After MinBind it ends. An
// empty pool (no top) leaves the pick untouched (no hysteresis). Only a max_hold (Hold::Forced, a complete quest's turn-in in
// tier A) can replace it inside MinBind; that switch and its mode are max_hold's (a turn-in pick wants QUEST).
inline bool BackToBandHolds(bool writes, bool incumbentEligible, bool pickHas, bool pickForced, bool pickGrind,
    std::uint32_t pickSinceMs, std::uint32_t nowMs, std::uint32_t minBindMs)
{
    return writes && !incumbentEligible && pickHas && pickForced && pickGrind && nowMs - pickSinceMs < minBindMs;
}

// setNewTarget of a fed choice (ApplyWatch): a new write generation is applied_quest / applied_grind; none is fallback_parked
// (the #485 refusal expired the live target) or fallback_denied (the arbiter refused the copy).
enum class ApplyOutcome : std::uint8_t
{
    Applied = 0,
    Parked = 1,
    Denied = 2,
};

inline ApplyOutcome ApplyOutcomeOf(std::uint32_t genBefore, std::uint32_t genAfter, bool liveExpired)
{
    if (genAfter != genBefore)
        return ApplyOutcome::Applied;
    return liveExpired ? ApplyOutcome::Parked : ApplyOutcome::Denied;
}

// The pending expire (section 2.3, Nachtrag 1 F8): a denial of the switch tick by WORK, the fallback block, the lane, the group
// copy or the thread leaves the expire pending; every tick tries it again with conditions 1-7 (P3-4: nothing expires by waiting -
// a WORK without a forced pick stays denied). It ends with the expire, a new pick (F8 (i): only while its key is the pick), the
// live target being the pick, or a live target that is no longer TRAVEL or WORK (the end of the target, also WORK -> COOLDOWN,
// F8 (iii)). WORK -> TRAVEL on the same target keeps it (F8 (a)): it fires in TRAVEL (expire_after_work).
inline bool PendingStarts(ExpireStep step)
{
    return step == ExpireStep::DeniedWork || step == ExpireStep::BlockedFb || step == ExpireStep::DeniedLane ||
        step == ExpireStep::DeniedGroup || step == ExpireStep::DeniedThread;
}

inline bool PendingKeeps(std::uint32_t pendingKey, std::uint32_t pickKey, ExpireStep step)
{
    return pendingKey && pendingKey == pickKey && PendingStarts(step);
}

// The F1 feed of a choice (section 2.4): which choice tries the plan pick first, and why not. Every "not" is one fallback_*.
enum class FeedSkip : std::uint8_t
{
    Feed = 0,
    Purpose = 1,   // the search is neither quest nor grind, a forced target (guild, pvp, petition), or the pick is of the other kind
    None = 2,      // no plan pick
    Stale = 3,     // the pick is older than the bind time (F5 (c))
    Budget = 4,    // #416: a resume of the full list is pending, or this pick's attempt already ran out of time
};

inline FeedSkip FeedCheck(bool questPurpose, bool grindPurpose, bool forcedTarget, bool hasPick, bool pickGrind,
    std::uint32_t pickAtMs, std::uint32_t nowMs, std::uint32_t freshMs, bool resumePending, bool budgetHeld)
{
    if (forcedTarget || (!questPurpose && !grindPurpose))
        return FeedSkip::Purpose;
    if (!hasPick)
        return FeedSkip::None;
    if (pickGrind != grindPurpose)
        return FeedSkip::Purpose;
    if (nowMs - pickAtMs >= freshMs)
        return FeedSkip::Stale;
    if (resumePending || budgetHeld)
        return FeedSkip::Budget;
    return FeedSkip::Feed;
}

// twow-repo#726 F2 (OB-10 Nachtrag 1 Q1 (a)): the fallback reason of a skipped feed. No skip locks rule 5: a held budget or a
// pending resume is BudgetHeld (fallback_budget, no lock); the abort itself (FeedEnd::Budget) locked once.
inline FeedFallback SkipFallback(FeedSkip skip)
{
    switch (skip)
    {
        case FeedSkip::Purpose: return FeedFallback::Purpose;
        case FeedSkip::Stale: return FeedFallback::Stale;
        case FeedSkip::Budget: return FeedFallback::BudgetHeld;
        default: return FeedFallback::None;
    }
}

// The mode switch (section 2.6, P3-3): only out of a pick change (switch or forced), never out of same / margin / bind; a grind
// pick of tier C wants GRIND, every other pick wants QUEST (Supervisor Decide with Requester::Planner and the dwell decides).
inline bool ModeSwitchFrom(Hold hold)
{
    return hold == Hold::Switch || hold == Hold::Forced;
}

inline bool PickWantsGrind(bool grindKind, Tier tier)
{
    return grindKind && tier == Tier::C;
}
}
