#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

// A3-GERUEST-L60 7.2.11 (OB-00 07.11.2026; AiPlayerbot.BotGroups.Manager.StatusSeconds, default 0): the manager inbox. Bots
// (any map thread) report to the group manager (world thread) by guid and plain values, never by pointer. This is the pure
// part: no game types, no config reads, no locks. ManagerInbox.cpp wraps the one ring with a mutex. The opposite direction
// (manager -> bot) is the supervisor's postbox and is not touched here.
namespace ai::manager_inbox
{
// New kinds go at the END only: the values of the others stay fixed. DirectiveAck (09.11., agreed with CLI-Projekt) is the
// bot's answer to a manager directive (PostDirectiveAck); the manager only counts it. GroupEdge (T30-0) is the bot's own group
// as the bot sees it (id, leader, size, flags), posted on a change and with every status.
enum class Kind : std::uint8_t
{
    QueueRequest,
    DungeonWish,
    GroupWish,
    Status,
    RepopEdge,
    ReviveEdge,
    RunEnded,
    QueueLeave,
    DirectiveAck,
    GroupEdge,
};
constexpr std::size_t kKinds = std::size_t(Kind::GroupEdge) + 1;
// One bucket more than kinds: a corrupt or future kind lands in that "other" bucket, never in a real kind's count.
constexpr std::size_t kKindBuckets = kKinds + 1;

inline std::size_t KindIndex(Kind kind)
{
    std::size_t const i = std::size_t(kind);
    return i < kKinds ? i : kKinds;
}

// DirectiveAck states (carried in Event::mapOrDungeon). The 32-bit state word: b0-7 the state, b8-15 0, b16-31 the low 16 bits
// of the group serial the bot holds (only kAckLeft sets them; states 1-3 leave them 0). kAckLeft (T30-1b, agreed with
// CLI-Projekt 10.11.): the bot left the GROUP mode on its own (T02-GROUP handler), posted with seq 0 and the reason 26
// goal_done, 27 goal_lost or 22 arbiter_off (the handler disarmed).
constexpr std::uint8_t kAckReceived = 1;
constexpr std::uint8_t kAckApplied = 2;
constexpr std::uint8_t kAckRefused = 3;
constexpr std::uint8_t kAckLeft = 4;

// POD, 32 bytes, copied by value into the ring. The 7.2.11 fields add up to 28 bytes; reserved pads to the 32 the design
// names and keeps the size fixed for a later field (always 0 today).
struct Event
{
    std::uint32_t botLow;          // guid low of the sender
    Kind kind;
    std::uint8_t role;             // group_mgr::Role, 0 = unknown
    std::uint8_t level;
    std::uint8_t reason;
    std::uint32_t mapOrDungeon;    // map id, dungeon catalog id or quest id (by kind); DirectiveAck: the ack state;
                                   // GroupEdge: the core group id (0 = none)
    std::uint32_t seq;             // sender sequence, for dedup and a later PostAck; GroupEdge: the leader's guid low
    std::uint16_t hpPct10;         // per mille (Status); GroupWish: the map id
    std::uint16_t manaPct10;       // per mille (Status), 0 without mana
    float x;                       // Status / RepopEdge / GroupEdge / GroupWish position
    float y;
    std::uint32_t reserved;        // Status: the objective key (PackKey); GroupWish: the guild id; else 0
};
static_assert(sizeof(Event) == 32, "manager inbox Event must stay 32 bytes");
static_assert(std::is_trivially_copyable<Event>::value, "manager inbox Event must be trivially copyable");
static_assert(std::is_standard_layout<Event>::value, "manager inbox Event must be standard layout");

constexpr std::size_t Capacity = 4096;         // one ring for the whole module
constexpr std::size_t kDrainMaxPerPass = 1024; // events one manager pass takes; the rest waits for the next pass

struct Counters
{
    std::uint64_t posted = 0;    // accepted by Push
    std::uint64_t dropped = 0;   // refused by Push (ring full)
    std::uint64_t drained = 0;   // handed out by Drain
    std::uint64_t keyOverflow = 0;   // T30-0: status posts whose quest id did not fit the key (ManagerInbox.cpp, not the ring)
};

// Fixed-capacity FIFO. Not thread-safe by itself: the caller holds one mutex around Push/Drain/Read.
template <std::size_t N>
class Ring
{
    static_assert(N > 0, "ring capacity must be positive");

public:
    // false = full: the event is dropped and counted (a bot never waits for the manager).
    bool Push(Event const& event)
    {
        if (size_ == N)
        {
            ++counters_.dropped;
            return false;
        }
        buf_[(head_ + size_) % N] = event;
        ++size_;
        ++counters_.posted;
        return true;
    }

