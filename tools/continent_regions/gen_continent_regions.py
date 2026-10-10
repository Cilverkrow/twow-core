#!/usr/bin/env python3
"""Continent region layouts for Continents.Layout (twow-repo#541, part C).

Builds one region number per 66.67-yard cell (8 x 8 per map tile, 512 x 512 per continent) for the layouts
14, 16, 18 and 20 from the zone of each cell, read from the server map files (area grid, 16 x 16 per tile) and
AreaTable.dbc. Zone -> region assignment: OB-50 draft (twow-repo#541 comment 6041262753,
newregions-proposal-16.json); 14 and 18 are derived from it as described there (section 6). Layout 20: owner
decision of 10.10.2026 (proposal twow-repo#541 comment 6096621659): every capital its own region, Ironforge and
Undercity as cell rectangles, region ids per layout (Eastern Kingdoms 1..12, Kalimdor 21..32).

Cells without a zone (sea, unnamed) take the region of the nearest assigned cell (multi-source BFS); the
rectangles of a layout are set after the zone vote and before that fill.
Output: a C++ source with the run-length encoded tables and a review report.

Usage: gen_continent_regions.py --maps DIR --dbc AreaTable.dbc --out-cpp FILE --out-report FILE
"""
import argparse
import struct
import sys
from collections import Counter, deque
from pathlib import Path

TILE = 533.33333
CELLS = 512          # 66.67-yard cells per continent axis (8 per tile)
SUB = 16             # area-grid entries per tile axis in the .map files

# OB-50 draft, 16 regions (Eastern Kingdoms 8, Kalimdor 8). Order = region id order.
LAYOUT16 = {
    0: [
        ("E1 Untote-Kueste", ["Tirisfal Glades", "Western Plaguelands", "Silverpine Forest"]),
        ("E2 Nord-Ost", ["Eastern Plaguelands", "Thalassian Highlands", "Quel'Thalas", "Scarlet Enclave",
                         "The Hinterlands", "Hillsbrad Foothills"]),
        ("E3 Dun Morogh", ["Dun Morogh", "Searing Gorge", "Northwind"]),
        ("E4 Zwergen-Mitte", ["Wetlands", "Loch Modan", "Grim Reaches", "Arathi Highlands"]),
        ("E5 Elwynn", ["Elwynn Forest", "Redridge Mountains", "Burning Steppes", "Badlands"]),
        ("E6 Suedwest", ["Westfall", "Duskwood", "Deadwind Pass", "Blasted Lands", "Swamp of Sorrows",
                         "Gillijim's Isle", "Lapidis Isle", "Balor"]),
        ("E7 Stranglethorn", ["Stranglethorn Vale", "Gilneas", "Alterac Mountains"]),
        ("E8 Staedte", ["Stormwind City", "Alah'Thalas"]),
    ],
    1: [
        ("K1 Nachtelfen", ["Teldrassil", "Darkshore", "Felwood", "Moonglade", "Hyjal", "Winterspring"]),
        ("K2 Ashenvale", ["Ashenvale", "Stonetalon Mountains", "Moonwhisper Coast"]),
        ("K3 Durotar", ["Durotar", "Durotar: Echo Isles", "Azshara", "Durotar: Valley of Trials"]),
        ("K4 Brachland Nord", ["Barrens Nord", "Silithus"]),
        ("K5 Brachland Sued", ["Barrens Sued", "Dustwallow Marsh"]),
        ("K6 Mulgore", ["Mulgore", "Desolace", "Blackstone Island"]),
        ("K7 Tanaris", ["Tanaris", "Thousand Needles", "Un'Goro Crater", "Feralas", "Tel'Abim", "Icepoint Rock"]),
        ("K8 Staedte", ["Orgrimmar", "Thunder Bluff", "Darnassus"]),
    ],
}


