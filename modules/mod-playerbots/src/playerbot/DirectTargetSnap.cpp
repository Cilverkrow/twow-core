#include "playerbot/playerbot.h"
#include "playerbot/DirectTargetSnap.h"
#include "Maps/MoveMap.h"
#include "Maps/MoveMapSharedDefines.h"
#include "Maps/GridDefines.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace ai::direct_targets;

namespace
{
// Detour stores (y, z, x).
inline void ToDetour(float x, float y, float z, float* out)
{
    out[0] = y;
    out[1] = z;
    out[2] = x;
}

inline int32 GridX(float y) { return int32(32 - y / SIZE_OF_GRIDS); }
inline int32 GridY(float x) { return int32(32 - x / SIZE_OF_GRIDS); }

// Link BFS from start over polys the filter allows, up to cap. twow-repo#594 / P3-b (review R2): an external portal edge
// without a link (the neighbour tile is not loaded) is an open edge and counts as large, so the result does not depend
// on which tiles happen to be loaded.
uint32 Component(dtNavMesh const* mesh, dtPolyRef start, uint16 include, uint16 exclude, uint32 cap, bool& openEdge)
{
    openEdge = false;
    if (!start)
        return 0;

    std::unordered_set<dtPolyRef> seen;
    std::vector<dtPolyRef> stack;
    seen.insert(start);
    stack.push_back(start);
    while (!stack.empty() && seen.size() < cap)
    {
        dtPolyRef const ref = stack.back();
        stack.pop_back();
        dtMeshTile const* tile = nullptr;
        dtPoly const* poly = nullptr;
        if (dtStatusFailed(mesh->getTileAndPolyByRef(ref, &tile, &poly)))
            continue;

        for (uint8 edge = 0; edge < poly->vertCount; ++edge)
        {
            if (!(poly->neis[edge] & DT_EXT_LINK))
                continue;
            bool linked = false;
            for (uint32 i = poly->firstLink; i != DT_NULL_LINK && !linked; i = tile->links[i].next)
                linked = tile->links[i].edge == edge;
            if (!linked)
            {
                openEdge = true;
                return cap;
            }
        }

        for (uint32 i = poly->firstLink; i != DT_NULL_LINK; i = tile->links[i].next)
        {
            dtPolyRef const next = tile->links[i].ref;
            if (!next || seen.count(next))
                continue;
            dtMeshTile const* nextTile = nullptr;
            dtPoly const* nextPoly = nullptr;
            if (dtStatusFailed(mesh->getTileAndPolyByRef(next, &nextTile, &nextPoly)))
                continue;
            if (!(nextPoly->flags & include) || (nextPoly->flags & exclude))
                continue;
            seen.insert(next);
            stack.push_back(next);
        }
    }
    return uint32(seen.size());
}
}

uint16 ai::direct_targets::SnapIncludeFlags()
{
    return uint16(NAV_GROUND | NAV_WATER);
}

uint16 ai::direct_targets::ShoreIncludeFlags()
{
    return uint16(NAV_GROUND);
}

uint16 ai::direct_targets::ShoreExcludeFlags()
{
    return uint16(NAV_WATER | NAV_STEEP_SLOPES | NAV_MAGMA | NAV_SLIME);
}

void DirectTargetSnap::LoadTiles(float x, float y, float half, bool neighbourhood)
{
    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    int32 const extra = neighbourhood ? 1 : 0;
    int32 const gxMin = std::max<int32>(0, GridX(y + half) - extra), gxMax = std::min<int32>(63, GridX(y - half) + extra);
    int32 const gyMin = std::max<int32>(0, GridY(x + half) - extra), gyMax = std::min<int32>(63, GridY(x - half) + extra);
    for (int32 gx = gxMin; gx <= gxMax; ++gx)
        for (int32 gy = gyMin; gy <= gyMax; ++gy)
            mmap->loadMap(mapId, gx, gy);
}

bool DirectTargetSnap::HasMesh() const
{
    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    return mmap->GetNavMesh(mapId) && mmap->GetNavMeshQuery(mapId);
}

SnapHit DirectTargetSnap::NearestMain(float x, float y, float z, float boxXY, float boxZ, uint16 include, uint16 exclude,
    uint32 minPolys, uint32 cap, bool& anyPoly) const
{
    SnapHit best;
    anyPoly = false;

    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    dtNavMesh const* mesh = mmap->GetNavMesh(mapId);
    dtNavMeshQuery const* query = mmap->GetNavMeshQuery(mapId);
    if (!mesh || !query)
        return best;

    dtQueryFilter filter;
    filter.setIncludeFlags(include);
    filter.setExcludeFlags(exclude);

    float point[3], extents[3] = { boxXY, boxZ, boxXY };
    ToDetour(x, y, z, point);
    dtPolyRef polys[1024];
    int count = 0;
    if (dtStatusFailed(query->queryPolygons(point, extents, &filter, polys, &count, 1024)))
        return best;

    struct Candidate { dtPolyRef ref; float sq; float c[3]; };
    std::vector<Candidate> candidates;
    for (int i = 0; i < count; ++i)
    {
        float closest[3];
        bool over = false;
        if (dtStatusFailed(query->closestPointOnPoly(polys[i], point, closest, &over)))
            continue;
        float const dy = closest[0] - point[0], dheight = closest[1] - point[1], dx = closest[2] - point[2];
        if (std::sqrt(dx * dx + dy * dy) > boxXY || std::fabs(dheight) > boxZ)
            continue;
        candidates.push_back({ polys[i], dx * dx + dy * dy + dheight * dheight, { closest[0], closest[1], closest[2] } });
    }
    anyPoly = !candidates.empty();
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.sq < b.sq; });

    for (Candidate const& candidate : candidates)
    {
        bool openEdge = false;
        uint32 const size = Component(mesh, candidate.ref, include, exclude, std::max(cap, minPolys), openEdge);
        if (openEdge)
            ++openEdges;
        if (!ComponentLarge(size, minPolys, openEdge))
            continue;

        best.found = true;
        best.x = candidate.c[2];
        best.y = candidate.c[0];
        best.z = candidate.c[1];
        best.xy = std::sqrt((best.x - x) * (best.x - x) + (best.y - y) * (best.y - y));
        best.dz = best.z - z;
        best.polys = size;
        best.openEdge = openEdge;
        break;
    }
    return best;
}

SnapOutcome DirectTargetSnap::Classify(float x, float y, float z, uint32 cap) const
{
    SnapOutcome outcome;
    if (!HasMesh())
    {
        outcome.noMesh = true;
        return outcome;
    }

    bool nearAny = false, farAny = false;
    SnapHit const nearHit = NearestMain(x, y, z, SnapOkXY, SnapOkDz, SnapIncludeFlags(), 0, MainComponentPolys, cap, nearAny);
    SnapHit const farHit = nearHit.found ? nearHit :
        NearestMain(x, y, z, SnapFarXY, SnapFarDz, SnapIncludeFlags(), 0, MainComponentPolys, cap, farAny);
    outcome.snap = ClassifySnap(nearHit.found, nearHit.xy, nearHit.dz, farHit.found, farHit.xy, farHit.dz);
    outcome.hit = outcome.snap == SnapClass::Ok ? nearHit : farHit;
    outcome.anyPoly = nearAny || farAny;
    return outcome;
}
