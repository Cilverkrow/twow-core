# Upstream catch-up: measurement, and a staged plan

Measured 2026-09-27 against `upstream/playerbots-integration-gh`
(`f2df1b6a`, authored 2026-09-10, pushed 2026-09-14) and `origin/main`
(`38c1d433`). Merge base `6be01e53`. We are **224 ahead, upstream 164 ahead**.

**Verdict: a single catch-up merge is the wrong operation. Do not attempt it.**
Not because the textual conflict surface is large — it is modest — but because
resolving it correctly requires five product decisions that no merge can make,
and because the reason for having the policy at all expired this month.

## The fact that reframes everything

Upstream announced its own retirement in `9c1e8266` (2026-09-08, `README.md`):

> Through 30 September 2026 it will be kept in sync **only** with upstream
> `Penqle/tortoise-wow` changes — no further work of our own. After that it will
> be **discontinued and archived** (read-only).

`Shyalya/tortoise-wow` is not archived yet (`archived: false`, last push
2026-09-14, idle 13 days). It becomes read-only in three days.

ADR-0026/ADR-0040 justify upstream compatibility as *a means of keeping merge
cost low*. After 2026-09-30 there is no future merge, so there is no future cost
to keep low. The justification is gone; the obligation goes with it. What remains
is a one-off question of **content**: are those 164 commits worth having? That is
a cherry-pick question, not a merge question — and cherry-picking is what lets us
take the 41 commits that are pure gain while declining the three large bets.

Corollary for a human to decide: if this fork wants a live upstream at all, the
ref is `Penqle/tortoise-wow`, which is what Shyalya itself is syncing from. That
is an ADR-level retarget of `UPSTREAM.lock`, not a merge.

## Conflict surface, measured

A throwaway `git merge --no-commit --no-ff` of upstream into a worktree of
`origin/main` produces **30 conflicted paths / 49 conflict hunks**, plus 231
adds, 205 auto-merged modifications, 26 renames and 11 deletions.

### (c) Files the parent `AGENTS.md` forbids editing — 2

