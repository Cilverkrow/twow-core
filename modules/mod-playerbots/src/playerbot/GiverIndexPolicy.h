#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

// A3 step 3 planner S5 F1-IX (twow-repo#726; IX-ENTWURF-v0.2, OB-60 pre-review 4376ef95, OB-10 OK 773531cd;
// AiPlayerbot.Planner.GiverIndex, default 0): the giver index. A query collects a superset of the givers whose distance can be
// within the radius, as positions in the walk order of TravelMgr's giver fetch (ordinals); TravelMgr sorts them and runs the
// original checks on them, so the fetch returns the same list in the same order while it looks at far fewer givers.
// Per map, a uniform bucket grid over the map's coordinates; a giver's map-square box (the very min/max that
// WorldPointSquare::sqOutDistance clamps against) is registered in every bucket it meets, widened by kEps; a query probes the
// buckets of the square [x - r - kEps, x + r + kEps] x [y - r - kEps, y + r + kEps]. A box that misses that square is more than r
// away (its 2D box distance, which bounds the original distance from below), so the original drops it too. A box over more
// than kWideSpan buckets, or with a coordinate that is not finite, goes to the map's wide list (a candidate whenever its map is
// queried). The header names no game type: t/giver_index_policy_tests.cpp compiles it standalone.
namespace ai::giver_index
{
constexpr float kHalf = 17066.666f;      // the map half size (32 grids of 533.3333 yd)
constexpr float kCell = 533.3333f;       // one map grid
constexpr int kCells = 64;               // buckets per axis
constexpr float kEps = 1.0f;             // yards; far above the float error of the box distance at |v| <= 17067 (ulp <= 0.002)
constexpr std::uint32_t kWideSpan = 16;  // a box over more buckets goes to the wide list

// The bucket of one coordinate: monotone in v (a larger v never has a smaller bucket), clamped to 0..kCells-1; NaN is 0.
inline int Bucket(float v, float cell = kCell)
{
    float const t = (v + kHalf) / cell;
    if (!(t >= 0.0f))
        return 0;
    if (t >= float(kCells))
        return kCells - 1;
    return int(t);
}

// An inclusive bucket rectangle.
struct Range
{
    int x0 = 0;
    int x1 = -1;
    int y0 = 0;
    int y1 = -1;
};

// The buckets a box meets, widened by kEps (the same Bucket as the query).
inline Range BoxRange(float minX, float minY, float maxX, float maxY, float cell = kCell)
{
    return Range{ Bucket(minX - kEps, cell), Bucket(maxX + kEps, cell), Bucket(minY - kEps, cell), Bucket(maxY + kEps, cell) };
}

// The buckets of the query square around (x, y) with radius r, widened by kEps.
inline Range QueryRange(float x, float y, float r, float cell = kCell)
{
    return Range{ Bucket(x - r - kEps, cell), Bucket(x + r + kEps, cell), Bucket(y - r - kEps, cell), Bucket(y + r + kEps, cell) };
}

// One map-square box of the giver at walk position ord.
struct Box
{
    std::uint32_t map = 0;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    std::uint32_t ord = 0;
};

// The bucket grids of every map with a giver box; immutable after Build.
class Grid
{
public:
    void Build(std::vector<Box> const& boxes, float cell = kCell)
    {
        cell_ = cell;
        maps_.clear();
        boxes_ = std::uint32_t(boxes.size());
        wide_ = 0;
        cells_ = 0;
        std::vector<std::uint32_t> ids;
        for (Box const& b : boxes)
            ids.push_back(b.map);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        maps_.resize(ids.size());
        for (std::size_t m = 0; m < ids.size(); ++m)
        {
            maps_[m].map = ids[m];
            maps_[m].start.assign(std::size_t(kCells) * kCells + 1, 0);
        }
        // two passes (count, then fill): one ordinal list per map, cell c owns ords[start[c] .. start[c + 1])
        std::vector<Range> ranges(boxes.size());
        std::vector<bool> wide(boxes.size(), false);
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            Box const& b = boxes[i];
            MapCells& mc = *FindMut(b.map);
            bool const finite = std::isfinite(b.minX) && std::isfinite(b.minY) && std::isfinite(b.maxX) && std::isfinite(b.maxY);
            Range const r = BoxRange(b.minX, b.minY, b.maxX, b.maxY, cell_);
            std::uint64_t const span = (r.x1 >= r.x0 && r.y1 >= r.y0) ?
                std::uint64_t(r.x1 - r.x0 + 1) * std::uint64_t(r.y1 - r.y0 + 1) : 0;
            if (!finite || span == 0 || span > kWideSpan)
            {
                wide[i] = true;
                mc.wide.push_back(b.ord);
                ++wide_;
                continue;
            }
            ranges[i] = r;
            for (int y = r.y0; y <= r.y1; ++y)
                for (int x = r.x0; x <= r.x1; ++x)
                    ++mc.start[std::size_t(y) * kCells + std::size_t(x) + 1];
        }
        for (MapCells& mc : maps_)
        {
            for (std::size_t c = 1; c < mc.start.size(); ++c)
            {
                if (mc.start[c])
                    ++cells_;
                mc.start[c] += mc.start[c - 1];
            }
            mc.ords.assign(mc.start.back(), 0);
        }
        std::vector<std::uint32_t> fill;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            if (wide[i])
                continue;
            Box const& b = boxes[i];
            MapCells& mc = *FindMut(b.map);
            if (mc.fill.empty())
                mc.fill.assign(mc.start.begin(), mc.start.end() - 1);
            Range const& r = ranges[i];
            for (int y = r.y0; y <= r.y1; ++y)
                for (int x = r.x0; x <= r.x1; ++x)
                    mc.ords[mc.fill[std::size_t(y) * kCells + std::size_t(x)]++] = b.ord;
        }
        for (MapCells& mc : maps_)
            std::vector<std::uint32_t>().swap(mc.fill);
        mapIds_ = ids;
    }

    // Calls take(ord) for every ordinal registered in a bucket of the query square around (x, y) with radius r on map; returns the
    // buckets probed (0 when the map has no giver box). An ordinal can come more than once (the caller dedupes).
    template <class Take>
    std::uint32_t Collect(std::uint32_t map, float x, float y, float r, Take&& take) const
    {
        MapCells const* mc = Find(map);
        if (!mc)
            return 0;
        Range const q = QueryRange(x, y, r, cell_);
        std::uint32_t probed = 0;
        for (int by = q.y0; by <= q.y1; ++by)
            for (int bx = q.x0; bx <= q.x1; ++bx)
            {
                std::size_t const c = std::size_t(by) * kCells + std::size_t(bx);
                ++probed;
                for (std::uint32_t k = mc->start[c]; k < mc->start[c + 1]; ++k)
                    take(mc->ords[k]);
            }
        return probed;
    }

    // Calls take(ord) for every wide-list ordinal of map; returns how many.
    template <class Take>
    std::uint32_t Wide(std::uint32_t map, Take&& take) const
    {
        MapCells const* mc = Find(map);
        if (!mc)
            return 0;
        for (std::uint32_t ord : mc->wide)
            take(ord);
        return std::uint32_t(mc->wide.size());
    }

    std::vector<std::uint32_t> const& Maps() const { return mapIds_; }
    std::uint32_t BoxCount() const { return boxes_; }
    std::uint32_t WideCount() const { return wide_; }
    std::uint32_t CellCount() const { return cells_; }