def layout14():
    """OB-50 section 6, compact: no city regions; each city joins the region around it."""
    joins = {"Stormwind City": "E5 Elwynn", "Alah'Thalas": "E2 Nord-Ost", "Orgrimmar": "K3 Durotar",
             "Thunder Bluff": "K6 Mulgore", "Darnassus": "K1 Nachtelfen"}
    out = {}
    for mp, regions in LAYOUT16.items():
        kept = [(name, list(zones)) for name, zones in regions if "Staedte" not in name]
        for name, zones in regions:
            if "Staedte" in name:
                for z in zones:
                    target = next(r for r in kept if r[0] == joins[z])
                    target[1].append(z)
        out[mp] = kept
    return out


def layout18():
    """OB-50 section 6, extension: Barrens North in a west (Crossroads) and an east (Ratchet) half, and
    the Valley of Trials as a region of its own (Kalimdor 10)."""
    out = {0: [(n, list(z)) for n, z in LAYOUT16[0]], 1: []}
    for name, zones in LAYOUT16[1]:
        zones = list(zones)
        if name.startswith("K3"):
            zones.remove("Durotar: Valley of Trials")
        if name.startswith("K4"):
            zones = ["Barrens Nord-West" if z == "Barrens Nord" else z for z in zones]
        out[1].append((name, zones))
    out[1].append(("K9 Brachland Nord-Ost", ["Barrens Nord-Ost"]))
    out[1].append(("K10 Tal der Pruefungen", ["Durotar: Valley of Trials"]))
    return out


# Owner decision 10.10.2026 (twow-repo#541, proposal comment 6096621659, relayed by OB-00): 12 + 12 regions, every
# capital its own region. Ironforge and Undercity are no zones of their own in the area grid (report of 09.10.: no
# unassigned zone on map 0, so both count as Dun Morogh / Tirisfal Glades); they are the cell
# rectangles in RECTS (the zone lists stay empty), Dun Morogh and Tirisfal keep the rest. Barrens option A: the
# north/south cut of layout 16 (x median), Crossroads and Ratchet both north.
LAYOUT20 = {
    0: [
        ("E1 Stormwind", ["Stormwind City"]),
        ("E2 Ironforge", []),
        ("E3 Undercity", []),
        ("E4 Alah'Thalas", ["Alah'Thalas", "Thalassian Highlands"]),
        ("E5 Elwynn", ["Elwynn Forest"]),
        ("E6 Dun Morogh", ["Dun Morogh"]),
        ("E7 Tirisfal", ["Tirisfal Glades"]),
        ("E8 Nord-Ost", ["Quel'Thalas", "Eastern Plaguelands", "Scarlet Enclave"]),
        ("E9 Sueden", ["Westfall", "Redridge Mountains", "Duskwood", "Deadwind Pass", "Swamp of Sorrows",
                       "Blasted Lands", "Burning Steppes"]),
        ("E10 Stranglethorn", ["Stranglethorn Vale", "Gillijim's Isle", "Lapidis Isle"]),
        ("E11 Zwergen-Mitte", ["Wetlands", "Loch Modan", "Arathi Highlands", "Grim Reaches", "Badlands",
                               "Searing Gorge", "Northwind", "Balor"]),
        ("E12 Nord-West", ["Silverpine Forest", "Hillsbrad Foothills", "Alterac Mountains", "Gilneas",
                           "The Hinterlands", "Western Plaguelands"]),
    ],
    1: [
        ("K1 Orgrimmar", ["Orgrimmar"]),
        ("K2 Thunder Bluff", ["Thunder Bluff"]),
        ("K3 Darnassus", ["Darnassus"]),
        ("K4 Durotar", ["Durotar", "Durotar: Echo Isles"]),
        ("K5 Tal der Pruefungen", ["Durotar: Valley of Trials"]),
        ("K6 Mulgore", ["Mulgore"]),
        ("K7 Teldrassil", ["Teldrassil"]),
        ("K8 Brachland Nord", ["Barrens Nord"]),
        ("K9 Brachland Sued", ["Barrens Sued", "Dustwallow Marsh", "Thousand Needles"]),
        ("K10 Nord", ["Darkshore", "Ashenvale", "Stonetalon Mountains", "Felwood", "Moonglade", "Winterspring",
                      "Azshara", "Hyjal", "Moonwhisper Coast"]),
        ("K11 Sued", ["Desolace", "Feralas", "Tanaris", "Un'Goro Crater", "Silithus", "Tel'Abim", "Icepoint Rock"]),
        ("K12 Blackstone", ["Blackstone Island"]),
    ],
}

