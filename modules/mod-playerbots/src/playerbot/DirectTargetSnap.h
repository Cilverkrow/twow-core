#pragma once

#include "playerbot/DirectTargetPolicy.h"
#include <cstdint>

namespace ai::direct_targets
{
// twow-repo#594 / P3-b: the runtime Detour snap of the boot loader (TravelMgr::LoadDirectTargets), with the parameters
// of the offline tool (p3b-data/snap.cpp). Master thread at boot only: it uses that thread's dtNavMeshQuery and loads
// the mmap tiles under each query box (the TerrainInfo::GetGrid formula). Never the WorldPosition corrected-point
// helper (it asserts and overwrites the point).
struct SnapHit
{
    bool found = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float xy = -1.0f;   // 2D distance to the probe
    float dz = 0.0f;    // snapped z - probe z
    std::uint32_t polys = 0;   // link component size (capped)
    bool openEdge = false;
};

struct SnapOutcome
{
    SnapClass snap = SnapClass::Drop;
    SnapHit hit;          // the snapped point for snap_ok / snap_far
    bool anyPoly = false; // a poly in range, but only on small components (drop reason island)
    bool noMesh = false;  // no navmesh for the map (drop reason nomesh)
};

class DirectTargetSnap
{
public:
    explicit DirectTargetSnap(std::uint32_t mapId) : mapId(mapId) {}

    // Loads the mmap tiles under the box (+- half yards); with neighbourhood also the 3x3 tiles around the centre.
    void LoadTiles(float x, float y, float half, bool neighbourhood = false);

    bool HasMesh() const;

    // snap_ok / snap_far / drop_offmesh as the offline classification; components count up to cap polys.
    SnapOutcome Classify(float x, float y, float z, std::uint32_t cap = MainComponentPolys) const;

    // The nearest poly in the box (+- xy, +- dz around z) whose link component reaches minPolys (or has an open edge);
    // include / exclude are NavTerrain flags. anyPoly: a poly was in the box at all.
    SnapHit NearestMain(float x, float y, float z, float boxXY, float boxZ, std::uint16_t include, std::uint16_t exclude,
        std::uint32_t minPolys, std::uint32_t cap, bool& anyPoly) const;

    std::uint32_t OpenEdges() const { return openEdges; }

private:
    std::uint32_t mapId;
    mutable std::uint32_t openEdges = 0;
};

// The ground filters of the snap: GROUND | WATER (PathFinder default for players); the fishing shore: dry GROUND only.
std::uint16_t SnapIncludeFlags();
std::uint16_t ShoreIncludeFlags();
std::uint16_t ShoreExcludeFlags();
}
