#include "playerbot/playerbot.h"
#include "playerbot/IslandGuard.h"
#include "Maps/MoveMap.h"
#include "Maps/MoveMapSharedDefines.h"
#include "Maps/PathFinder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <vector>

using namespace ai::island;

namespace
{
// Detour stores (y, z, x).
inline void ToDetour(float x, float y, float z, float* out)
{
    out[0] = y;
    out[1] = z;
    out[2] = x;
}

// The labels of one tile: per poly its component inside the tile (NoComp: the poly is outside the label filter), per
// component its size (polys, capped at 65535) and whether it touches a tile-portal edge. Immutable once published.
struct TileLabels
{
    std::vector<uint16> comp;
    std::vector<uint16> size;
    std::vector<uint8> border;
};

constexpr uint16 NoComp = 0xFFFF;

// Label filter = the server path filter for players (PathInfo default GROUND | WATER), the filter of its FindWalkPoly snap.
inline uint16 LabelInclude() { return uint16(NAV_GROUND | NAV_WATER); }
// Ground filter of the arrival / step-off targets = WorldPosition::ClosestCorrectPoint (include GROUND, exclude 0x1A).
inline uint16 GroundInclude() { return uint16(NAV_GROUND); }
inline uint16 GroundExclude() { return uint16(NAV_MAGMA_SLIME | NAV_GROUND_STEEP | NAV_WATER); }

inline bool InLabelFilter(dtPoly const& poly) { return (poly.flags & LabelInclude()) != 0; }

// Components inside one tile: only links whose target lies in the same tile. Reads immutable tile data only.
TileLabels Label(dtNavMesh const* mesh, dtMeshTile const* tile, unsigned tileIndex)
{
    TileLabels labels;
    int const polyCount = tile->header->polyCount;
    labels.comp.assign(std::size_t(polyCount), NoComp);
    std::vector<int> stack;
    for (int start = 0; start < polyCount; ++start)
    {
        if (labels.comp[start] != NoComp || !InLabelFilter(tile->polys[start]))
            continue;
        if (labels.size.size() >= NoComp)
            break;   // more components than uint16 can name: the rest stay "outside" (not judged); never seen in a tile
        uint16 const component = uint16(labels.size.size());
        labels.size.push_back(0);
        labels.border.push_back(0);
        labels.comp[start] = component;
        stack.push_back(start);
        uint32 count = 0;
        while (!stack.empty())
        {
            int const index = stack.back();
            stack.pop_back();
            dtPoly const& poly = tile->polys[index];
            ++count;
            for (uint8 edge = 0; edge < poly.vertCount; ++edge)
                if (poly.neis[edge] & DT_EXT_LINK)
                    labels.border[component] = 1;
            for (unsigned link = poly.firstLink; link != DT_NULL_LINK; link = tile->links[link].next)
            {
                dtPolyRef const next = tile->links[link].ref;
                if (!next || mesh->decodePolyIdTile(next) != tileIndex)
                    continue;
                unsigned const nextIndex = mesh->decodePolyIdPoly(next);
                if (nextIndex >= unsigned(polyCount) || labels.comp[nextIndex] != NoComp || !InLabelFilter(tile->polys[nextIndex]))
                    continue;
                labels.comp[nextIndex] = component;
                stack.push_back(int(nextIndex));
            }
        }
        labels.size[component] = uint16(std::min<uint32>(count, 0xFFFF));
    }
    return labels;
}

PublishOnceCache<uint64, TileLabels>& Cache()
{
    static PublishOnceCache<uint64, TileLabels> cache;
    return cache;
}

// The bounded link BFS across tile edges (OB-00 06.11.2026 Q1 A): only for a component that is small inside its tile and
// touches the tile edge; capped at cap polys. An external portal edge without a link (neighbour tile not loaded) is open.
uint32 Across(dtNavMesh const* mesh, dtPolyRef start, uint32 cap, bool& openEdge)
{
    openEdge = false;
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
            for (unsigned link = poly->firstLink; link != DT_NULL_LINK && !linked; link = tile->links[link].next)
                linked = tile->links[link].edge == edge;
            if (!linked)
            {
                openEdge = true;
                return cap;
            }
        }
        for (unsigned link = poly->firstLink; link != DT_NULL_LINK; link = tile->links[link].next)
        {
            dtPolyRef const next = tile->links[link].ref;
            if (!next || seen.count(next))
                continue;
            dtMeshTile const* nextTile = nullptr;
            dtPoly const* nextPoly = nullptr;
            if (dtStatusFailed(mesh->getTileAndPolyByRef(next, &nextTile, &nextPoly)) || !InLabelFilter(*nextPoly))
                continue;
            seen.insert(next);
            stack.push_back(next);
        }
    }
    return uint32(seen.size());
}