    // Appends up to max events to out, oldest first; returns how many.
    std::size_t Drain(std::vector<Event>& out, std::size_t max)
    {
        std::size_t const n = size_ < max ? size_ : max;
        for (std::size_t i = 0; i < n; ++i)
        {
            out.push_back(buf_[head_]);
            head_ = (head_ + 1) % N;
        }
        size_ -= n;
        counters_.drained += n;
        return n;
    }

    std::size_t Size() const { return size_; }
    Counters const& Read() const { return counters_; }

private:
    std::array<Event, N> buf_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    Counters counters_;
};

// Wrap-safe (unsigned difference of the 32-bit ms clock). periodMs 0 = never due; lastMs 0 = never posted = due.
inline bool StatusDue(std::uint32_t lastMs, std::uint32_t nowMs, std::uint32_t periodMs)
{
    if (periodMs == 0)
        return false;
    if (lastMs == 0)
        return true;
    return std::uint32_t(nowMs - lastMs) >= periodMs;
}

// The stamp a post stores as lastMs: 0 means "never posted", so a post at clock 0 stores 1.
inline std::uint32_t PostStamp(std::uint32_t nowMs)
{
    return nowMs ? nowMs : 1;
}

// cur / max in per mille, clamped to 0..1000; max 0 (no such power) = 0.
inline std::uint16_t PerMille(std::uint32_t cur, std::uint32_t max)
{
    if (max == 0)
        return 0;
    if (cur >= max)
        return 1000;
    return std::uint16_t(std::uint64_t(cur) * 1000u / max);
}

// AiPlayerbot.BotGroups.Manager.StatusSeconds: 0 = no status posts (default); above the max = the max (the loader writes
// one error line); a negative value = 0.
constexpr std::uint32_t kStatusSecondsMax = 3600;

inline std::uint32_t ClampStatusSeconds(std::int64_t raw)
{
    if (raw <= 0)
        return 0;
    if (raw > std::int64_t(kStatusSecondsMax))
        return kStatusSecondsMax;
    return std::uint32_t(raw);
}

// The minute line prints increments, never totals (XWatch format): the delta since the last line, then remember the total.
// Unsigned difference, so a counter that wrapped still gives the right increment.
inline std::uint64_t TakeDelta(std::uint64_t total, std::uint64_t& seen)
{
    std::uint64_t const delta = total - seen;
    seen = total;
    return delta;
}

// ---- T30-0 (design T30 v1.1 3.1): the bots' facts for the manager -----------------------------------------------------------
// Every value is the sending bot's own state, read on its own thread (no other player, no member list). Measurement only: the
// manager stores them and counts shadow candidates; nothing acts on them yet.

// Status flags (Event::reason of a Status). b7 stays 0.
constexpr std::uint8_t kFlagAlliance = 0x01;
constexpr std::uint8_t kFlagAlive = 0x02;
constexpr std::uint8_t kFlagCandidate = 0x04;       // adhoc_group::IsCandidateBot (roster, no real master, no BG/instance/raid)
constexpr std::uint8_t kFlagInBgQueue = 0x08;
constexpr std::uint8_t kFlagDungeonMap = 0x10;
constexpr std::uint8_t kFlagBusy = 0x20;            // taxi, teleport, a running S5a band trip or parked
constexpr std::uint8_t kFlagStartZoneHold = 0x40;   // zone_keep::StartZoneHolds (R1)

struct StatusFlags
{
    bool alliance = false;
    bool alive = false;
    bool candidate = false;
    bool inBgQueue = false;
    bool dungeonMap = false;
    bool busy = false;
    bool startZoneHold = false;
};

inline std::uint8_t PackFlags(StatusFlags const& f)
{
    return std::uint8_t((f.alliance ? kFlagAlliance : 0) | (f.alive ? kFlagAlive : 0) | (f.candidate ? kFlagCandidate : 0) |
        (f.inBgQueue ? kFlagInBgQueue : 0) | (f.dungeonMap ? kFlagDungeonMap : 0) | (f.busy ? kFlagBusy : 0) |
        (f.startZoneHold ? kFlagStartZoneHold : 0));
}

inline StatusFlags UnpackFlags(std::uint8_t bits)
{
    StatusFlags f;
    f.alliance = (bits & kFlagAlliance) != 0;
    f.alive = (bits & kFlagAlive) != 0;
    f.candidate = (bits & kFlagCandidate) != 0;
    f.inBgQueue = (bits & kFlagInBgQueue) != 0;
    f.dungeonMap = (bits & kFlagDungeonMap) != 0;
    f.busy = (bits & kFlagBusy) != 0;
    f.startZoneHold = (bits & kFlagStartZoneHold) != 0;
    return f;
}

// The objective key (Event::reserved of a Status): b0-22 quest id, b23-25 objective + 1 (0 = none), b26-27 quest status,
// b28 objective done, b29-31 progress counter mod 8 (a change between two posts shows; 8 changes in one period alias).
constexpr std::uint32_t kKeyQuestMax = (1u << 23) - 1;
constexpr std::uint8_t kQuestNone = 0;
constexpr std::uint8_t kQuestIncomplete = 1;
constexpr std::uint8_t kQuestComplete = 2;
constexpr std::uint8_t kQuestRewarded = 3;

struct KeyFacts
{
    std::uint32_t questId = 0;     // 0 = no key
    std::int8_t objective = -1;    // 0..6, -1 = none
    std::uint8_t questStatus = kQuestNone;
    bool objDone = false;
    std::uint32_t progress = 0;    // the objective counter; the key keeps it mod 8
};

// false = the quest id does not fit 23 bits: the key is posted empty (0) and the caller counts key_overflow.
inline bool PackKey(KeyFacts const& k, std::uint32_t& out)
{
    out = 0;
    if (k.questId > kKeyQuestMax)
        return false;
    std::uint32_t const objective = k.objective >= 0 && k.objective <= 6 ? std::uint32_t(k.objective + 1) : 0u;
    out = k.questId | (objective << 23) | (std::uint32_t(k.questStatus & 0x3u) << 26) | (k.objDone ? (1u << 28) : 0u) |
        ((k.progress & 0x7u) << 29);
    return true;
}

inline KeyFacts UnpackKey(std::uint32_t bits)
{
    KeyFacts k;
    k.questId = bits & kKeyQuestMax;
    std::uint32_t const objective = (bits >> 23) & 0x7u;
    k.objective = objective ? std::int8_t(objective - 1) : std::int8_t(-1);
    k.questStatus = std::uint8_t((bits >> 26) & 0x3u);
    k.objDone = ((bits >> 28) & 0x1u) != 0;
    k.progress = (bits >> 29) & 0x7u;
    return k;
}

// GroupEdge: the bot's own group only (id, leader, size, own flags) - never the member list (M6).
constexpr std::uint8_t kEdgeLeader = 0x01;
// 0x02 stays free (allRoster in v1.0; the manager derives it from the edges, M6).
constexpr std::uint8_t kEdgeRaid = 0x04;
constexpr std::uint8_t kEdgeBgGroup = 0x08;
constexpr std::uint8_t kEdgeInvite = 0x10;     // an invite is pending for the bot
constexpr std::uint8_t kEdgePosStuck = 0x20;   // T30-1: PosStuck of a manager QUEST leader; always 0 in T30-0

struct EdgeFacts
{
    std::uint32_t groupId = 0;     // 0 = no group
    std::uint32_t leaderLow = 0;
    std::uint32_t members = 0;
    bool leader = false;
    bool raid = false;
    bool bgGroup = false;
    bool invite = false;
    bool posStuck = false;

