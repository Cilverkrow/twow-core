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

// Region ids per layout: 14/16/18 Eastern Kingdoms from 1, Kalimdor from 11, in the order of the OB-50 draft;
// 20 Eastern Kingdoms 1..12, Kalimdor 21..32 (owner decision 10.10.2026).
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
    // Layout 20 (owner 10.10.2026): every capital its own region, ids 1..12 and 21..32. Ironforge and Undercity
    // are cell rectangles; just outside the Ironforge gate is Dun Morogh. Alah'Thalas: a cell inside the
    // Stormwind/Alah'Thalas city region of layout 16 (see the generator, KNOWN20).
    { 20, 0, -8913.0f, 554.0f, 1, "Stormwind (20)" },
    { 20, 0, -4838.0f, -1186.0f, 2, "Ironforge Great Forge (20: rectangle)" },
    { 20, 0, -5030.0f, -1300.0f, 2, "Ironforge rectangle, corner cell x min / y min (20)" },
    { 20, 0, -4570.0f, -900.0f, 2, "Ironforge rectangle, corner cell x max / y max (20)" },
    { 20, 0, 1595.0f, 231.0f, 3, "Undercity bank (20: rectangle)" },
    { 20, 0, 1240.0f, 40.0f, 3, "Undercity rectangle, corner cell x min / y min (20)" },
    { 20, 0, 1860.0f, 500.0f, 3, "Undercity rectangle, corner cell x max / y max (20)" },
    { 20, 0, 4966.0f, -2966.0f, 4, "Alah'Thalas (20)" },
    { 20, 0, -9464.0f, 62.0f, 5, "Goldshire (20: Elwynn)" },
    { 20, 0, -5603.0f, -482.0f, 6, "Kharanos (20: Dun Morogh)" },
    { 20, 0, -5100.0f, -800.0f, 6, "outside the Ironforge gate (20: Dun Morogh)" },
    { 20, 0, 2269.0f, 244.0f, 7, "Brill (20: Tirisfal)" },
    { 20, 0, -14297.0f, 530.0f, 10, "Booty Bay (20: Stranglethorn)" },
    { 20, 1, 1629.0f, -4373.0f, 21, "Orgrimmar (20)" },
    { 20, 1, -1277.0f, 124.0f, 22, "Thunder Bluff (20)" },
    { 20, 1, 9947.0f, 2482.0f, 23, "Darnassus (20)" },
    { 20, 1, 311.0f, -4724.0f, 24, "Razor Hill (20: Durotar)" },
    { 20, 1, -602.0f, -4262.0f, 25, "Valley of Trials (20)" },
    { 20, 1, -2345.0f, -366.0f, 26, "Bloodhoof Village (20: Mulgore)" },
    { 20, 1, 9889.0f, 977.0f, 27, "Dolanaar (20: Teldrassil)" },
    { 20, 1, -452.0f, -2650.0f, 28, "Crossroads (20: Barrens north)" },
    { 20, 1, -956.0f, -3754.0f, 28, "Ratchet (20: Barrens north)" },
    { 20, 1, -2380.0f, -1880.0f, 29, "Camp Taurajo (20: Barrens south)" },
    { 20, 1, -7180.0f, -3773.0f, 31, "Gadgetzan (20)" },
};
}

int main()
{
    using namespace continent_regions;
    struct Expect { std::uint32_t layout; std::uint32_t first[2]; std::uint32_t last[2]; };
    Expect const expects[] = {
        { 14, { 1, 11 }, { 7, 17 } },
        { 16, { 1, 11 }, { 8, 18 } },
        { 18, { 1, 11 }, { 8, 20 } },
        { 20, { 1, 21 }, { 12, 32 } },
    };

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
            Require(FirstRegion(cells) == e.first[mapId], "first region id of the layout", e.layout, mapId);
            Require(LastRegion(cells) == e.last[mapId], "last region id of the layout", e.layout, mapId);
            Require(RegionsContiguous(cells, e.first[mapId], e.last[mapId]), "region ids contiguous", e.layout, mapId);
            std::set<std::uint32_t> seen;
            std::size_t border = 0;
            for (std::uint8_t const cell : cells)
            {
                std::uint32_t const region = cell & kRegionMask;
                Require(region >= e.first[mapId] && region <= e.last[mapId], "region id in the continent's range", e.layout, region);
                seen.insert(region);
                if (cell & kTransitionBit)
                    ++border;
            }
            Require(seen.size() == e.last[mapId] - e.first[mapId] + 1, "every region of the layout has cells", e.layout, mapId);
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
    Require(FirstRegion(cells) == 1 && RegionsContiguous(cells, 1, 1), "a single region is contiguous");
    Require(!RegionsContiguous(cells, 1, 2), "a missing id in first..last is refused");
    Require(!RegionsContiguous(cells, 0, 1) && !RegionsContiguous(cells, 2, 1), "first 0 or first > last is refused");
    Run const gapRuns[] = { { 65535, 1 }, { 65535, 1 }, { 65535, 1 }, { 65535, 3 }, { 4, 3 } };
    LayoutTable const gapTable = { 99, 0, gapRuns, 5 };
    Require(Decode(gapTable, cells) && FirstRegion(cells) == 1 && LastRegion(cells) == 3, "first/last of a table with a gap");
    Require(!RegionsContiguous(cells, 1, 3), "a gap in the region ids is refused");
    Require(!RegionsContiguous(cells, 1, 1), "a cell outside first..last is refused");
    Run const kalimdorRuns[] = { { 65535, 22 }, { 65535, 22 }, { 65535, 21 }, { 65535, 21 }, { 4, 21 } };
    LayoutTable const kalimdorTable = { 99, 1, kalimdorRuns, 5 };
    Require(Decode(kalimdorTable, cells) && FirstRegion(cells) == 21 && LastRegion(cells) == 22
        && RegionsContiguous(cells, 21, 22), "per-layout ids: a continent may start at 21");
    for (Expect const& e : expects)
        Require(e.last[0] < e.first[1], "the continents' id ranges do not overlap", e.layout);
    Require(CellAxis(1.0e9f) == 0 && CellAxis(-1.0e9f) == kCells - 1, "coordinates clamp to the continent");

    if (failures)
    {
        std::printf("CONTINENT_LAYOUT_541_TEST=FAIL (%d)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("CONTINENT_LAYOUT_541_TEST=PASS\n");
    return EXIT_SUCCESS;
}