private:
    struct MapCells
    {
        std::uint32_t map = 0;
        std::vector<std::uint32_t> start;   // kCells * kCells + 1 offsets into ords
        std::vector<std::uint32_t> ords;
        std::vector<std::uint32_t> wide;
        std::vector<std::uint32_t> fill;    // build only
    };

    MapCells const* Find(std::uint32_t map) const
    {
        auto const it = std::lower_bound(maps_.begin(), maps_.end(), map,
            [](MapCells const& mc, std::uint32_t id) { return mc.map < id; });
        return (it != maps_.end() && it->map == map) ? &*it : nullptr;
    }
    MapCells* FindMut(std::uint32_t map) { return const_cast<MapCells*>(Find(map)); }

    float cell_ = kCell;
    std::vector<MapCells> maps_;            // sorted by map id
    std::vector<std::uint32_t> mapIds_;
    std::uint32_t boxes_ = 0;
    std::uint32_t wide_ = 0;
    std::uint32_t cells_ = 0;
};

// The dedupe of one query by walk position (a pointer that sits twice in the walk keeps both positions). Begin sizes the marks
// from the built N and starts a new generation; when the generation wraps to 0 the marks are cleared once and it restarts at 1.
class Stamp
{
public:
    void Begin(std::size_t n)
    {
        if (mark_.size() != n)
        {
            mark_.assign(n, 0);
            gen_ = 0;
        }
        if (++gen_ == 0)
        {
            std::fill(mark_.begin(), mark_.end(), 0u);
            gen_ = 1;
        }
    }
    // True the first time ord is taken in this generation; false for a repeat or an ordinal outside the built N.
    bool Take(std::uint32_t ord)
    {
        if (ord >= mark_.size() || mark_[ord] == gen_)
            return false;
        mark_[ord] = gen_;
        return true;
    }
    std::uint32_t Generation() const { return gen_; }
    void SetGenerationForTest(std::uint32_t gen) { gen_ = gen; }

private:
    std::vector<std::uint32_t> mark_;
    std::uint32_t gen_ = 0;
};

