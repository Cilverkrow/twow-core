// twow-repo#541 part C (Continents.Layout): the generated cell tables decode to whole continents, carry the
// region ids of their layout (first..last, each present) and put known places into the expected regions.
// Links only the generated data; no server, map files or database.
#include "ContinentRegionTables.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace
{
int failures = 0;

void Require(bool ok, char const* what, unsigned a = 0, unsigned b = 0)
{
    if (!ok)
    {
        std::printf("FAIL: %s (%u, %u)\n", what, a, b);
        ++failures;
    }
}

struct Place
{
    std::uint32_t layout;
    std::uint32_t mapId;
    float x;
    float y;
    std::uint32_t region;
    char const* name;
};

// Region ids: Eastern Kingdoms from 1, Kalimdor from 11, in the order of the OB-50 draft.
Place const kPlaces[] = {
    { 16, 0, -9464.0f, 62.0f, 5, "Goldshire (E5 Elwynn)" },
    { 16, 0, -8913.0f, 554.0f, 8, "Stormwind (E8 cities)" },
    { 16, 0, -5603.0f, -482.0f, 3, "Kharanos (E3 Dun Morogh)" },
    { 16, 0, 2269.0f, 244.0f, 1, "Brill (E1)" },
    { 16, 0, -10646.0f, 1053.0f, 6, "Sentinel Hill (E6)" },
    { 16, 0, -14297.0f, 530.0f, 7, "Booty Bay (E7)" },
    { 16, 1, -452.0f, -2650.0f, 14, "Crossroads (K4 Barrens north)" },
    { 16, 1, -2380.0f, -1880.0f, 15, "Camp Taurajo (K5 Barrens south)" },
    { 16, 1, 311.0f, -4724.0f, 13, "Razor Hill (K3 Durotar)" },
    { 16, 1, -602.0f, -4262.0f, 13, "Valley of Trials (K3)" },
    { 16, 1, 1629.0f, -4373.0f, 18, "Orgrimmar (K8 cities)" },
    { 16, 1, -1277.0f, 124.0f, 18, "Thunder Bluff (K8 cities)" },
    { 16, 1, -2345.0f, -366.0f, 16, "Bloodhoof Village (K6 Mulgore)" },
    { 16, 1, 9947.0f, 2482.0f, 18, "Darnassus (K8 cities)" },
    { 16, 1, 9889.0f, 977.0f, 11, "Dolanaar (K1)" },
    { 16, 1, -7180.0f, -3773.0f, 17, "Gadgetzan (K7)" },
    { 14, 0, -8913.0f, 554.0f, 5, "Stormwind joins Elwynn (14)" },
    { 14, 1, 1629.0f, -4373.0f, 13, "Orgrimmar joins Durotar (14)" },
    { 14, 1, -1277.0f, 124.0f, 16, "Thunder Bluff joins Mulgore (14)" },
    { 14, 1, 9947.0f, 2482.0f, 11, "Darnassus joins the night elves (14)" },
    { 18, 1, -452.0f, -2650.0f, 14, "Crossroads (18: Barrens north-west)" },
    { 18, 1, -956.0f, -3754.0f, 19, "Ratchet (18: Barrens north-east)" },
    { 18, 1, -602.0f, -4262.0f, 20, "Valley of Trials on its own (18)" },
    { 18, 1, 311.0f, -4724.0f, 13, "Razor Hill stays in Durotar (18)" },
};
}

int main()
{
    using namespace continent_regions;
    struct Expect { std::uint32_t layout; std::uint32_t last[2]; };
    Expect const expects[] = { { 14, { 7, 17 } }, { 16, { 8, 18 } }, { 18, { 8, 20 } } };
    std::uint32_t const first[2] = { 1, 11 };

    Require(Find(0, 0) == nullptr && Find(0, 1) == nullptr, "layout 0 has no table (legacy polygons)");
    Require(Find(15, 0) == nullptr, "no table for an unknown layout");

    for (Expect const& e : expects)
    {
        for (std::uint32_t mapId = 0; mapId < 2; ++mapId)
        {
            LayoutTable const* table = Find(e.layout, mapId);
            Require(table != nullptr, "table present", e.layout, mapId);
            if (!table)
                continue;
            std::vector<std::uint8_t> cells;
            Require(Decode(*table, cells), "table covers the continent", e.layout, mapId);
            Require(cells.size() == std::size_t(kCells) * kCells, "512 x 512 cells", e.layout, mapId);
            Require(LastRegion(cells) == e.last[mapId], "last region id of the layout", e.layout, mapId);
            std::set<std::uint32_t> seen;
            std::size_t border = 0;
            for (std::uint8_t const cell : cells)
            {
                std::uint32_t const region = cell & kRegionMask;
                Require(region >= first[mapId] && region <= e.last[mapId], "region id in the continent's range", e.layout, region);
                seen.insert(region);
                if (cell & kTransitionBit)
                    ++border;
            }
            Require(seen.size() == e.last[mapId] - first[mapId] + 1, "every region of the layout has cells", e.layout, mapId);
            Require(border > 0 && border < cells.size() / 10, "border cells exist and stay a small share", e.layout, unsigned(border));
        }
    }

    for (Place const& p : kPlaces)
    {
        LayoutTable const* table = Find(p.layout, p.mapId);
        std::vector<std::uint8_t> cells;
        if (!table || !Decode(*table, cells))
        {
            Require(false, p.name);
            continue;
        }
        std::uint32_t const region = cells[CellIndex(p.x, p.y)] & kRegionMask;
        if (region != p.region)
            std::printf("place %s: region %u, expected %u\n", p.name, region, p.region);
        Require(region == p.region, p.name, region, p.region);
    }

    // Negative: a table that misses one cell or names region 0 is refused.
    Run const shortRuns[] = { { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 3, 1 } };
    LayoutTable const shortTable = { 99, 0, shortRuns, 5 };
    std::vector<std::uint8_t> cells;
    Require(!Decode(shortTable, cells), "a table one cell short is refused");
    Run const zeroRuns[] = { { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 65535, 0 }, { 4, 1 } };
    LayoutTable const zeroTable = { 99, 0, zeroRuns, 5 };
    Require(!Decode(zeroTable, cells), "region 0 is refused");
    Run const fullRuns[] = { { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 4, 1 } };
    LayoutTable const fullTable = { 99, 0, fullRuns, 5 };
    Require(Decode(fullTable, cells) && LastRegion(cells) == 1, "a single-region table decodes");
    Require((cells[0] & kTransitionBit) == 0, "no border inside one region");
    Require(CellAxis(1.0e9f) == 0 && CellAxis(-1.0e9f) == kCells - 1, "coordinates clamp to the continent");

    if (failures)
    {
        std::printf("CONTINENT_LAYOUT_541_TEST=FAIL (%d)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("CONTINENT_LAYOUT_541_TEST=PASS\n");
    return EXIT_SUCCESS;
}
