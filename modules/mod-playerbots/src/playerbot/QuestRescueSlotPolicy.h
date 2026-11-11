#pragma once

// twow-repo#703 item 1 (AiPlayerbot.QuestRescue.NoTargetFreeSlot, CLI-PATH 11.11.2026; design
// ws-10/cli-pathfinding/szf/DESIGN-703-1-NOTARGET-SLOT-20261111.md v2, OB-10 review 1ef7451f). Before this key, a quest rescue took
// one of the global limiter slots (5 per 10 min) before it computed its targets, so a rescue with no target (many are empty by
// design: owner rule R1, no rescue inside the start zone) used a slot and moved nothing. With the key the targets come first:
// an empty list takes no slot (the band rescue is consumed, no requeue, no limiter call), a non-empty one asks the limiter as
// before. Pure rules, no game types, no config reads, no log.

#include <atomic>
#include <cstdint>

namespace ai::rescue_slot
{
// The key: 0 off (the legacy order), 1 on; any other value acts like 0 (the reader logs it).
inline std::uint32_t ClampMode(std::int64_t value)
{
    return value == 1 ? 1u : 0u;
}

inline bool ModeValid(std::int64_t value)
{
    return value == 0 || value == 1;
}

// The target list is computed before the limiter only with the key on.
inline bool TargetsFirst(std::uint32_t mode)
{
    return mode == 1;
}

// A rescue needs a global slot only when it has somewhere to go.
inline bool NeedsSlot(bool targetsEmpty)
{
    return !targetsEmpty;
}

// The list computed before the limiter is used for the teleport only when the band rung taken after the limiter is the one that
// was peeked (the tick barrier keeps them equal; a difference is counted as rung_mismatch and the list is computed again).
inline bool ReuseEarlyTargets(bool earlyComputed, std::uint32_t takenRungPlusOne, std::uint32_t peekedRungPlusOne)
{
    return earlyComputed && takenRungPlusOne == peekedRungPlusOne;
}

// Realm-wide counters for the [QuestRescueMin] minute line (key on only; relaxed atomics).
struct Counters
{
    std::atomic<std::uint32_t> noTargetNoSlot{0};   // empty target list: no limiter call, no slot used
    std::atomic<std::uint32_t> rungMismatch{0};     // taken rung != peeked rung: targets computed again
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