// The per-fetch counts of the index path (TravelMgr.h: the index switch points at them for one planner giver fetch; a non-null
// pointer is the switch). timed is set by the caller (Planner.TimeTrace); used = the index ran; miss = no index (key 0 at load,
// not built, or a position that is not finite); stale = the giver map changed since the build; both fall back to the walk.
struct Counts
{
    bool timed = false;
    bool used = false;
    bool miss = false;
    bool stale = false;
    std::uint32_t all = 0;        // givers in the index (N)
    std::uint32_t maps = 0;       // maps whose buckets were probed
    std::uint32_t portals = 0;    // portals within the radius
    std::uint32_t buckets = 0;    // buckets probed
    std::uint32_t wide = 0;       // wide-list ordinals taken
    std::uint32_t cand = 0;       // candidates handed to the original checks
    std::uint32_t portalUs = 0;   // the portal lists of the other maps
    std::uint32_t collectUs = 0;  // bucket probes and dedupe
    std::uint32_t sortUs = 0;     // the ordinal sort and the candidate list
    std::uint32_t appendUs = 0;   // the original checks on the candidates
};

// Microseconds between two steady clock points, clamped to 0..UINT32_MAX.
inline std::uint32_t UsBetween(std::chrono::steady_clock::time_point from, std::chrono::steady_clock::time_point to)
{
    if (to <= from)
        return 0;
    long long const us = (long long)std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
    return us > 0xFFFFFFFFll ? 0xFFFFFFFFu : std::uint32_t(us);
}

// The [PlanIndex] verify line (AiPlayerbot.Planner.GiverIndexVerify = N: every N-th index fetch also runs the original walk and
// compares the full vectors). first_idx = first index that differs, -1 when equal.
constexpr std::size_t kVerifyBuf = 256;

inline std::size_t FormatVerifyLine(char* buf, std::size_t n, std::uint32_t guid, std::uint32_t nOrig, std::uint32_t nIx,
    long long firstIdx, Counts const& c)
{
    if (!buf || n == 0)
        return 0;
    int const w = std::snprintf(buf, n, "[PlanIndex] verify bot=%u n_orig=%u n_ix=%u mismatch=%u first_idx=%lld cand=%u maps=%u "
        "portals=%u buckets=%u wide=%u", guid, nOrig, nIx, firstIdx >= 0 ? 1u : 0u, firstIdx, c.cand, c.maps, c.portals, c.buckets,
        c.wide);
    if (w < 0)
        return 0;
    return std::size_t(w) < n ? std::size_t(w) : n - 1;
}
}
