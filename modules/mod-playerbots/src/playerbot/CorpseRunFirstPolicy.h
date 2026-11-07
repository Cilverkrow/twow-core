#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ai::corpse_first
{
// twow-repo#575 A3 step 4 S4-1 (AiPlayerbot.Revive.CorpseRunFirst, OB-00 07.11.2026; design
// ws-10/cli-pathfinding/a3-step4-route/DESIGN-STEP4-RESCUE-DANGER-20261107.md d54dfd0e): corpse run before the spirit healer.
// Pure rules, no game types, no config reads, no logging.
//
// Why: "revive from corpse" never ran in the dead engine (U4). The engine always carries "stay", and the base
// MovementAction::isUseful is !stay, so the ghost walked to its corpse and then waited for the spirit healer (dead > 10 min
// and death count > 1, or > 20 min) or for "move long stuck" to repop it (race start, group leave). With the switch the
// reclaim runs. The spirit healer is chosen only for a reason below, never for "many deaths" alone. Real loops at the corpse
// show as Reason::Loop (own deaths near the corpse), next to the unchanged death loop / death count > 15 repops.

constexpr std::uint32_t DefaultMaxSeconds = 600;   // dead longer than this: spirit healer (the corpse run is taking too long)
constexpr std::uint32_t MinMaxSeconds = 120;
constexpr std::uint32_t MaxMaxSeconds = 1800;
constexpr std::uint32_t LoopDeaths = 2;            // own deaths near the corpse within LoopWindowSeconds that make a loop
constexpr std::uint32_t LoopWindowSeconds = 600;
constexpr float LoopRadius = 60.0f;                // yards (CampBlock radius)
constexpr std::uint8_t MinDurability = 10;         // percent; below it the spirit healer (as the legacy rule)
constexpr std::size_t RingSize = 4;

// Cohort mode (CohortPolicy.h): 0 off, 1 all, 2 cohort B, 3 cohort A; anything else is off.
inline std::uint32_t ClampMode(int value)
{
    return value >= 0 && value <= 3 ? std::uint32_t(value) : 0u;
}

inline std::uint32_t ClampMaxSeconds(int value)
{
    if (value < int(MinMaxSeconds))
        return MinMaxSeconds;
    if (value > int(MaxMaxSeconds))
        return MaxMaxSeconds;
    return std::uint32_t(value);
}

// The bot's own last deaths (per bot, bot thread only). S4-4 (Danger.Memory) takes this ring over as its per-bot layer.
struct DeathMark
{
    std::uint32_t atSeconds = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Ring
{
    std::array<DeathMark, RingSize> marks{};
    std::size_t next = 0;
    std::size_t count = 0;
};

inline void Record(Ring& ring, DeathMark const& mark)
{
    ring.marks[ring.next] = mark;
    ring.next = (ring.next + 1) % RingSize;
    if (ring.count < RingSize)
        ++ring.count;
}

// Own deaths within LoopWindowSeconds before now and LoopRadius (2D) of the point, same map.
inline std::uint32_t DeathsNear(Ring const& ring, std::uint32_t nowSeconds, std::uint32_t mapId, float x, float y)
{
    std::uint32_t n = 0;
    for (std::size_t i = 0; i < ring.count; ++i)
    {
        DeathMark const& d = ring.marks[i];
        if (d.mapId != mapId || d.atSeconds > nowSeconds || nowSeconds - d.atSeconds > LoopWindowSeconds)
            continue;
        float const dx = d.x - x, dy = d.y - y;
        if (dx * dx + dy * dy <= LoopRadius * LoopRadius)
            ++n;
    }
    return n;
}

enum class Reason : std::uint8_t
{
    Corpse = 0,   // run to the corpse and reclaim it
    NoCorpse,
    Sickness,
    Durability,
    Loop,
    Timeout,
    Count
};

inline char const* ReasonName(Reason r)
{
    switch (r)
    {
        case Reason::Corpse: return "corpse";
        case Reason::NoCorpse: return "no_corpse";
        case Reason::Sickness: return "sickness";
        case Reason::Durability: return "durability";
        case Reason::Loop: return "loop";
        case Reason::Timeout: return "timeout";
        default: return "unknown";
    }
}

struct Input
{
    bool hasCorpse = false;
    bool sickness = false;           // resurrection sickness already on the bot: nothing to lose
    std::uint8_t durability = 100;   // percent
    std::uint32_t deadSeconds = 0;   // since the ghost time of the corpse
    std::uint32_t maxSeconds = DefaultMaxSeconds;
    std::uint32_t deathsNear = 0;    // DeathsNear() at the corpse, the current death included
};

// Spirit healer only with a reason; Reason::Corpse = no spirit healer. The legacy reasons dropped for these bots:
// death count > 8, dead > 10 min with death count > 1, and the enemies/graveyard-in-sight shortcuts.
inline Reason Decide(Input const& in)
{
    if (!in.hasCorpse)
        return Reason::NoCorpse;
    if (in.sickness)
        return Reason::Sickness;
    if (in.durability < MinDurability)
        return Reason::Durability;
    if (in.deathsNear >= LoopDeaths)
        return Reason::Loop;
    if (in.deadSeconds > in.maxSeconds)
        return Reason::Timeout;
    return Reason::Corpse;
}

// Realm-wide counters for the [CorpseRunMin] minute line (relaxed atomics; sums over a window are exact).
struct Counters
{
    std::atomic<std::uint32_t> deaths{0};        // deaths with a decision (the first decision of each death)
    std::array<std::atomic<std::uint32_t>, std::size_t(Reason::Count)> decided{};   // logged decisions by reason (first + changes)
    std::atomic<std::uint32_t> reclaimed{0};     // corpse reclaimed by "revive from corpse"
    std::atomic<std::uint32_t> ghostStuck{0};    // "move long stuck" on a ghost sent to the spirit healer instead of repop
    std::atomic<std::uint64_t> lastLogSeconds{0};

    // True once per minute for one caller.
    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t last = lastLogSeconds.load(std::memory_order_relaxed);
        if (nowSeconds < last + 60)
            return false;
        return lastLogSeconds.compare_exchange_strong(last, nowSeconds, std::memory_order_relaxed);
    }
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}
}
