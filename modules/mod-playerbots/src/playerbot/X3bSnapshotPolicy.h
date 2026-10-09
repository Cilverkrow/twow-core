#pragma once

// twow-repo#563 X3b, variant 2 (published snapshot, OB-00 go 10.10.2026; switch AiPlayerbot.X3b.PublishedTargets,
// default 0). A bot no longer computes values on another bot's context from its own region thread. The owner
// publishes what others need - here the targets around it - as an immutable snapshot; readers take a shared_ptr
// copy and never touch the other bot's Value objects. A snapshot older than MaxAgeMs is ignored and counted.

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace ai::x3b
{
    // Readers accept a snapshot this old. The owner republishes on every own attackers calculation, which
    // in combat is every few hundred ms; out of combat a stale snapshot only means "no extra targets".
    constexpr uint32_t MaxAgeMs = 3000;

    // uint32 ms clock that wraps after ~49 days; unsigned subtraction handles the wrap.
    inline bool Fresh(uint32_t nowMs, uint32_t publishedMs)
    {
        return uint32_t(nowMs - publishedMs) <= MaxAgeMs;
    }

    template <class Guid>
    struct PublishedTargets
    {
        uint32_t publishedMs = 0;
        std::vector<Guid> possibleTargets;   // the owner's own "possible targets" at AttackersValue::GetRange()
        Guid currentTarget;
        Guid oldTarget;
    };

    enum SnapshotRead { SnapshotHit, SnapshotStale, SnapshotMissing, SnapshotReads };

    inline std::atomic<uint64_t>& SnapshotCount(SnapshotRead read)
    {
        static std::atomic<uint64_t> counts[SnapshotReads] = {};
        return counts[read];
    }
}
