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
// bot's answer to a manager directive (PostDirectiveAck); the manager only counts it.
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
};
constexpr std::size_t kKinds = std::size_t(Kind::DirectiveAck) + 1;
// One bucket more than kinds: a corrupt or future kind lands in that "other" bucket, never in a real kind's count.
constexpr std::size_t kKindBuckets = kKinds + 1;

inline std::size_t KindIndex(Kind kind)
{
    std::size_t const i = std::size_t(kind);
    return i < kKinds ? i : kKinds;
}

// DirectiveAck states (carried in Event::mapOrDungeon).
constexpr std::uint8_t kAckReceived = 1;
constexpr std::uint8_t kAckApplied = 2;
constexpr std::uint8_t kAckRefused = 3;

// POD, 32 bytes, copied by value into the ring. The 7.2.11 fields add up to 28 bytes; reserved pads to the 32 the design
// names and keeps the size fixed for a later field (always 0 today).
struct Event
{
    std::uint32_t botLow;          // guid low of the sender
    Kind kind;
    std::uint8_t role;             // group_mgr::Role, 0 = unknown
    std::uint8_t level;
    std::uint8_t reason;
    std::uint32_t mapOrDungeon;    // map id, dungeon catalog id or quest id (by kind); DirectiveAck: the ack state
    std::uint32_t seq;             // sender sequence, for dedup and a later PostAck
    std::uint16_t hpPct10;         // per mille (Status)
    std::uint16_t manaPct10;       // per mille (Status), 0 without mana
    float x;                       // Status / RepopEdge position
    float y;
    std::uint32_t reserved;
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
    return s;
}

// One minute of the inbox, every field an increment except snapshots (the table size now).
struct MinuteLine
{
    std::uint64_t posted = 0;
    std::uint64_t dropped = 0;
    std::uint64_t drained = 0;
    std::uint64_t status = 0;
    std::uint64_t ack = 0;
    std::uint64_t snapshots = 0;
};

// A minute with no traffic and an empty table writes no line: with StatusSeconds 0 nothing posts, so a Manager = 1 run gets
// no new line until something arrives (an ack or a status).
inline bool MinuteLineDue(MinuteLine const& m)
{
    return m.posted || m.dropped || m.drained || m.status || m.ack || m.snapshots;
}

// "[ManagerInbox] posted=.. dropped=.. drained=.. status=.. ack=.. snapshots=..": its own tag, read by no existing evaluator.
inline int FormatMinuteLine(char* buf, std::size_t size, MinuteLine const& m)
{
    return std::snprintf(buf, size, "[ManagerInbox] posted=%llu dropped=%llu drained=%llu status=%llu ack=%llu snapshots=%llu",
        (unsigned long long)m.posted, (unsigned long long)m.dropped, (unsigned long long)m.drained,
        (unsigned long long)m.status, (unsigned long long)m.ack, (unsigned long long)m.snapshots);
}

// The manager was off (BotGroups.Manager reloaded 1 -> 0 -> 1) when the gap between two passes is longer than this: its minute
// window, snapshot table and the events left in the ring are stale then.
constexpr std::uint32_t kResumeGapMs = 2 * 60000;

inline bool ResumedAfterGap(std::uint32_t lastPassMs, std::uint32_t nowMs)
{
    return std::uint32_t(nowMs - lastPassMs) > kResumeGapMs;
}
}
