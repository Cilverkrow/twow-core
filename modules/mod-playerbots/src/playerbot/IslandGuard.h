#pragma once

#include "playerbot/IslandGuardPolicy.h"
#include <cstdint>

namespace ai::island
{
// twow-repo#575 A3 step 4 M-c (AiPlayerbot.Path.IslandGuard): the Detour side of IslandGuardPolicy.h. Callable from any
// map thread: each call uses that thread's own dtNavMeshQuery (MMapManager::GetNavMeshQuery) and the shared tile label
// cache (PublishOnceCache, thread model in IslandGuardPolicy.h). Never loads tiles; a point without a loaded poly is
// "not judged" and left as it is.

struct ArrivalCheck
{
    ArrivalSnap snap = ArrivalSnap::Unchecked;
    float distance = 0.0f;       // island>main: 3D distance from the island point to the main ground point
    std::uint32_t polys = 0;     // island component size (inside the tile, or across the edge when that decided)
};

// The arrival point of a far move, already snapped by ClosestCorrectPoint (x, y, z in world coordinates). On an island
// the point is moved to the nearest main ground (same filter as ClosestCorrectPoint, within SnapXY / snapDz).
ArrivalCheck CheckArrival(std::uint32_t mapId, float& x, float& y, float& z, std::uint32_t maxPolys, float snapDz);

struct StepOff
{
    bool onIsland = false;       // the bot's server poly is on an island
    bool found = false;          // a main-ground target within maxYards
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distance = 0.0f, dz = 0.0f;
    std::uint32_t polys = 0;
};

// The bot's own spot: on an island, the nearest main ground within maxYards (StepOffTargetOk).
StepOff FindStepOff(std::uint32_t mapId, float x, float y, float z, std::uint32_t maxPolys, float maxYards);

// Tiles currently labelled (the cache size), for the [IslandGuard] summary.
std::uint32_t LabelledTiles();
}
