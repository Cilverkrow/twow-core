# `.mmaps replay` fixtures (twow-repo#592 PATH-02)

`.mmaps replay <name> [offset]` (GM, SEC_DEVELOPER; `.mmap replay` works as abbreviation) computes the
rows of `<DataDir>/pathreplay/<name>` on fresh `PathInfo` objects and writes one `[PathReplay]` line per
result into the server log, points as hexfloats. Two runs diff bit exact. It is the proof base for PathInfo
variant A (PR pair 2): old build against old build (A/A, precondition), then old against new.

Row format, output lines and limits: `src/game/Maps/PathReplay.h`.

## Make a fixture

```
python3 tools/path_replay/gen_path_replay_fixture.py --out m2.tsv              # module SQL dump, seed 592
python3 tools/path_replay/gen_path_replay_fixture.py --out m2.tsv \
    --nodes-tsv nodes.tsv --links-tsv links.tsv --extra telabim.txt             # live export + extra points
```

Same input and seed give the same file. Defaults: 5000 single rows (about 10 % shore rows from walk links
with `swim_distance > 0`), 200 chain rows (40 steps, as `getPathFromPath`), 200 map-mode copies.
`--extra` takes `<map> <x> <y> <z> [label]` per line, e.g. Tel'Abim points (map 1, areas 5122/5128/5129),
which the travel node dump does not contain.

## Run (M2 slots only, never in measurement arms)

1. Copy the fixture to `<DataDir>/pathreplay/` (the command opens nothing else; names are plain file names).
2. Same mmaps, `Terrain.Preload.Continents = 1` (tiles and grids loaded the same way in both runs), bots
   off, default `TW_ARCH`; one extra run with `LogLevel` DEBUG.
3. Per profile select the probe: nothing (yourself), yourself with `.gm fly`, a ground creature, a creature
   with AIR. The probe must stand on the map of the rows (other rows report `skipped=other_map`) and not on
   a transport.
4. `.mmap replay m2.tsv`. A call reads at most 500 lines and stops after the row that crosses 200 ms wall
   clock with `[PathReplay] partial next=<offset>`; continue with `.mmap replay m2.tsv <offset>` until
   `[PathReplay] done`.
5. Compare: `grep -o '\[PathReplay\] id=.*' a.log > a.txt` (same for b), `diff a.txt b.txt` must be empty.

Map-mode rows (`profile map`) report `skipped=map_mode` until PathInfo has a map mode (PR pair 2,
`PathFinder.Unitless`); today `PathInfo(mapId, instanceId)` has no unit and crashes in `calculate()`.
