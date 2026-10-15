#!/usr/bin/env python3
"""Fixture for `.mmaps replay` (twow-repo#592 PATH-02, PR pair 0).

Builds the row file the GM command `.mmaps replay <name> [offset]` reads from <DataDir>/pathreplay/<name>
(format: src/game/Maps/PathReplay.h). Rows are travel node pairs of map 0 and 1 from
ai_playerbot_travelnode, chosen with a fixed seed, so the same input and seed always give the same file:

  singles  node pairs 20..400 yd apart (2D), one calculate each (chain 0)
  water    walk links with swim_distance > 0 (shore nodes), node -> node, chain 0
  extra    points from --extra (e.g. Tel'Abim, map 1 areas 5122/5128/5129, which the travel node dump
           does not cover), each paired both ways with nearby nodes, chain 0
  chains   node pairs 400..1500 yd apart, chain 40 (WorldPosition::getPathFromPath default)
  map      copies of the first single rows with profile map; the current build reports them as
           skipped=map_mode, they run once PathInfo has a map mode (PR pair 2)

Coordinates are written exactly as the dump has them. Input is the module's SQL dump (default) or a live
export (`mysql --batch -e "SELECT id, name, map_id, x, y, z FROM ai_playerbot_travelnode"` and
`... SELECT node_id, to_node_id, type, swim_distance FROM ai_playerbot_travelnode_link`).

Usage: gen_path_replay_fixture.py --out FILE [--sql FILE | --nodes-tsv FILE --links-tsv FILE]
                                  [--extra FILE] [--seed 592] [--singles 5000] [--chains 200] [--map 200]
"""
import argparse
import math
import random
import re
import sys
from pathlib import Path

DEFAULT_SQL = Path(__file__).resolve().parents[2] / "modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql"
MAPS = (0, 1)
SINGLE_MIN, SINGLE_MAX = 20.0, 400.0
CHAIN_MIN, CHAIN_MAX = 400.0, 1500.0
CHAIN_STEPS = 40
WATER_SHARE = 0.10          # at most this share of the single rows comes from swim links
EXTRA_PAIRS = 5             # rows per extra point and direction
LINK_WALK = 1               # TravelNodePathType walk

NUM = r"(-?[0-9.]+(?:[eE][-+]?[0-9]+)?)"
NODE_ROW = re.compile(r"^\s*\((\d+),\s*'((?:[^'\\]|\\.|'')*)',\s*(-?\d+),\s*" + NUM + r",\s*" + NUM + r",\s*" + NUM + r",")
LINK_ROW = re.compile(r"^\s*\((\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*" + NUM + r",\s*" + NUM + r",")
INSERT = re.compile(r"^INSERT INTO `(\w+)`")


class Node:
    def __init__(self, node_id, map_id, xs, ys, zs):
        self.id = node_id
        self.map = map_id
        self.text = (xs, ys, zs)          # verbatim
        self.x, self.y, self.z = float(xs), float(ys), float(zs)


def dist2d(a, b):
    return math.hypot(a.x - b.x, a.y - b.y)


def read_sql(path):
    nodes, links, table = {}, [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = INSERT.match(line)
            if m:
                table = m.group(1)
                continue
            if table == "ai_playerbot_travelnode":
                m = NODE_ROW.match(line)
                if m:
                    nodes[int(m.group(1))] = Node(int(m.group(1)), int(m.group(3)), m.group(4), m.group(5), m.group(6))
            elif table == "ai_playerbot_travelnode_link":
                m = LINK_ROW.match(line)
                if m:
                    links.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), float(m.group(6))))
    return nodes, links


def read_tsv(path, columns):
    rows = []
    with open(path, encoding="utf-8") as f:
        header = f.readline().rstrip("\n").split("\t")
        index = [header.index(c) for c in columns]
        for line in f:
            fields = line.rstrip("\n").split("\t")
            if len(fields) == len(header):
                rows.append([fields[i] for i in index])
    return rows


def read_live(nodes_tsv, links_tsv):
    nodes = {}
    for node_id, map_id, xs, ys, zs in read_tsv(nodes_tsv, ["id", "map_id", "x", "y", "z"]):
        nodes[int(node_id)] = Node(int(node_id), int(map_id), xs, ys, zs)
    links = [(int(a), int(b), int(t), float(s))
             for a, b, t, s in read_tsv(links_tsv, ["node_id", "to_node_id", "type", "swim_distance"])]
    return nodes, links


