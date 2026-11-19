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
};

inline char const* FallbackName(Fallback f)
{
    switch (f)
    {
        case Fallback::None: return "-";
        case Fallback::R1Hold: return "r1_hold";
        case Fallback::BandTableOff: return "band_table_off";
        case Fallback::Arbiter: return "arbiter";
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
    Forced = 4,     // switch without margin and bind (max_hold, back_to_band)
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
// may (expire_work_override). Every other phase may expire.
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
};
constexpr std::size_t kMemorySlots = 8;

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

// The replacement rule: the same key, else the oldest expired slot, else the oldest slot. closeMs 0 is not a close.
inline void Close(MemorySlot* mem, std::size_t n, std::uint32_t key, std::uint32_t questId, std::uint8_t kind, CloseReason reason,
    std::uint32_t nowMs, std::uint32_t closeMs)
{
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
    }
    std::uint8_t const cycles = mem[at].key == key && mem[at].deadCycles < 255 ? mem[at].deadCycles : 0;
    mem[at].key = key;
    mem[at].questId = questId;
    mem[at].kind = kind;
    mem[at].reason = reason;
    mem[at].deadCycles = reason == CloseReason::Dead ? std::uint8_t(cycles + 1) : cycles;
    mem[at].closedAtMs = nowMs;
    mem[at].closedUntilMs = nowMs + closeMs;
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
};

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
}

inline std::size_t FormatPlanMin(char* buf, std::size_t n, char cohort, std::uint32_t minute, PlanMinCounts const& c)
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
    return std::size_t(w) < n ? std::size_t(w) : n - 1;
}
}