LAYOUTS = {14: layout14(), 16: {mp: [(n, list(z)) for n, z in r] for mp, r in LAYOUT16.items()}, 18: layout18(),
           20: {mp: [(n, list(z)) for n, z in r] for mp, r in LAYOUT20.items()}}
# First region id per layout and continent. 14/16/18 as before (1 / 11); 20 has 12 regions per continent and
# starts Kalimdor at 21. The server checks that each continent's ids run without a gap from first to last, that
# the two ranges do not overlap and that they stay below 100 (generated instance ids; 0x80 is the border bit).
FIRST_ID = {14: {0: 1, 1: 11}, 16: {0: 1, 1: 11}, 18: {0: 1, 1: 11}, 20: {0: 1, 1: 21}}
MAX_REGION_ID = 99
# Cell rectangles (region id, x min, x max, y min, y max): a cell whose centre lies inside takes the region,
# whatever zone the area grid names there. Owner decision 10.10.2026, layout 20 only.
RECTS = {
    20: {0: [(2, -5060.0, -4540.0, -1360.0, -840.0, "Ironforge"),
             (3, 1180.0, 1880.0, -20.0, 520.0, "Undercity + Ruins of Lordaeron")]},
}
# Layout 18 (OB-50: "Crossroads-West gegen Ratchet-Ost"): halfway between the Crossroads (y -2650) and Ratchet
# (y -3754); the median of the northern half put both hubs on the same side.
BARRENS_NORTH_YCUT = (-2650.0 + -3754.0) / 2

# Known places (x, y) and the zone label expected there: checks the orientation of the map files.
KNOWN = [
    (0, -9464.0, 62.0, "Elwynn Forest", "Goldshire"),
    (0, -8913.0, 554.0, "Stormwind City", "Stormwind"),
    (0, -5603.0, -482.0, "Dun Morogh", "Kharanos"),
    (0, 2269.0, 244.0, "Tirisfal Glades", "Brill"),
    (0, -10646.0, 1053.0, "Westfall", "Sentinel Hill"),
    (0, -14297.0, 530.0, "Stranglethorn Vale", "Booty Bay"),
    (1, -452.0, -2650.0, "Barrens Nord-West", "Crossroads"),
    (1, -956.0, -3754.0, "Barrens Nord-Ost", "Ratchet"),
    (1, -2380.0, -1880.0, "Barrens Sued", "Camp Taurajo"),
    (1, 311.0, -4724.0, "Durotar", "Razor Hill"),
    (1, -602.0, -4262.0, "Durotar: Valley of Trials", "Valley of Trials"),
    (1, 1629.0, -4373.0, "Orgrimmar", "Orgrimmar"),
    (1, -1277.0, 124.0, "Thunder Bluff", "Thunder Bluff"),
    (1, -2345.0, -366.0, "Mulgore", "Bloodhoof Village"),
    (1, 9947.0, 2482.0, "Darnassus", "Darnassus"),
    (1, 9889.0, 977.0, "Teldrassil", "Dolanaar"),
    (1, -7180.0, -3773.0, "Tanaris", "Gadgetzan"),
]

