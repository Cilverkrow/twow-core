#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <vector>

namespace ai::danger_map
{
// #307: the per-bot rules (#123, #138) only react after each bot has died
// itself, so every low-level bot paid for the same wolf pack or camp (post-reset:
// one bot 54 deaths to a Gray Forest Wolf pack, another 22 to Riverpaw level 10).
// Bot deaths are pooled per map cell; a roster bot skips a destination whose
// cell, neighbours or straight route contain a recent cluster of deaths caused
// by mobs clearly above its level.
struct Death
{
    std::uint32_t time = 0;         // seconds
    std::uint32_t victim = 0;       // bot guid (low)
    std::uint8_t killerLevel = 0;
    std::uint8_t victimLevel = 0;
};

struct Params
{
    float cellSize = 100.0f;
    std::uint32_t windowSeconds = 7200;
    std::uint32_t minDeaths = 3;
    std::uint32_t levelMargin = 3;
    std::uint32_t lineSamples = 20;
};

struct Cell
{
    std::uint32_t map = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator<(Cell const& other) const
    {
        return std::tie(map, x, y) < std::tie(other.map, other.x, other.y);
    }
    bool operator==(Cell const& other) const
    {
        return map == other.map && x == other.x && y == other.y;
    }
};

inline Cell CellOf(std::uint32_t map, float x, float y, float cellSize)
{
    return { map, std::int32_t(std::floor(x / cellSize)), std::int32_t(std::floor(y / cellSize)) };
}

inline bool IsExpired(Death const& death, std::uint32_t now, std::uint32_t windowSeconds)
{
    return death.time + windowSeconds <= now;
}

// Deaths by different bots count, so one bot dying in a loop (already covered
// by #138) does not mark a cell for everyone. Returns the highest counted
// killer level, 0 when the cell is not dangerous for botLevel.
inline std::uint32_t DangerousKillerLevel(std::vector<Death> const& deaths, std::uint32_t now, std::uint32_t botLevel,
    Params const& params)
{
    if (params.minDeaths == 0)
        return 0;

    std::vector<std::uint32_t> victims;
    std::uint32_t worst = 0;
    for (Death const& death : deaths)
    {
        if (IsExpired(death, now, params.windowSeconds) || death.killerLevel < botLevel + params.levelMargin)
            continue;

        if (std::find(victims.begin(), victims.end(), death.victim) == victims.end())
            victims.push_back(death.victim);
        worst = std::max<std::uint32_t>(worst, death.killerLevel);
    }

    return victims.size() >= params.minDeaths ? worst : 0;
}

// Cells on the straight line from -> to, one sample per cell size, at most
// maxSamples, without duplicates and without the start cell and its neighbours
// (a bot already standing in danger is the flee logic's job, and excluding its
// own surroundings keeps it from being left without any destination).
inline std::vector<Cell> LineCells(std::uint32_t map, float fromX, float fromY, float toX, float toY, Params const& params)
{
    std::vector<Cell> cells;
    Cell const start = CellOf(map, fromX, fromY, params.cellSize);
    float const dx = toX - fromX, dy = toY - fromY;
    float const length = std::sqrt(dx * dx + dy * dy);
    std::uint32_t const steps = std::min<std::uint32_t>(params.lineSamples, std::uint32_t(length / params.cellSize));

    for (std::uint32_t i = 1; i <= steps; ++i)
    {
        float const f = float(i) / float(steps);
        Cell const cell = CellOf(map, fromX + dx * f, fromY + dy * f, params.cellSize);
        if (std::abs(cell.x - start.x) <= 1 && std::abs(cell.y - start.y) <= 1)
            continue;
        if (std::find(cells.begin(), cells.end(), cell) == cells.end())
            cells.push_back(cell);
    }

    return cells;
}

// Shared by all bots. Map updates run in thread pools and destination search
// runs outside the world thread, so every access is locked; writes (deaths)
// are rare, reads are shared.
class DangerMap
{
public:
    static constexpr std::size_t MaxDeathsPerCell = 16;
    static constexpr std::size_t MaxCells = 4096;

    void Record(std::uint32_t map, float x, float y, Death const& death, Params const& params)
    {
        std::unique_lock lock(mutex);
        std::vector<Death>& deaths = cells[CellOf(map, x, y, params.cellSize)];
        deaths.erase(std::remove_if(deaths.begin(), deaths.end(),
            [&](Death const& d) { return IsExpired(d, death.time, params.windowSeconds); }), deaths.end());
        if (deaths.size() >= MaxDeathsPerCell)
            deaths.erase(deaths.begin());
        deaths.push_back(death);

        if (cells.size() > MaxCells)
            PruneLocked(death.time, params);
    }

    struct Hit
    {
        std::uint32_t cells = 0;
        std::uint32_t worstKillerLevel = 0;
    };

    // Destination cell with its 8 neighbours plus the straight route.
    Hit Query(std::uint32_t map, float fromX, float fromY, float toX, float toY, std::uint32_t botLevel,
        std::uint32_t now, Params const& params) const
    {
        std::vector<Cell> check = LineCells(map, fromX, fromY, toX, toY, params);
        Cell const start = CellOf(map, fromX, fromY, params.cellSize);
        Cell const target = CellOf(map, toX, toY, params.cellSize);
        for (std::int32_t ox = -1; ox <= 1; ++ox)
            for (std::int32_t oy = -1; oy <= 1; ++oy)
            {
                Cell const cell{ map, target.x + ox, target.y + oy };
                if (std::abs(cell.x - start.x) <= 1 && std::abs(cell.y - start.y) <= 1)
                    continue;
                if (std::find(check.begin(), check.end(), cell) == check.end())
                    check.push_back(cell);
            }

        Hit hit;
        std::shared_lock lock(mutex);
        if (cells.empty())
            return hit;

        for (Cell const& cell : check)
        {
            auto const it = cells.find(cell);
            if (it == cells.end())
                continue;

            if (std::uint32_t const level = DangerousKillerLevel(it->second, now, botLevel, params))
            {
                ++hit.cells;
                hit.worstKillerLevel = std::max(hit.worstKillerLevel, level);
            }
        }

        return hit;
    }

    // Drops expired deaths; returns {cells, deaths} after pruning.
    std::pair<std::size_t, std::size_t> Prune(std::uint32_t now, Params const& params)
    {
        std::unique_lock lock(mutex);
        return PruneLocked(now, params);
    }

private:
    std::pair<std::size_t, std::size_t> PruneLocked(std::uint32_t now, Params const& params)
    {
        std::size_t total = 0;
        for (auto it = cells.begin(); it != cells.end();)
        {
            auto& deaths = it->second;
            deaths.erase(std::remove_if(deaths.begin(), deaths.end(),
                [&](Death const& d) { return IsExpired(d, now, params.windowSeconds); }), deaths.end());
            if (deaths.empty())
                it = cells.erase(it);
            else
            {
                total += deaths.size();
                ++it;
            }
        }

        // Still over the bound only if thousands of cells saw deaths within the
        // window; start over rather than grow without limit.
        if (cells.size() > MaxCells)
        {
            cells.clear();
            total = 0;
        }

        return { cells.size(), total };
    }

    mutable std::shared_mutex mutex;
    std::map<Cell, std::vector<Death>> cells;
};

inline DangerMap& Instance()
{
    static DangerMap map;
    return map;
}
}
