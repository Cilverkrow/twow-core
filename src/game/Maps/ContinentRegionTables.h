/*
 * twow-repo#541 part C (Continents.Layout): continent regions from a cell table instead of polygons.
 *
 * One region id per 66.67-yard cell (8 x 8 cells per map tile, 512 x 512 per continent), stored run-length
 * encoded in ContinentRegionTables.cpp, which tools/continent_regions/gen_continent_regions.py generates from
 * the server map files (zone of every cell) and the zone -> region lists of the OB-50 draft. Layout 0 keeps the
 * legacy polygons in MapManager::GetContinentInstanceId and has no table.
 *
 * Header only apart from the generated data, so t/continent_layout_541_test.cpp can check the tables without
 * the game library.
 */

#ifndef TWOW_CONTINENT_REGION_TABLES_H
#define TWOW_CONTINENT_REGION_TABLES_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace continent_regions
{
    struct Run
    {
        std::uint16_t count;
        std::uint8_t region;
    };

    struct LayoutTable
    {
        std::uint32_t layout;
        std::uint32_t mapId;
        Run const* runs;
        std::size_t runCount;
    };

    extern LayoutTable const kTables[];
    extern std::size_t const kTableCount;

    constexpr std::uint32_t kCells = 512;
    constexpr float kTile = 533.33333f;
    // Set on a cell with a neighbour (8 around) of another region: a fight there does not switch the region.
    constexpr std::uint8_t kTransitionBit = 0x80;
    constexpr std::uint8_t kRegionMask = 0x7F;

    // Cell along one axis; the axes fall with the world coordinate like the map tiles (32 - v / tile).
    inline std::uint32_t CellAxis(float v)
    {
        float const c = (32.0f - v / kTile) * 8.0f;
        if (!(c > 0.0f))
            return 0;
        if (c >= float(kCells))
            return kCells - 1;
        return std::uint32_t(c);
    }

    inline std::size_t CellIndex(float x, float y)
    {
        return std::size_t(CellAxis(x)) * kCells + CellAxis(y);
    }

    inline LayoutTable const* Find(std::uint32_t layout, std::uint32_t mapId)
    {
        for (std::size_t i = 0; i < kTableCount; ++i)
            if (kTables[i].layout == layout && kTables[i].mapId == mapId)
                return &kTables[i];
        return nullptr;
    }

    // Region ids per cell (cx * kCells + cy) with kTransitionBit on border cells. False if the runs do not
    // cover the continent exactly or a run holds region 0.
    inline bool Decode(LayoutTable const& table, std::vector<std::uint8_t>& cells)
    {
        cells.assign(std::size_t(kCells) * kCells, 0);
        std::size_t pos = 0;
        for (std::size_t i = 0; i < table.runCount; ++i)
        {
            Run const& run = table.runs[i];
            if (run.region == 0 || (run.region & kTransitionBit) || pos + run.count > cells.size())
                return false;
            for (std::uint32_t k = 0; k < run.count; ++k)
                cells[pos++] = run.region;
        }
        if (pos != cells.size())
            return false;

        std::vector<std::uint8_t> const plain = cells;
        for (std::uint32_t cx = 0; cx < kCells; ++cx)
        {
            for (std::uint32_t cy = 0; cy < kCells; ++cy)
            {
                std::uint8_t const own = plain[std::size_t(cx) * kCells + cy];
                bool border = false;
                for (int dx = -1; dx <= 1 && !border; ++dx)
                {
                    for (int dy = -1; dy <= 1 && !border; ++dy)
                    {
                        int const nx = int(cx) + dx;
                        int const ny = int(cy) + dy;
                        if (nx < 0 || ny < 0 || nx >= int(kCells) || ny >= int(kCells))
                            continue;
                        border = plain[std::size_t(nx) * kCells + std::size_t(ny)] != own;
                    }
                }
                if (border)
                    cells[std::size_t(cx) * kCells + cy] |= kTransitionBit;
            }
        }
        return true;
    }

    // Highest region id of a decoded continent (the regions run from the continent's first id up to it).
    inline std::uint32_t LastRegion(std::vector<std::uint8_t> const& cells)
    {
        std::uint32_t last = 0;
        for (std::uint8_t const cell : cells)
            if (std::uint32_t(cell & kRegionMask) > last)
                last = cell & kRegionMask;
        return last;
    }
}

#endif