# Layout 20: expected region of known places (cell of the place in the generated table). Alah'Thalas: a cell deep
# inside the Stormwind/Alah'Thalas city region of layout 16 north of Stormwind (cell 181,300), so its zone vote is
# Alah'Thalas or Thalassian Highlands, both region 4 here. Just outside the Ironforge gate stays Dun Morogh.
KNOWN20 = [
    (0, -8913.0, 554.0, 1, "Stormwind"),
    (0, -4838.0, -1186.0, 2, "Ironforge Great Forge"),
    (0, 1595.0, 231.0, 3, "Undercity bank"),
    (0, 4966.0, -2966.0, 4, "Alah'Thalas"),
    (0, -9464.0, 62.0, 5, "Goldshire"),
    (0, -5603.0, -482.0, 6, "Kharanos"),
    (0, -5100.0, -800.0, 6, "outside the Ironforge gate"),
    (0, 2269.0, 244.0, 7, "Brill"),
    (0, -14297.0, 530.0, 10, "Booty Bay"),
    (1, 1629.0, -4373.0, 21, "Orgrimmar"),
    (1, -1277.0, 124.0, 22, "Thunder Bluff"),
    (1, 9947.0, 2482.0, 23, "Darnassus"),
    (1, 311.0, -4724.0, 24, "Razor Hill"),
    (1, -602.0, -4262.0, 25, "Valley of Trials"),
    (1, -2345.0, -366.0, 26, "Bloodhoof Village"),
    (1, 9889.0, 977.0, 27, "Dolanaar"),
    (1, -452.0, -2650.0, 28, "Crossroads"),
    (1, -956.0, -3754.0, 28, "Ratchet"),
    (1, -2380.0, -1880.0, 29, "Camp Taurajo"),
    (1, -7180.0, -3773.0, 31, "Gadgetzan"),
]
KNOWN_REGION = {20: KNOWN20}


def read_dbc(path):
    b = Path(path).read_bytes()
    magic, records, fields, size, ssize = struct.unpack_from("<4sIIII", b, 0)
    assert magic == b"WDBC", magic
    rows = [struct.unpack_from("<%dI" % fields, b, 20 + i * size) for i in range(records)]
    strings = b[20 + records * size:]
    def string(off):
        end = strings.index(b"\0", off)
        return strings[off:end].decode("utf-8", "replace")
    return rows, string


def load(maps_dir, dbc):
    rows, string = read_dbc(dbc)
    name = {r[0]: string(r[11]) for r in rows}
    parent = {r[0]: (r[2] or r[0]) for r in rows}
    by_bit = {}
    for r in rows:
        if r[3] and (r[1], r[3]) not in by_bit:
            by_bit[(r[1], r[3])] = r[0]
    sea = {a for a, n in name.items() if " Sea" in n or n.startswith("The Great Sea") or "Ocean" in n}
    grid = {0: {}, 1: {}}   # mp -> (gx, gy) -> area id; gx along world x (falling), gy along world y
    files = sorted(Path(maps_dir).glob("00[01]????.map"))
    for f in files:
        mp = int(f.stem[:3])
        xx, yy = int(f.stem[3:5]), int(f.stem[5:7])
        b = f.read_bytes()
        aoff = struct.unpack_from("<I", b, 8)[0]
        _, flags, flat_area = struct.unpack_from("<4sHH", b, aoff)
        flat = [flat_area] * 256 if flags & 1 else list(struct.unpack_from("<256H", b, aoff + 8))
        for lx in range(SUB):
            for ly in range(SUB):
                v = flat[lx * SUB + ly]
                aid = by_bit.get((mp, v)) if v and v != 0xFFFF else None
                if aid is not None and aid not in sea:
                    grid[mp][(SUB * xx + lx, SUB * yy + ly)] = aid
    return grid, name, parent, len(files)


def world_of_sub(g):
    return (32 - (g + 0.5) / SUB) * TILE


