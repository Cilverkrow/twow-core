#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace ai::island
{
// twow-repo#575 A3 step 4 M-c (OB-00 06.11.2026; evidence ws-10/cli-pathfinding/a3-step4-route/count-mc-ma/
// RESULT-COUNT-MC-MA-20261106.md, plan PLAN-M-c-20261106.md): no far-move arrival on a navmesh micro-island, and a bot
// that already stands on one steps off it. Pure rules, no game types, no config reads; the Detour side is IslandGuard.cpp.
//
// Why: FarMoveArrivalPoint (MovementActions.cpp) snaps the arrival with ClosestCorrectPoint(20, 50) - nearest poly, no
// component check. Offline, that snap put 858 of 907 linkable island starts onto the island (control: 3 of 723); island
// starts were 3,141 of 9,033 failed node routes (CFC / A3 / m1r), the q12 node-budget class 212.
//
// Island: a link component below IslandMaxPolys polys (default 200). With < 200 polys the server logged "all start legs
// failed" for 70 % of the route starts against 7.5 % on the main mesh.

constexpr std::uint32_t DefaultMaxPolys = 200;
constexpr std::uint32_t MinMaxPolys = 20;
constexpr std::uint32_t MaxMaxPolys = 1000;   // the tile labels store sizes as uint16
constexpr float DefaultSnapDz = 10.0f;        // offline: dz 10 finds main ground for 68 % of island arrivals; dz 50 91 %, but
constexpr float MinSnapDz = 3.0f;             // 316 of those hits lie on another floor (caves, bridges)
constexpr float MaxSnapDz = 30.0f;
constexpr float SnapXY = 20.0f;               // the horizontal box of ClosestCorrectPoint(20, 50)
constexpr float DefaultStepOffYards = 7.0f;   // 1,275 of 3,141 island starts lie within 7 yd of the main mesh
constexpr float MaxStepOffYards = 15.0f;
constexpr float StepOffDz = 3.0f;
constexpr std::uint32_t StepOffIntervalSeconds = 10;
constexpr std::uint32_t StepOffLinesPerMinute = 30;

// Config loaders: an unknown mode is off (CohortPolicy.h: 0 off, 1 all, 2 cohort B, 3 cohort A).
inline std::uint32_t ClampMode(std::int32_t mode)
{
    return mode >= 0 && mode <= 3 ? std::uint32_t(mode) : 0u;
}

inline std::uint32_t ClampMaxPolys(std::int32_t polys)
{
    if (polys < std::int32_t(MinMaxPolys))
        return MinMaxPolys;
    if (polys > std::int32_t(MaxMaxPolys))
        return MaxMaxPolys;
    return std::uint32_t(polys);
}

inline float ClampSnapDz(float dz)
{
    if (!(dz >= MinSnapDz))
        return MinSnapDz;
    return dz > MaxSnapDz ? MaxSnapDz : dz;
}

inline float ClampStepOffYards(float yards)
{
    if (!(yards > 0.0f))
        return 0.0f;
    return yards > MaxStepOffYards ? MaxStepOffYards : yards;
}

// The verdict of one poly from its tile labels. Components are labelled inside one tile; border = a poly of the component
// has a tile-portal edge (DT_EXT_LINK, static tile data). A component without a border edge is complete inside its tile.
enum class TileVerdict : std::uint8_t
{
    Main,         // complete in the tile and large, or large already inside the tile
    Island,       // complete in the tile and small
    CheckAcross,  // small inside the tile but touching its edge: the bounded link BFS across the edge decides
};

inline TileVerdict FromTileLabels(std::uint32_t tileSize, bool border, std::uint32_t maxPolys)
{
    if (tileSize >= maxPolys)
        return TileVerdict::Main;
    return border ? TileVerdict::CheckAcross : TileVerdict::Island;
}

// The bounded BFS across the tile edge (capped at maxPolys). An external portal edge without a link (neighbour tile not
// loaded) is open and counts as main, as DirectTargetSnap's Component does: the result never depends on which tiles
// happen to be loaded in the direction of "island".
inline bool IslandAcross(std::uint32_t bfsSize, bool openEdge, std::uint32_t maxPolys)
{
    return !openEdge && bfsSize < maxPolys;
}

// What the arrival snap did; the snap= field of [FarMoveArrive].
enum class ArrivalSnap : std::uint8_t
{
    Unchecked,    // switch off for this bot (the field is not printed)
    None,         // checked, the point is on the main mesh (or no server poly there to judge)
    IslandToMain, // the point was on an island and moved to the nearest main ground
    IslandNoMain, // the point was on an island and no main ground in the box: no arrival point (the caller walks)
};

inline char const* SnapName(ArrivalSnap snap)
{
    switch (snap)
    {
        case ArrivalSnap::None: return "none";
        case ArrivalSnap::IslandToMain: return "island>main";
        case ArrivalSnap::IslandNoMain: return "island>none";
        default: return "unchecked";
    }
}

// A main-ground candidate for the arrival snap: within SnapXY horizontally and snapDz vertically of the island point.
inline bool SnapCandidateOk(float xy, float dz, float snapDz)
{
    return xy <= SnapXY && std::fabs(dz) <= snapDz;
}

// A main-ground target for the step-off: within maxYards (3D) and StepOffDz vertically; maxYards 0 = no step-off.
inline bool StepOffTargetOk(float d3, float dz, float maxYards)
{
    return maxYards > 0.0f && d3 <= maxYards && std::fabs(dz) <= StepOffDz;
}

// The step-off is only the fallback for a bot already on an island: not on a transport, not swimming or flying, no far
// move pending (its arrival is snapped), and at most once per StepOffIntervalSeconds.
inline bool StepOffAllowed(bool onTransport, bool swimming, bool flying, bool farMovePending, std::uint64_t nowSeconds,
    std::uint64_t lastStepOffSeconds)
{
    if (onTransport || swimming || flying || farMovePending)
        return false;
    return lastStepOffSeconds == 0 || nowSeconds >= lastStepOffSeconds + StepOffIntervalSeconds;
}

// Thread model (OB-00 06.11.2026): the navmesh of a map is shared by every map thread of that map id; each thread has its
// own dtNavMeshQuery (MMapManager::QueryForThread). The tile labels are filled lazily by whichever map thread first needs a
// tile, and read by all of them. PublishOnceCache:
//  - a hit is read under the shared lock and handed out as shared_ptr<const Value>: published values are immutable;
//  - a miss computes the value OUTSIDE the lock (labelling a tile reads only immutable tile data), then publishes it under
//    the unique lock; when another thread published the same key and stamp first, that value is kept and returned, so
//    every caller of one (key, stamp) sees the same object (publish-once);
//  - a different stamp (a reloaded tile has a new salt) replaces the slot; readers still holding the old value keep it alive.
template <class Key, class Value, class Hash = std::hash<Key>>
class PublishOnceCache
{
public:
    using Ptr = std::shared_ptr<Value const>;

    template <class Compute>
    Ptr Get(Key const& key, std::uint32_t stamp, Compute&& compute, bool* computedHere = nullptr)
    {
        if (computedHere)
            *computedHere = false;
        {
            std::shared_lock<std::shared_mutex> read(lock);
            auto const it = slots.find(key);
            if (it != slots.end() && it->second.value && it->second.stamp == stamp)
                return it->second.value;
        }

        Ptr fresh = std::make_shared<Value const>(compute());

        std::unique_lock<std::shared_mutex> write(lock);
        Slot& slot = slots[key];
        if (slot.value && slot.stamp == stamp)
            return slot.value;
        slot.stamp = stamp;
        slot.value = std::move(fresh);
        if (computedHere)
            *computedHere = true;
        return slot.value;
    }

    std::size_t Size() const
    {
        std::shared_lock<std::shared_mutex> read(lock);
        return slots.size();
    }

private:
    struct Slot
    {
        std::uint32_t stamp = 0;
        Ptr value;
    };

    mutable std::shared_mutex lock;
    std::unordered_map<Key, Slot, Hash> slots;
};

// Shared by all map threads; [IslandGuard] summary once a minute (the counts of the previous minute).
struct Counters
{
    std::atomic<std::uint32_t> checked{0};       // arrival points checked
    std::atomic<std::uint32_t> island{0};        // of these on an island
    std::atomic<std::uint32_t> snapped{0};       // island>main
    std::atomic<std::uint32_t> noMain{0};        // island>none
    std::atomic<std::uint32_t> stepOff{0};       // step-offs started
    std::atomic<std::uint32_t> across{0};        // bounded BFS across a tile edge
    std::atomic<std::uint32_t> labelTiles{0};    // tiles labelled (published here)
    std::atomic<std::uint64_t> labelUs{0};       // their labelling time
    std::atomic<std::uint64_t> labelBytes{0};    // their label memory (cumulative since start, not reset)
    std::atomic<std::uint32_t> stepOffLines{0};  // [IslandStepOff] lines this minute
    std::atomic<std::uint64_t> lastLogMinute{0};

    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t const minute = nowSeconds / 60;
        std::uint64_t seen = lastLogMinute.load(std::memory_order_relaxed);
        return seen != minute && lastLogMinute.compare_exchange_strong(seen, minute, std::memory_order_relaxed);
    }

    // True for the first StepOffLinesPerMinute step-offs of a minute (reset by the summary).
    bool StepOffLineAllowed()
    {
        return stepOffLines.fetch_add(1, std::memory_order_relaxed) < StepOffLinesPerMinute;
    }
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}
}