| File | Conflict | Class |
|---|---|---|
| `src/game/ScriptObjects.h` | both sides append to the same `PlayerHook` enum tail: ours `PLAYERHOOK_ON_REPOP_AT_GRAVEYARD`, `PLAYERHOOK_ON_QUEST_SHARE_REFUSED`; upstream `PLAYERHOOK_ON_AI_UPDATE`, `PLAYERHOOK_IS_AI_UPDATE_DUE` | union is textually safe (tail append, no renumber), but upstream's two hooks exist to serve the ManTech bot-AI scheduler. Taking the hooks without the scheduler ships two hooks nothing raises. **Semantic.** |
| `src/game/World.h` | enum tail: ours `CONFIG_UINT32_PERFLOG_TICK_STATS_INTERVAL`; upstream `CONFIG_UINT32_PERFLOG_PLAYER_SUMMARY_INTERVAL` | union safe as a tail append. But these are **two competing instrumentations of the same thing** (our aggregated tick percentiles vs upstream's player/bot update-cost summary). Keeping both is a decision, not a resolution. **Semantic.** |

`src/game/ScriptMgr.{h,cpp}` and `src/game/ModuleSlots.h` do **not** conflict.

### (b) Semantic, needs a human — 8

| File | What is actually in dispute |
|---|---|
| `src/game/Objects/Unit.cpp` | Upstream's side is `if (damage > 0 && sWorld.getConfig(CONFIG_BOOL_LEECH_ENABLE))`; ours is the pet-avoidance halving plus the `UNITHOOK_ON_DAMAGE_APPLIED` dispatch, deliberately sited where `damage` is final. **This is a hard compile break, not a style choice:** we removed the leech config, and `CONFIG_BOOL_LEECH_ENABLE` exists nowhere in the merged tree's `World.h`. Any union resolution fails to compile. Resolution is "take ours", but the decision underneath is *do we want upstream's leech feature back*. |
| `src/mangosd/mangosd.conf.dist.in` | Upstream's side is 12 new `MapUpdate.*` / `Diagnostics.Architecture.*` / `Network.ListenBacklog` keys — the ManTech scheduler's entire configuration surface. Adopting the block means adopting the architecture. |
| `CMakeLists.txt`, `src/mangosd/CMakeLists.txt` | `TW_CORE_ROOT` (ours, required for building as a submodule subdirectory) vs `CMAKE_SOURCE_DIR` (upstream). Ours must win everywhere, mechanically. Upstream's side also drags in `ENABLE_SOAP`, `dep/include/gsoap` and `src/mangosd/soap/*` — a re-added SOAP remote-command interface, ~3,300 lines of generated code and a vendored static lib. Separate decision; default should be no. |
| `modules/mod-playerbots/src/ahbot/AhBot.{cpp,h}` (+ 3 `UD`, + 8 `D`) | Upstream **deleted the entire old AHBot (12 files) and replaced it** with a CMaNGOS-style one. Four of the deleted/changed files carry our modifications. Merging means adopting a rewrite and reapplying our AHBot fixes onto different code. Discrete piece of work in its own right. |
| `modules/mod-playerbots/src/playerbot/RandomPlayerbotMgr.{cpp,h}` (8 hunks) | Bot admission/population reconciliation — the exact surface upstream reworked for 4k–6k bots and we reworked for the persistent roster. Invariant 1 (a bot must never be lost) is at stake here; textual resolution is not sufficient evidence. |
| `modules/mod-playerbots/src/playerbot/PlayerbotMgr.{cpp,h}`, `PlayerbotAIConfig.cpp`, `PlayerbotFactory.cpp` | same family, smaller. |

### (a) Trivial / textual — 20

- `src/game/LFT/LFTMgr.h`, `src/game/Spells/SpellAuras.h`,
  `src/game/Movement/RandomMovementGenerator.h` — **duplicate work.** Both sides
  independently made these headers self-contained (upstream: "Core: Make
  SpellAuras header self-contained", "Core: Fix missing ObjectGuid include in
  LFTMgr"). Ours carries the reasoning in comments; take ours, union the
  includes.
- `src/shared/Database/AutoUpdater.cpp` — upstream deleted a `std::getline(std::cin)`
  that paused startup after a failed migration; we already guard it with
  `TW_STDIN_IS_TTY()`. Ours is strictly better. Take ours.
- `src/game/Commands/Commands.cpp` — a comment. `src/game/World.cpp` — the
  `setConfig` pair matching the `World.h` enum decision. `README.md`,
  `AGENTS.md` — take ours; upstream's `README` is the wind-down notice.
- The remaining playerbot action/strategy files are 1–2 hunk textual merges.

### The set that worries me more than the conflicts: 49 silent auto-merges

79 files were changed by both sides; 30 conflicted, so **49 auto-merged
textually with no human looking at them.** Among them:
`modules/mod-playerbots/src/playerbot/PlayerbotAI.{h,cpp}`,
`PlayerbotLoginMgr.cpp`, `TravelMgr.{h,cpp}`, `TravelNode.cpp`,
`WorldPosition.{h,cpp}`, `src/game/Objects/Player.{h,cpp}`,
`src/game/PerformanceMonitor.{h,cpp}`, `src/game/SharedDefines.h`,
`src/game/ObjectMgr.{h,cpp}`, `src/shared/Database/Database.{h,cpp}`.

That is our bot-brain work meeting upstream's bot-AI scheduling rework in the
same files, with git deciding. A clean auto-merge here is evidence of nothing.
Reviewing those 49 is strictly more work than resolving the 30 conflicts, and it
is the work that a "the merge conflicts are only 30 files" framing hides.

## What upstream actually did (164 commits, 503 files, +987k/-7.6k)

| Area | Files | +/- |
|---|---|---|
| `modules/mod-playerbots` | 102 | +4156 / -4090 |
| `modules/mod-dungeon-clear` | 92 | +3248 / -719 |
| `src/game` | 74 | +2857 / -1044 |
| `sql` | 67 | +5309 / -51 |
| `docs` | 56 | **+956953** / -0 |
| `tests` | 37 | +4026 |
| `src/shared` | 34 | +1615 / -500 |
| `src/mangosd` | 10 | +3830 / -28 |

Five themes, in descending order of risk:

1. **"ManTech": a replacement world/map update scheduler (~28 commits).** Merged
   in from a third-party fork (`timfork/mantech-turtle`) via `integrate-mantech`.
   `src/game/Maps/Map.cpp` +910/-260, `Map.h`, `MapManager.cpp`,
   `src/shared/ThreadPool.{h,cpp}` rewritten (`ThreadPool.h` 220 → 78 lines, new
   API), plus **18 new `src/shared/` headers** (`MapTaskExecutor.h`,
   `BackgroundWorldScheduling.h`, `PopulationSpatialIndex.h`,
   `ArchitectureDiagnostics.h`, `WorkSlice.h`, `ExecutionWatch.h`, …), 12 new
   config keys, and the two new `PlayerHook` AI-update hooks. Goal: 4k–6k bots.
   The commit series around it is `Baseline: ManTech scheduling…`, then
   `Revert unstable Turtle scheduling experiment`, then six separate
   `Stabilize …` commits. This is an unbenchmarked performance bet from a fork
   we do not track, and we cannot benchmark it in this repository — it needs a
   populated server. `Map.cpp` does **not** conflict (we have not touched it), so
   it would land silently and wholesale.
2. **gSOAP remote-command interface re-added (2 commits).** `MaNGOSsoap.cpp`,
   `src/mangosd/soap/soap{C.cpp,H.h,Stub.h,Server.cpp}` ≈3,300 generated lines,
   a vendored `dep/include/gsoap`, and `-DENABLE_SOAP`. A remote command
   execution surface on the world daemon. Default answer: no.
3. **Gameplay and world-data fixes (41 commits touch only `sql/`+`docs/`).**
   Quests (Balor, Northwind, Grim Reaches, Troll/Tauren racials), spells
   (shaman clearcasting mask, T3 Earthquake proc, druid, item effects), weapon
   skill hit/glance formulas, creature stats (Lower Karazhan, Hateforge Quarry),
   gossip/`broadcast_text`, item sets, female Satyrs, Path of the Brewmaster,
   plus dead-row cleanup in `spell_chain`/`spell_proc_event`/`spell_threat`/
   `creature_movement`/`creature_linking`. **High value, low risk, independent,
   cherry-pickable.** This is the part worth taking.
4. **Genuine core bug fixes, small and self-contained.**
   `VMapManager2` instance-tree shared mutex; `MoveSpline` clamps a negative
   segment instead of asserting; `AccountMgr::GetName` ignoring an empty cached
   username (silent account lockout after `.account set password`);
   `AuctionHouseMgr` holding auction+item locks for the whole client-query
   snapshot and skipping owner notification for socketless bot owners (AHBot
   expiry crash); `~MangosSocketMgr` declared `noexcept` for MSVC;
   `ObjectMgr` broadcast_text warnings naming the right field; 1.12 client
   crash on initial spline visibility; moving-transport lifecycle for 1.18.
   **Take these.** Several are one-hunk.
5. **Bot/module feature work (194 files across two modules).** AHBot rewrite,
   Arathi Basin node capture, taxi/travel convergence, atomic trainer purchases,
   `who`-list restoration, dungeon-clear routes and rosters. Entangled with our
   224 commits; needs per-feature triage, not a merge.

Plus **956,953 lines of `docs/core-audit/*.json`** — database dumps
(`db-templates.json` alone is 286,782 lines). Pure repository weight. Decline.

## Traps

Two were known going in. Both are real, and I found four more.

1. **Enum tails in shared headers** (known). Confirmed in `ScriptObjects.h` and
   `World.h`. Both are genuine tail appends, so a union resolution does not
   renumber anything — the danger here is not renumbering but *adopting a hook or
   a config key whose implementation you did not take*.
2. **`CONFIG_BOOL_LEECH_ENABLE`: the mid-enum removal, biting** (known trap,
   new instance). We removed the leech config; upstream's `Unit.cpp` still reads
   it. The merged tree contains a reference to an enumerator that does not
   exist. **The merge as produced does not compile.** This is the cheapest
   possible proof that the merge cannot be waved through.
3. **Project-only features written directly into `src/game/`** (known).
   `FunserverLootUnits.h`, `FunserverRareRespawn.h`, `TickStats.h` — absent
   upstream. They did not conflict this time, but `TickStats.h`'s config keys are
   exactly what collides with upstream's `PerformanceLog.PlayerUpdateSummary*` in
   `World.h`, `World.cpp` and `mangosd.conf.dist.in`. Three of the 49 silent
   auto-merges are `PerformanceMonitor.{h,cpp}`.
4. **NEW — `TW_CORE_ROOT` vs `CMAKE_SOURCE_DIR` is a silent build-break vector.**
   We converted the build to `TW_CORE_ROOT` so core builds as a submodule
   subdirectory; upstream still uses `CMAKE_SOURCE_DIR`. I checked the merged
   tree against `origin/main`: exactly one file regains `CMAKE_SOURCE_DIR`
   (`src/mangosd/CMakeLists.txt`, and it conflicted, so it is visible). No
   silent contamination this time — but every upstream build-system commit is a
   fresh chance, and a `CMAKE_SOURCE_DIR` that survives resolves to the *parent*
   repo when core is consumed as a submodule, i.e. it fails only in the
   integrated build, never in core's own CI.
5. **NEW — upstream reorganises the migration layout in a way this repo's own
   `AGENTS.md` forbids.** 26 renames move `sql/database_updates/*.sql` into
   `sql/database_updates/world/` ("move 27 stranded world migrations"). Core's
   `AGENTS.md` states migrations live at
   `sql/database_updates/<YYYYMMDDhhmmss>_<db>.sql` and that "live application
   happens individually with a ledger entry in the `migrations` table, **never
   through auto-update**". `sql/README.md` documents the same split: top-level
   files are install-time bootstrap applied once by `setup_databases.sh`, and
   `<target>/` subdirectories are what the auto-updater scans
   (`AutoUpdater::LoadFileMigrations` uses a **non-recursive**
   `directory_iterator`). The rename therefore **reclassifies 26 migrations from
   bootstrap-once to auto-applied-every-start.** Reject it; take the *contents*
   of any new migration at a new top-level filename instead.
   Mitigating facts, verified: the ledger keys on `(module, content hash)`, not
   path, so a pure rename only logs a name-change note; and no test or script in
   either repository references a moved filename by literal path (our 10
   top-level `2026-09-*` migrations, which several `t/*.cmake` contracts do read
   literally, are untouched by upstream).
6. **NEW — upstream also rewrites `sql/base`** ("remove 560 `npc_trainer` rows
   that pre-empt 20260718150344_world"). `sql/base` only affects a *fresh*
   bootstrap. An existing database bootstrapped from the old base, plus a
   migration written against the new base, is a divergence the auto-updater
   cannot see. Fresh-bootstrap and warm-upgrade are different gates here.
7. **NEW — `UPSTREAM.lock` is at 49/50 drift and about to become meaningless.**
   `upstream_tracking_commit = 3f9a0622` is **49 commits** behind the live tip
   against `max_drift_commits = 50`. The `upstream-freshness` job passes by one
   commit and goes red on upstream's next push. Note also that `3f9a0622` is
   **115 commits ahead of the merge base**: the tracking mirror has been advanced
   as the "we have looked at upstream" ceremony 115 commits further than the code
   was ever merged. Once upstream archives, the drift freezes and the check
   asserts nothing. It should be retired deliberately, not left to rot green.

## Staged plan

Each stage is independently landable, independently revertible, and ends in a
state where `origin/main` builds. Nothing after stage 1 is blocked by anything
before it except stage 0.

**Stage 0 — decide the policy (human, before any code).** Upstream archives
2026-09-30. Choose one: (a) declare the fork independent and retire
`UPSTREAM.lock` / the `upstream-freshness` job with an ADR superseding the
tracking clauses of ADR-0026/ADR-0040; or (b) retarget `UPSTREAM.lock` at
`Penqle/tortoise-wow` and measure the gap there from scratch. Do this first,
because (a) makes stages 4–5 optional and (b) changes what "caught up" means.
Cost: one ADR. **No merge required either way.**

**Stage 1 — snapshot upstream while it still exists.** Before 2026-09-30, push
`upstream/playerbots-integration-gh` to a ref in `Cilverkrow/twow-core`
(`refs/upstream-archive/playerbots-integration-gh-f2df1b6a`) and record the hash
in `UPSTREAM.lock`. A read-only GitHub repo is still readable, but this removes
the dependency entirely. Cost: minutes. **Do this regardless of stage 0.**

**Stage 2 — the free wins: cherry-pick theme 4 (core bug fixes).** Land as
separate PRs, in this order (cheapest and most clearly correct first):
`AccountMgr::GetName` empty-username lockout → `MoveSpline` negative-segment
clamp → `VMapManager2` instance-tree mutex → `AuctionHouseMgr` snapshot locking
and socketless-owner notification skip → `ObjectMgr` broadcast_text field →
`~MangosSocketMgr noexcept`. Each is small, self-contained, and reviewable
against our tree. **Human decision at this checkpoint:** none, beyond normal
review. Gate: core CI (`Build core (Debian trixie)`) per PR.

**Stage 3 — theme 3 (SQL and world data), as new migrations at new top-level
filenames.** Do **not** take upstream's file paths (trap 5) and do **not** take
`sql/base` edits (trap 6). Take the row-level content, re-emit it as
`sql/database_updates/<new timestamp>_world.sql`, idempotent, LF, one migration
per coherent fix. 41 upstream commits touch only `sql/`+`docs/`; expect ~10–15
of our migrations. **Human decisions:** whether each data fix is wanted on a
funserver at all (several are vanilla-correctness fixes we may have deliberately
diverged from — the elemental-invasion default and the weapon-skill hit/glance
formula change in particular), and whether `sql/base` needs a matching
correction for fresh bootstrap.

**Stage 4 — resolve the two forbidden-file enum conflicts explicitly, as a
`twow-core` PR of its own.** `ScriptObjects.h` and `World.h` only. Decide:
(i) do we adopt `PLAYERHOOK_ON_AI_UPDATE` / `PLAYERHOOK_IS_AI_UPDATE_DUE`
without the ManTech scheduler that raises them — almost certainly no, so decline
both rather than union; (ii) do `PerformanceLog.TickStats*` and
`PerformanceLog.PlayerUpdateSummaryInterval` coexist, or does one win — our
`TickStats` is newer, documented against ADR-0031, and already shipped, so the
default is decline upstream's. Note this PR rebuilds ~1060 of 1171 TUs
(ADR-0021); budget one full build. **Human decision required on both.**

**Stage 5 — ManTech: a spike, not a merge, and only if performance demands it.**
Do not take it as part of a catch-up. It is a scheduler replacement plus 18 new
headers plus a `ThreadPool` API break, arriving with its own revert-and-stabilize
history, aimed at 4k–6k bots. If our bot population targets make it interesting,
it is a separate project: isolate `integrate-mantech`, port it onto current
`main` deliberately, and gate it on a measured before/after with a populated
server. **Human decision:** is 4k–6k bots a goal we have? Until that is yes,
this is a no.

**Never — decline permanently, and record why:** the 956k lines of
`docs/core-audit/*.json`; the gSOAP re-add; upstream's migration-layout
reorganisation; upstream's `README.md`/`AGENTS.md`; `CMAKE_SOURCE_DIR`.

**Deliberately not staged: theme 5 (194 files of bot/module feature work).**
`modules/mod-playerbots` and `modules/mod-dungeon-clear` are ours by policy and
have diverged 102 and 92 files respectively on upstream's side alone, on top of
our 218-file delta. There is no ordering of that work that is cheaper than
triaging individual features on demand against a frozen upstream ref (stage 1),
and after 2026-09-30 nothing new arrives. The AHBot rewrite is the one candidate
worth its own issue if AHBot behaviour is currently a problem; otherwise the
existing AHBot stays.

## What was not verified

- **Nothing was built.** No C++ toolchain on this host, and a full core build in
  a container is hours. The merge is known not to compile for one specific
  reason (trap 2) and was abandoned before resolution, so there is no merge
  artifact to build. No claim is made about whether the other 29 conflicts
  resolve to compiling code.
- **No runtime behaviour was tested.** In particular the 49 silent auto-merges
  in `PlayerbotAI`/`Player`/`TravelMgr` are unassessed beyond being named.
- **ManTech was not benchmarked** and cannot be here.
- Upstream's archival is an announcement, not an observed state
  (`archived: false` as of 2026-09-27).