struct Verdict
{
    bool known = false;    // the poly was judged
    bool island = false;
    uint32 polys = 0;
};

Verdict Judge(dtNavMesh const* mesh, uint32 mapId, dtPolyRef ref, uint32 maxPolys)
{
    Verdict verdict;
    dtMeshTile const* tile = nullptr;
    dtPoly const* poly = nullptr;
    if (!ref || dtStatusFailed(mesh->getTileAndPolyByRef(ref, &tile, &poly)) || !tile->header)
        return verdict;

    unsigned salt = 0, tileIndex = 0, polyIndex = 0;
    mesh->decodePolyId(ref, salt, tileIndex, polyIndex);

    Counters& counters = SharedCounters();
    bool labelledHere = false;
    uint64 labelUs = 0;
    PublishOnceCache<uint64, TileLabels>::Ptr const labels = Cache().Get((uint64(mapId) << 32) | uint64(tileIndex), uint32(salt),
        [&]()
        {
            auto const startedAt = std::chrono::steady_clock::now();
            TileLabels fresh = Label(mesh, tile, tileIndex);
            labelUs = uint64(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startedAt).count());
            return fresh;
        },
        &labelledHere);
    if (labelledHere)
    {
        counters.labelTiles.fetch_add(1, std::memory_order_relaxed);
        counters.labelUs.fetch_add(labelUs, std::memory_order_relaxed);
        counters.labelBytes.fetch_add(uint64(labels->comp.size()) * 2 + uint64(labels->size.size()) * 3, std::memory_order_relaxed);
    }

    if (polyIndex >= labels->comp.size() || labels->comp[polyIndex] == NoComp)
        return verdict;

    uint16 const component = labels->comp[polyIndex];
    verdict.known = true;
    verdict.polys = labels->size[component];
    switch (FromTileLabels(labels->size[component], labels->border[component] != 0, maxPolys))
    {
        case TileVerdict::Main:
            break;
        case TileVerdict::Island:
            verdict.island = true;
            break;
        case TileVerdict::CheckAcross:
        {
            counters.across.fetch_add(1, std::memory_order_relaxed);
            bool openEdge = false;
            verdict.polys = Across(mesh, ref, maxPolys, openEdge);
            verdict.island = IslandAcross(verdict.polys, openEdge, maxPolys);
            break;
        }
    }
    return verdict;
}

struct Candidate
{
    dtPolyRef ref;
    float sq;
    float c[3];
};

// Polys of the ground filter in the box around point, nearest first (3D).
std::vector<Candidate> GroundCandidates(dtNavMeshQuery const* query, float const* point, float boxXY, float boxZ)
{
    std::vector<Candidate> candidates;
    dtQueryFilter ground;
    ground.setIncludeFlags(GroundInclude());
    ground.setExcludeFlags(GroundExclude());
    float extents[3] = { boxXY, boxZ, boxXY };
    dtPolyRef polys[512];
    int count = 0;
    if (dtStatusFailed(query->queryPolygons(point, extents, &ground, polys, &count, 512)))
        return candidates;
    for (int i = 0; i < count; ++i)
    {
        float closest[3];
        bool over = false;
        if (dtStatusFailed(query->closestPointOnPoly(polys[i], point, closest, &over)))
            continue;
        float const dx = closest[2] - point[2], dy = closest[0] - point[0], dz = closest[1] - point[1];
        candidates.push_back({ polys[i], dx * dx + dy * dy + dz * dz, { closest[0], closest[1], closest[2] } });
    }
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.sq < b.sq; });
    return candidates;
}