def labels(grid, name, parent):
    """Planning label per area-grid entry (zone, with the sub-area and Barrens cuts of OB-50)."""
    lab = {0: {}, 1: {}}
    barrens = []
    for mp in (0, 1):
        for (gx, gy), aid in grid[mp].items():
            zn = name[parent[aid]]
            an = name[aid]
            if zn == "Durotar":
                if an == "Valley of Trials":
                    zn = "Durotar: Valley of Trials"
                elif an in ("Echo Isles", "Darkspear Strand"):
                    zn = "Durotar: Echo Isles"
            if zn == "The Barrens":
                barrens.append((gx, gy))
            lab[mp][(gx, gy)] = zn
    xs = sorted(world_of_sub(gx) for gx, _ in barrens)
    xcut = xs[len(xs) // 2]
    ycut = BARRENS_NORTH_YCUT
    for gx, gy in barrens:
        if world_of_sub(gx) >= xcut:
            # Barrens Nord; for layout 18 split along y (west = larger y = Crossroads side)
            lab[1][(gx, gy)] = "Barrens Nord|W" if world_of_sub(gy) >= ycut else "Barrens Nord|E"
        else:
            lab[1][(gx, gy)] = "Barrens Sued"
    return lab, xcut, ycut


def label_matches(label, zone):
    if label.startswith("Barrens Nord|"):
        return zone == "Barrens Nord" or zone == ("Barrens Nord-West" if label.endswith("W") else "Barrens Nord-Ost")
    return label == zone


def cell_centre(c):
    return (32 - (c + 0.5) / 8) * TILE


def build(lid, layout, lab):
    """Region id per 66.67-yard cell, majority of its 2 x 2 area-grid entries, then the layout's rectangles,
    BFS fill for the rest."""
    tables = {}
    unassigned_labels = Counter()
    for mp in (0, 1):
        zone_region = {}
        for i, (_, zones) in enumerate(layout[mp]):
            for z in zones:
                zone_region[z] = FIRST_ID[lid][mp] + i
        def region_of(label):
            for z, rid in zone_region.items():
                if label_matches(label, z):
                    return rid
            return None
        votes = [[None] * CELLS for _ in range(CELLS)]
        tally = {}
        for (gx, gy), label in lab[mp].items():
            rid = region_of(label)
            if rid is None:
                unassigned_labels[(mp, label)] += 1
                continue
            cx, cy = gx // 2, gy // 2
            tally.setdefault((cx, cy), Counter())[rid] += 1
        for (cx, cy), c in tally.items():
            votes[cx][cy] = c.most_common(1)[0][0]
        for rid, x0, x1, y0, y1, _ in RECTS.get(lid, {}).get(mp, []):
            for cx in range(CELLS):
                if not x0 <= cell_centre(cx) <= x1:
                    continue
                for cy in range(CELLS):
                    if y0 <= cell_centre(cy) <= y1:
                        votes[cx][cy] = rid
        queue = deque((cx, cy) for cx in range(CELLS) for cy in range(CELLS) if votes[cx][cy] is not None)
        while queue:
            cx, cy = queue.popleft()
            for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                if 0 <= nx < CELLS and 0 <= ny < CELLS and votes[nx][ny] is None:
                    votes[nx][ny] = votes[cx][cy]
                    queue.append((nx, ny))
        tables[mp] = votes
    return tables, unassigned_labels


def cell_of(x, y):
    cx = min(CELLS - 1, max(0, int((32 - x / TILE) * 8)))
    cy = min(CELLS - 1, max(0, int((32 - y / TILE) * 8)))
    return cx, cy


def rle(table):
    flat = [table[cx][cy] for cx in range(CELLS) for cy in range(CELLS)]
    runs = []
    for v in flat:
        if runs and runs[-1][1] == v and runs[-1][0] < 65535:
            runs[-1][0] += 1
        else:
            runs.append([1, v])
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", required=True)
    ap.add_argument("--dbc", required=True)
    ap.add_argument("--out-cpp", required=True)
    ap.add_argument("--out-report", required=True)
    a = ap.parse_args()

    grid, name, parent, nfiles = load(a.maps, a.dbc)
    lab, xcut, ycut = labels(grid, name, parent)
    report = [f"map files: {nfiles}", f"Barrens cut north/south x = {xcut:.0f}; north west/east y = {ycut:.0f}"]
    failures = []

    # Orientation check on known places (zone label at the area-grid entry of the place).
    for mp, x, y, zone, place in KNOWN:
        gx, gy = int(SUB * (32 - x / TILE)), int(SUB * (32 - y / TILE))
        got = lab[mp].get((gx, gy))
        ok = got is not None and label_matches(got, zone)
        report.append(f"known {place}: expected {zone}, got {got} -> {'OK' if ok else 'FAIL'}")
        if not ok:
            failures.append(f"known place {place}")

    out = ["// Generated by tools/continent_regions/gen_continent_regions.py - do not edit.",
           "// twow-repo#541 part C: region per 66.67-yard cell (cx along falling x, cy along falling y),",
           "// run-length encoded over cx * 512 + cy. Zone -> region: OB-50 draft (#541 comment 6041262753);",
           "// layout 20: owner decision 10.10.2026 (#541 comment 6096621659), Ironforge/Undercity rectangles.",
           "#include \"ContinentRegionTables.h\"", "", "namespace continent_regions", "{"]
    index = []
    for lid, layout in LAYOUTS.items():
        tables, unassigned = build(lid, layout, lab)
        report.append(f"\n=== Continents.Layout = {lid}")
        for (mp, label), n in sorted(unassigned.items()):
            report.append(f"  map {mp}: zone without region (filled from neighbours): {label} ({n} entries)")
        ranges = {mp: (FIRST_ID[lid][mp], FIRST_ID[lid][mp] + len(layout[mp]) - 1) for mp in (0, 1)}
        if not (ranges[0][1] < ranges[1][0] or ranges[1][1] < ranges[0][0]):
            failures.append(f"layout {lid}: region ids {ranges[0]} and {ranges[1]} overlap")
        for mp in (0, 1):
            regions = layout[mp]
            first, last = ranges[mp]
            if first < 1 or last > MAX_REGION_ID:
                failures.append(f"layout {lid} map {mp}: region ids {first}-{last} outside 1-{MAX_REGION_ID}")
            counts = Counter(v for col in tables[mp] for v in col)
            land = Counter()
            for (gx, gy), label in lab[mp].items():
                land[tables[mp][gx // 2][gy // 2]] += 1
            rects = {r[0]: r for r in RECTS.get(lid, {}).get(mp, [])}
            for i, (rname, zones) in enumerate(regions):
                rid = first + i
                where = ", ".join(zones)
                if rid in rects:
                    _, x0, x1, y0, y1, rlabel = rects[rid]
                    where = ", ".join(zones + [f"rectangle {rlabel} x {x0:.0f}..{x1:.0f} y {y0:.0f}..{y1:.0f}"])
                report.append(f"  id {rid:2d} {rname}: cells {counts[rid]}, land entries {land[rid]}; " + where)
                if counts[rid] == 0 or land[rid] == 0:
                    failures.append(f"layout {lid} region {rid} has no land")
                for z in zones:
                    if not any(label_matches(l, z) for l in lab[mp].values()):
                        failures.append(f"layout {lid}: zone {z} not found in the map files")
            if any(v is None for col in tables[mp] for v in col):
                failures.append(f"layout {lid} map {mp}: cell without region")
            elif any(not first <= v <= last for col in tables[mp] for v in col):
                failures.append(f"layout {lid} map {mp}: region id outside {first}-{last}")
            for _, x, y, zone, place in [k for k in KNOWN if k[0] == mp]:
                cx, cy = cell_of(x, y)
                report.append(f"  {place}: region {tables[mp][cx][cy]}")
            for _, x, y, want, place in [k for k in KNOWN_REGION.get(lid, []) if k[0] == mp]:
                cx, cy = cell_of(x, y)
                got = tables[mp][cx][cy]
                report.append(f"  known {place}: expected region {want}, got {got} -> {'OK' if got == want else 'FAIL'}")
                if got != want:
                    failures.append(f"layout {lid} known place {place}: region {got}, expected {want}")
            runs = rle(tables[mp])
            sym = f"kLayout{lid}Map{mp}"
            out.append(f"// layout {lid}, map {mp}: {len(runs)} runs")
            out.append(f"static Run const {sym}[] = {{")
            for k in range(0, len(runs), 12):
                out.append("    " + " ".join(f"{{{n},{v}}}," for n, v in runs[k:k + 12]))
            out.append("};")
            index.append((lid, mp, sym, len(runs)))
    out.append("")
    out.append("LayoutTable const kTables[] = {")
    for lid, mp, sym, n in index:
        out.append(f"    {{ {lid}, {mp}, {sym}, {n} }},")
    out.append("};")
    out.append("std::size_t const kTableCount = sizeof(kTables) / sizeof(kTables[0]);")
    out.append("}")
    Path(a.out_cpp).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")

    report.append("\nRESULT=" + ("FAIL: " + "; ".join(failures) if failures else "PASS"))
    Path(a.out_report).write_text("\n".join(report) + "\n", encoding="utf-8", newline="\n")
    print("\n".join(report))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