    bool operator==(EdgeFacts const& o) const
    {
        return groupId == o.groupId && leaderLow == o.leaderLow && members == o.members && leader == o.leader && raid == o.raid &&
            bgGroup == o.bgGroup && invite == o.invite && posStuck == o.posStuck;
    }
    bool operator!=(EdgeFacts const& o) const { return !(*this == o); }
};

inline Event MakeEdge(std::uint32_t botLow, EdgeFacts const& f, float x, float y)
{
    Event e{};
    e.botLow = botLow;
    e.kind = Kind::GroupEdge;
    e.mapOrDungeon = f.groupId;
    e.seq = f.leaderLow;
    e.role = std::uint8_t(f.members > 255 ? 255 : f.members);
    e.level = std::uint8_t((f.leader ? kEdgeLeader : 0) | (f.raid ? kEdgeRaid : 0) | (f.bgGroup ? kEdgeBgGroup : 0) |
        (f.invite ? kEdgeInvite : 0) | (f.posStuck ? kEdgePosStuck : 0));
    e.x = x;
    e.y = y;
    return e;
}

inline EdgeFacts EdgeOf(Event const& e)
{
    EdgeFacts f;
    f.groupId = e.mapOrDungeon;
    f.leaderLow = e.seq;
    f.members = e.role;
    f.leader = (e.level & kEdgeLeader) != 0;
    f.raid = (e.level & kEdgeRaid) != 0;
    f.bgGroup = (e.level & kEdgeBgGroup) != 0;
    f.invite = (e.level & kEdgeInvite) != 0;
    f.posStuck = (e.level & kEdgePosStuck) != 0;
    return f;
}

// GroupWish (interim sender, F3 (a): the S7 predicates on the bot's thread, until the planner sends): the waiting group quest
// with the most open objectives of an ungrouped, not busy bot.
constexpr std::uint8_t kWishOpenMask = 0x07;
constexpr std::uint8_t kWishDropOnly = 0x80;

struct WishFacts
{
    std::uint32_t questId = 0;
    std::uint32_t openObjectives = 0;   // the wish keeps 0..7
    bool dropOnly = false;
    std::uint8_t level = 0;
    std::uint32_t mapId = 0;            // the wish keeps 16 bits (0xFFFF above)
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t guildId = 0;          // 0 = none
};

inline Event MakeWish(std::uint32_t botLow, std::uint32_t seq, WishFacts const& f)
{
    Event e{};
    e.botLow = botLow;
    e.kind = Kind::GroupWish;
    e.mapOrDungeon = f.questId;
    e.seq = seq;
    e.reason = std::uint8_t((f.openObjectives > kWishOpenMask ? kWishOpenMask : f.openObjectives) | (f.dropOnly ? kWishDropOnly : 0));
    e.level = f.level;
    e.hpPct10 = std::uint16_t(f.mapId > 0xFFFFu ? 0xFFFFu : f.mapId);
    e.x = f.x;
    e.y = f.y;
    e.reserved = f.guildId;
    return e;
}

inline WishFacts WishOf(Event const& e)
{
    WishFacts f;
    f.questId = e.mapOrDungeon;
    f.openObjectives = e.reason & kWishOpenMask;
    f.dropOnly = (e.reason & kWishDropOnly) != 0;
    f.level = e.level;
    f.mapId = e.hpPct10;
    f.x = e.x;
    f.y = e.y;
    f.guildId = e.reserved;
    return f;
}

// The manager's latest status of one bot (the last Status event drained for that guid).
struct StatusSnapshot
{
    std::uint8_t role = 0;
    std::uint8_t level = 0;
    std::uint16_t hpPct10 = 0;
    std::uint16_t manaPct10 = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t seq = 0;
    std::uint32_t receivedMs = 0;
    std::uint8_t flags = 0;    // T30-0: PackFlags
    std::uint32_t key = 0;     // T30-0: PackKey
};

inline StatusSnapshot SnapshotOf(Event const& event, std::uint32_t receivedMs)
{
    StatusSnapshot s;
    s.role = event.role;
    s.level = event.level;
    s.hpPct10 = event.hpPct10;
    s.manaPct10 = event.manaPct10;
    s.mapId = event.mapOrDungeon;
    s.x = event.x;
    s.y = event.y;
    s.seq = event.seq;
    s.receivedMs = receivedMs;
    s.flags = event.reason;
    s.key = event.reserved;
    return s;
}

// T30-0: the manager's latest edge and wish of one bot.
struct EdgeSnapshot
{
    EdgeFacts edge;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t receivedMs = 0;
};

inline EdgeSnapshot EdgeSnapshotOf(Event const& event, std::uint32_t receivedMs)
{
    EdgeSnapshot s;
    s.edge = EdgeOf(event);
    s.x = event.x;
    s.y = event.y;
    s.receivedMs = receivedMs;
    return s;
}

struct WishSnapshot
{
    WishFacts wish;
    std::uint32_t receivedMs = 0;
};

inline WishSnapshot WishSnapshotOf(Event const& event, std::uint32_t receivedMs)
{
    WishSnapshot s;
    s.wish = WishOf(event);
    s.receivedMs = receivedMs;
    return s;
}

// A snapshot older than this is stale (design 2.3 kStaleMs): three missed status periods, at least 30 s.
constexpr std::uint32_t kStaleMinMs = 30000;

inline std::uint32_t StaleMs(std::uint32_t statusSeconds)
{
    std::uint64_t const ms = std::uint64_t(statusSeconds) * 3000u;
    return ms < kStaleMinMs ? kStaleMinMs : (ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : std::uint32_t(ms));
}

inline bool Fresh(std::uint32_t receivedMs, std::uint32_t nowMs, std::uint32_t staleMs)
{
    return std::uint32_t(nowMs - receivedMs) <= staleMs;
}

// T30-0 shadow candidate (design 2.4, only what T30-0 has: no registry, no .Types, no pair cooldown): a fresh status, alive,
// a candidate, not busy, not in a BG queue or an instance, a fresh edge that shows no group, and the supervisor applies.
// QUEST in addition: a fresh wish and no start-zone hold. Counted, nothing acts on it.
struct ShadowVerdict
{
    bool stale = false;          // the status is older than staleMs
    bool busy = false;           // everything else holds (bar the supervisor), but busy
    bool adhoc = false;          // an ADHOC candidate
    bool quest = false;          // an ADHOC candidate with a fresh wish, no start-zone hold
    bool zoneHold = false;       // an ADHOC candidate with a fresh wish, held in its start zone (R1)
    bool noSupervisor = false;   // everything else holds, but the supervisor does not apply to the bot
};

inline ShadowVerdict ShadowCandidate(StatusSnapshot const& s, EdgeSnapshot const* edge, WishSnapshot const* wish,
    bool supervisorApplies, std::uint32_t nowMs, std::uint32_t staleMs)
{
    ShadowVerdict v;
    if (!Fresh(s.receivedMs, nowMs, staleMs))
    {
        v.stale = true;
        return v;
    }
    StatusFlags const f = UnpackFlags(s.flags);
    if (!f.alive || !f.candidate || f.inBgQueue || f.dungeonMap)
        return v;
    // T30-0 knows no manager group yet, so "ungrouped or leader of an own ADHOC group with room" is "ungrouped".
    if (!edge || !Fresh(edge->receivedMs, nowMs, staleMs) || edge->edge.groupId != 0)
        return v;
    // Busy only after the other predicates, so cand_busy counts the bots the busy rule alone removes (no grouped
    // followers on a taxi, no dead or instanced bots).
    if (f.busy)
    {
        v.busy = true;
        return v;
    }
    if (!supervisorApplies)
    {
        v.noSupervisor = true;
        return v;
    }
    v.adhoc = true;
    if (wish && Fresh(wish->receivedMs, nowMs, staleMs) && wish->wish.questId)
    {
        if (f.startZoneHold)
            v.zoneHold = true;
        else
            v.quest = true;
    }
    return v;
}

// Stands at the minute line (the tables now), not increments.
struct ShadowCounts
{
    std::uint64_t stale = 0;
    std::uint64_t busy = 0;
    std::uint64_t adhoc = 0;
    std::uint64_t quest = 0;
    std::uint64_t zoneHold = 0;
    std::uint64_t noSupervisor = 0;
};

inline void Tally(ShadowCounts& c, ShadowVerdict const& v)
{
    c.stale += v.stale ? 1 : 0;
    c.busy += v.busy ? 1 : 0;
    c.adhoc += v.adhoc ? 1 : 0;
    c.quest += v.quest ? 1 : 0;
    c.zoneHold += v.zoneHold ? 1 : 0;
    c.noSupervisor += v.noSupervisor ? 1 : 0;
}

// Over the manager's tables (guid low -> snapshot maps); applies(low) = supervisor::Applies for that bot.
template <typename StatusMap, typename EdgeMap, typename WishMap, typename SupervisorApplies>
ShadowCounts CountShadow(StatusMap const& status, EdgeMap const& edges, WishMap const& wishes, SupervisorApplies const& applies,
    std::uint32_t nowMs, std::uint32_t staleMs)
{
    ShadowCounts c;
    for (auto const& entry : status)
    {
        auto const e = edges.find(entry.first);
        auto const w = wishes.find(entry.first);
        Tally(c, ShadowCandidate(entry.second, e == edges.end() ? nullptr : &e->second, w == wishes.end() ? nullptr : &w->second,
            applies(entry.first), nowMs, staleMs));
    }
    return c;
}

// One minute of the inbox. Increments: posted, dropped, drained, status, ack, edge, wish, key_overflow. Stands (the tables
// now): snapshots, edges, wishes and the shadow counts (stale_snap, cand_*, no_supervisor).
struct MinuteLine
{
    std::uint64_t posted = 0;
    std::uint64_t dropped = 0;
    std::uint64_t drained = 0;
    std::uint64_t status = 0;
    std::uint64_t ack = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t edge = 0;
    std::uint64_t wish = 0;
    std::uint64_t keyOverflow = 0;
    std::uint64_t edges = 0;
    std::uint64_t wishes = 0;
    ShadowCounts shadow;
};

// A minute with no traffic and empty tables writes no line: with StatusSeconds 0 nothing posts, so a Manager = 1 run gets
// no new line until something arrives (an ack or a status). The shadow counts come from the tables, so they need no term.
inline bool MinuteLineDue(MinuteLine const& m)
{
    return m.posted || m.dropped || m.drained || m.status || m.ack || m.snapshots || m.edge || m.wish || m.keyOverflow ||
        m.edges || m.wishes;
}

// "[ManagerInbox] posted=.. dropped=.. drained=.. status=.. ack=.. snapshots=.. edge=.. ...": its own tag, read by no existing
// evaluator. T30-0 appends its fields after snapshots=, so the fields of the inbox pair keep their place.
inline int FormatMinuteLine(char* buf, std::size_t size, MinuteLine const& m)
{
    return std::snprintf(buf, size, "[ManagerInbox] posted=%llu dropped=%llu drained=%llu status=%llu ack=%llu snapshots=%llu"
        " edge=%llu wish=%llu key_overflow=%llu edges=%llu wishes=%llu stale_snap=%llu cand_busy=%llu cand_adhoc=%llu"
        " cand_quest=%llu cand_zone_hold=%llu no_supervisor=%llu",
        (unsigned long long)m.posted, (unsigned long long)m.dropped, (unsigned long long)m.drained,
        (unsigned long long)m.status, (unsigned long long)m.ack, (unsigned long long)m.snapshots,
        (unsigned long long)m.edge, (unsigned long long)m.wish, (unsigned long long)m.keyOverflow,
        (unsigned long long)m.edges, (unsigned long long)m.wishes, (unsigned long long)m.shadow.stale,
        (unsigned long long)m.shadow.busy, (unsigned long long)m.shadow.adhoc, (unsigned long long)m.shadow.quest,
        (unsigned long long)m.shadow.zoneHold, (unsigned long long)m.shadow.noSupervisor);
}

// The manager was off (BotGroups.Manager reloaded 1 -> 0 -> 1) when the gap between two passes is longer than this: its minute
// window, snapshot table and the events left in the ring are stale then.
constexpr std::uint32_t kResumeGapMs = 2 * 60000;

inline bool ResumedAfterGap(std::uint32_t lastPassMs, std::uint32_t nowMs)
{
    return std::uint32_t(nowMs - lastPassMs) > kResumeGapMs;
}
}
