# `.mmaps replay` fixtures (twow-repo#592 PATH-02)

`.mmaps replay <name> [offset [probe]]` (GM or server console, SEC_DEVELOPER; `.mmap replay` works as
abbreviation) computes the rows of `<DataDir>/pathreplay/<name>` on fresh `PathInfo` objects and writes one
`[PathReplay]` line per result into the server log, points as hexfloats. Two runs diff bit exact. It is the
proof base for PathInfo variant A (PR pair 2): old build against old build (A/A, precondition), then old
against new.

Row format, output lines and limits: `src/game/Maps/PathReplay.h`.

## Make a fixture

```
python3 tools/path_replay/gen_path_replay_fixture.py --out m2.tsv              # repo SQL dumps, seed 592
python3 tools/path_replay/gen_path_replay_fixture.py --out m2.tsv \
    --nodes-tsv nodes.tsv --links-tsv links.tsv --extra points.txt              # live export + extra points
```

Same input and seed give the same file. Defaults: 5000 single rows (about 10 % shore rows from walk links
with `swim_distance > 0`, 250 Tel'Abim rows), 200 chain rows (40 steps, as `getPathFromPath`, 20 of them
on Tel'Abim), 200 map-mode copies.

Tel'Abim (map 1, zone 5121) has no travel nodes, so its points come from the world dump: the creature
spawns of `sql/base/tw_world_creature.sql` within 1500 yd of `game_tele` `telabim`
(`sql/base/tw_world_game_tele.sql`), minus those within 500 yd of a map 1 travel node (Tanaris coast).
That gives 576 points; they are paired only with each other (20..400 yd for single rows, 400..1500 yd for
chain rows), so no row crosses the sea. The header line counts them (`telabim=`, `telabim_chains=`,
`telabim_points=`). `--telabim 0 --telabim-chains 0` builds without them.

`--extra` takes `<map> <x> <y> <z> [label]` per line (map 0 or 1); each point is paired both ways with
travel nodes, Tel'Abim points and other extra points within 400 yd. A point with no partner that close is
left out and counted as `extra_skipped=`.

## Run (M2 slots only, never in measurement arms)

The command runs on the world thread: a GM's chat command is processed in `World::UpdateSessions`
whatever the chat type (say, party, whisper), before the maps update. Every call therefore stalls the
whole world tick, all maps, for up to 200 ms plus one row (a chain row can take seconds).

1. Copy the fixture to `<DataDir>/pathreplay/` (the command opens nothing else; names are plain file names,
   no device names such as `CON` or `nul.tsv`, at most 64 MB).
2. Same mmaps, `Terrain.Preload.Continents = 1` (tiles and grids loaded the same way in both runs), bots
   off, default `TW_ARCH`; one extra run with `LogLevel` DEBUG.
3. Per profile select the probe: nothing (yourself), yourself with `.gm fly`, a ground creature, a creature
   with AIR. The probe must stand on the map of the rows (other rows report `skipped=other_map`) and not on
   a transport.
4. `.mmap replay m2.tsv`. A call reads at most 500 lines and stops after the row that crosses 200 ms wall
   clock with `[PathReplay] partial next=<offset>`; continue with `.mmap replay m2.tsv <offset>` until
   `[PathReplay] done`.
5. Compare: `grep -o '\[PathReplay\] id=.*' a.log > a.txt` (same for b), `diff a.txt b.txt` must be empty.

## Scripted run from the console (M2)

The command is allowed from the server console as well (same SEC_DEVELOPER level), so an M2 script can run
it unattended through `mangosd.in`. A console line runs on the world thread too (`World::ProcessCliCommands`
in `World::Update`, after the maps updated), with the same tick stall as above. The console has no
selection, so the probe is named after the offset; without it the call ends with
`[PathReplay] error=probe` before the fixture is opened:

```
echo 'mmaps replay m2.tsv 0 Probebot' > /opt/turtle/run/mangosd.in        # the player Probebot
echo 'mmaps replay m2.tsv 500 Probebot' > /opt/turtle/run/mangosd.in      # next= offset of the partial line
echo 'mmaps replay m2.tsv 0 Probebot:68' > /opt/turtle/run/mangosd.in     # nearest live creature 68 near him
```

- Probe spec: `<player>` (1..12 letters, the case does not matter) or `<player>:<entry>` (the nearest live
  creature with that entry within 150 yd of that player). Anything else is `[PathReplay] error=probe`.
- The probe player must be online and in the world (not in a loading screen). A random bot works; park it
  so it does not move during the run: `rndbot park <n> here`, or move it first with `tele name <char> <tele>`.
  Both runs of a diff need the same probe setup (same bot config, same place).
- There is no caller map check for a named probe; rows of other maps report `skipped=other_map` as usual.
  The probe must not stand on a transport.
- Read the offset to continue with from the `partial next=<offset>` line in the server log (the console
  output carries it too) until `[PathReplay] done`.
- The begin line ends with `probe_name=<player> probe_guid=<low guid>` (for a creature probe the creature's
  guid and the player it was searched from); it is metadata, the diff still uses the `id=` lines only.
- The GM-fly profile (yourself with `.gm fly`) still needs a GM in game: a named player probe uses that
  player as it is.

Map-mode rows (`profile map`) report `skipped=map_mode` until PathInfo has a map mode (PR pair 2,
`PathFinder.Unitless`); today `PathInfo(mapId, instanceId)` has no unit and crashes in `calculate()`.