// The server's own snap of a spot (PathInfo::FindWalkPoly, extents 5 / 10 / 5, not above +3 yd), label filter.
dtPolyRef ServerPoly(dtNavMeshQuery const* query, float const* point)
{
    dtQueryFilter server;
    server.setIncludeFlags(LabelInclude());
    server.setExcludeFlags(0);
    float closest[3];
    return PathInfo::FindWalkPoly(query, point, server, closest);
}
}

ArrivalCheck ai::island::CheckArrival(uint32 mapId, float& x, float& y, float& z, uint32 maxPolys, float snapDz)
{
    ArrivalCheck check;
    check.snap = ArrivalSnap::None;

    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    dtNavMesh const* mesh = mmap->GetNavMesh(mapId);
    dtNavMeshQuery const* query = mmap->GetNavMeshQuery(mapId);
    if (!mesh || !query)
        return check;

    Counters& counters = SharedCounters();
    counters.checked.fetch_add(1, std::memory_order_relaxed);

    float point[3];
    ToDetour(x, y, z, point);
    Verdict const here = Judge(mesh, mapId, ServerPoly(query, point), maxPolys);
    if (!here.known || !here.island)
        return check;

    counters.island.fetch_add(1, std::memory_order_relaxed);
    check.polys = here.polys;
    for (Candidate const& candidate : GroundCandidates(query, point, SnapXY, snapDz))
    {
        float const dx = candidate.c[2] - point[2], dy = candidate.c[0] - point[0], dz = candidate.c[1] - point[1];
        if (!SnapCandidateOk(std::sqrt(dx * dx + dy * dy), dz, snapDz))
            continue;
        Verdict const there = Judge(mesh, mapId, candidate.ref, maxPolys);
        if (!there.known || there.island)
            continue;
        x = candidate.c[2];
        y = candidate.c[0];
        z = candidate.c[1];
        check.snap = ArrivalSnap::IslandToMain;
        check.distance = std::sqrt(candidate.sq);
        counters.snapped.fetch_add(1, std::memory_order_relaxed);
        return check;
    }

    check.snap = ArrivalSnap::IslandNoMain;
    counters.noMain.fetch_add(1, std::memory_order_relaxed);
    return check;
}

StepOff ai::island::FindStepOff(uint32 mapId, float x, float y, float z, uint32 maxPolys, float maxYards)
{
    StepOff result;
    if (!(maxYards > 0.0f))
        return result;

    MMAP::MMapManager* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    dtNavMesh const* mesh = mmap->GetNavMesh(mapId);
    dtNavMeshQuery const* query = mmap->GetNavMeshQuery(mapId);
    if (!mesh || !query)
        return result;

    float point[3];
    ToDetour(x, y, z, point);
    Verdict const here = Judge(mesh, mapId, ServerPoly(query, point), maxPolys);
    if (!here.known || !here.island)
        return result;

    result.onIsland = true;
    result.polys = here.polys;
    for (Candidate const& candidate : GroundCandidates(query, point, maxYards, StepOffDz))
    {
        float const distance = std::sqrt(candidate.sq), dz = candidate.c[1] - point[1];
        if (!StepOffTargetOk(distance, dz, maxYards))
            continue;
        Verdict const there = Judge(mesh, mapId, candidate.ref, maxPolys);
        if (!there.known || there.island)
            continue;
        result.found = true;
        result.x = candidate.c[2];
        result.y = candidate.c[0];
        result.z = candidate.c[1];
        result.distance = distance;
        result.dz = dz;
        break;
    }
    return result;
}

uint32 ai::island::LabelledTiles()
{
    return uint32(Cache().Size());
}