def read_extra(path):
    points = []
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 4:
                sys.exit(f"{path}:{number}: expected <map> <x> <y> <z> [label]")
            node = Node(-number, int(fields[0]), fields[1], fields[2], fields[3])
            if node.map in MAPS:
                points.append(node)
    return points


def pick(rng, items):
    # Index from random() only: stable across Python versions, unlike choice()/sample().
    return items[int(rng.random() * len(items))]


def pairs_in_range(by_map, low, high):
    """For every node the nodes of its map within [low, high] yd, in id order."""
    near = {}
    for nodes in by_map.values():
        for a in nodes:
            lst = [b for b in nodes if b is not a and low <= dist2d(a, b) <= high]
            if lst:
                near[a.id] = (a, lst)
    return near


def sample_pairs(rng, near, count):
    keys = sorted(near)
    out = []
    for _ in range(count):
        a, lst = near[pick(rng, keys)]
        out.append((a, pick(rng, lst)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--sql", default=str(DEFAULT_SQL))
    ap.add_argument("--nodes-tsv")
    ap.add_argument("--links-tsv")
    ap.add_argument("--extra")
    ap.add_argument("--seed", type=int, default=592)
    ap.add_argument("--singles", type=int, default=5000)
    ap.add_argument("--chains", type=int, default=200)
    ap.add_argument("--map", type=int, default=200)
    args = ap.parse_args()

    if bool(args.nodes_tsv) != bool(args.links_tsv):
        sys.exit("--nodes-tsv and --links-tsv go together")
    if args.nodes_tsv:
        source = f"{args.nodes_tsv} {args.links_tsv}"
        nodes, links = read_live(args.nodes_tsv, args.links_tsv)
    else:
        source = Path(args.sql).name
        nodes, links = read_sql(args.sql)

    by_map = {m: [nodes[i] for i in sorted(nodes) if nodes[i].map == m] for m in MAPS}
    if not any(by_map.values()):
        sys.exit("no travel nodes on map 0/1 in the input")
    rng = random.Random(args.seed)
    rows = []   # (start, dest, profile, chain)

    water = [(nodes[a], nodes[b]) for a, b, t, swim in sorted(links)
             if t == LINK_WALK and swim > 0 and a in nodes and b in nodes
             and nodes[a].map in MAPS and nodes[a].map == nodes[b].map]
    water_count = min(len(water), int(args.singles * WATER_SHARE))
    for _ in range(water_count):
        a, b = pick(rng, water)
        rows.append((a, b, "unit", 0))

    extra_rows = []
    for p in (read_extra(args.extra) if args.extra else []):
        same = by_map[p.map]
        if not same:
            continue
        close = [n for n in same if dist2d(p, n) <= SINGLE_MAX] or sorted(same, key=lambda n: dist2d(p, n))[:EXTRA_PAIRS]
        for _ in range(EXTRA_PAIRS):
            n = pick(rng, close)
            extra_rows.append((p, n, "unit", 0))
            extra_rows.append((n, p, "unit", 0))
    rows.extend(extra_rows)

    singles = max(0, args.singles - len(rows))
    near = pairs_in_range(by_map, SINGLE_MIN, SINGLE_MAX)
    single_rows = [(a, b, "unit", 0) for a, b in sample_pairs(rng, near, singles)] if near else []
    rows = single_rows + rows

    far = pairs_in_range(by_map, CHAIN_MIN, CHAIN_MAX)
    chain_rows = [(a, b, "unit", CHAIN_STEPS) for a, b in sample_pairs(rng, far, args.chains)] if far else []
    map_rows = [(a, b, "map", chain) for a, b, _, chain in single_rows[:args.map]]
    rows += chain_rows + map_rows

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# twow-repo#592 PATH-02 .mmaps replay fixture (tools/path_replay/gen_path_replay_fixture.py)\n")
        f.write(f"# source={source} seed={args.seed} rows={len(rows)} singles={len(single_rows)} water={water_count} "
                f"extra={len(extra_rows)} chains={len(chain_rows)} map={len(map_rows)}\n")
        f.write("# id mapId sx sy sz dx dy dz profile chain\n")
        for row_id, (a, b, profile, chain) in enumerate(rows, 1):
            f.write(f"{row_id}\t{a.map}\t{a.text[0]}\t{a.text[1]}\t{a.text[2]}\t"
                    f"{b.text[0]}\t{b.text[1]}\t{b.text[2]}\t{profile}\t{chain}\n")
    print(f"{args.out}: {len(rows)} rows")


if __name__ == "__main__":
    main()
