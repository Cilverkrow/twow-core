# twow-core

The server core for the Cilverkrow Turtle-WoW project. A genuine fork of
[Shyalya/tortoise-wow](https://github.com/Shyalya/tortoise-wow), sharing its real
history, so `git merge upstream/playerbots-integration-gh` is an ordinary
operation here.

That sentence is the entire reason this repository exists. The project's other
repository, `twow-repo`, had its history rewritten with `git-filter-repo` to strip
binaries, which severed it from upstream permanently: there is no merge base, so
every upstream update was hand-resolution in the files that change most often
upstream — `Player.cpp`, `World.cpp`, `Unit.cpp`, `WorldSession.cpp`. And the
fork's genuinely upstream-worthy bug fixes could not be offered back in that
shape, so they were carried forever.

## What lives here, and what does not

| | where |
|---|---|
| the server core — `src/game`, `src/shared`, `src/framework`, `mangosd`, `realmd` | **here** |
| upstream's world data and migrations | **here** |
| project modules (`mod-playerbots`, `mod-dungeon-clear`, `mod-donation`, …) | `twow-repo` |
| deployment, docs, ADRs, tests | `twow-repo` |
| CI that proves the core compiles standalone | **here** (`.github/workflows/ci.yml`) |

Feature code that used to be spliced into upstream files is deliberately **not**
re-applied here. `AutoWorldBuff`, `AutoDonationPoints`, `Leech` and
`SoloDungeonRepop` lived inside `World::Update()`, `Unit::DealDamage()` and
`Player::RepopAtGraveyard()` with no file of their own; they are modules in
`twow-repo` now, and that interleaving — not the size of the delta — was what
actually made upstream merges painful.

## The delta, honestly

Six commits sit on top of `61a8269`. Four are classified:

1. **the script hook system** — `ScriptObjects.h`, `ScriptMgr`, `ModuleSlots`.
   The integration surface, deliberately the smallest part that must live in the
   core.
2. **the cmangos/AzerothCore compatibility shim** — 1,074 lines across seven core
   headers, giving this core's classes the names the vendored bot tree expects.
   It cannot move module-side: it is member functions on core classes, and a
   module cannot add a member to a core class.
3. **the playerbot host seam** — eleven free functions and the stubs that make
   the bot module optional.
4. **header self-containment fixes** — headers that used a type they never
   declared, which compiled only because include order happened to be lucky.

The fifth removes Penqle's superseded `PlayerBotAI`/`PlayerBotMgr` stubs.

**The sixth is 102 files labelled unclassified, and that is the honest state.**
Splitting it is the work this repository exists to enable and it has not been
done. It contains roughly 33 upstream-worthy bug fixes that should become
individual pull requests to Penqle, integration hooks that stay here, and some
divergence that should be reverted or explained. Nothing in it should be sent
upstream in its current shape.

## Before the first upstream merge

Read `UPSTREAM.lock`. Three things are already known and none of them is small:

- Upstream is several hundred commits ahead of the fork point. The exact figure
  deliberately does **not** appear here any more: this file said **379** for the
  life of the fork, that number was measured against `be3e6cd`, and `be3e6cd`
  stopped being upstream's tip 385 commits ago. A count written into prose goes
  stale in silence. The `upstream-freshness` CI job measures it against the live
  branch on every run instead.
- The branch is `playerbots-integration-gh`, and only that branch. Upstream's
  `main`, `dev`, `1181dev`, `challenges`, `shop` and `1181-rogue-fixes` are all
  **ancestors of this fork's own fork point** — merging any of them is a no-op
  against code we already have. That has been attempted once already, which is
  why the rule now lives in `UPSTREAM.lock` where CI reads it.
- Upstream **independently built the module system.** Their `8415f1b` moved
  playerbots to `modules/mod-playerbots`, arriving at the same layout this
  project had built for itself; their tree now ships `modules/`,
  `modules/CMakeLists.txt`, `modules/mod-dungeon-clear` and `modules/templates`,
  and their core owns the script-hook seam (`ScriptObjects.h`, `ModuleSlots.h`,
  `PlayerbotStubs.cpp`). This is convergence, not collision — and it means
  upstream's arrangement, not ours, is the reference for anything structural.

## Getting set up

Clone, then add the remote this fork was made from. GitHub records a fork
relationship, but `git` does not: a clone of this repository has only `origin`,
and `git merge upstream/playerbots-integration-gh` — the operation this
repository exists to make possible — cannot run without it. It is local git
config, so it is not carried by the clone and every contributor adds it once:

```sh
git remote add upstream https://github.com/Shyalya/tortoise-wow.git
git fetch upstream
```

The URL and the branch to track are also recorded in `UPSTREAM.lock`, which is
the machine-readable copy CI can assert against; the command above is the human
one.

## Building

`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
--target mangosd realmd` produces both servers. `.github/workflows/ci.yml` does
exactly that on every push, on `debian:trixie`, which is the toolchain this
project actually ships from.

### The pure core is superseded. The mergeable core is not.

This section used to claim something stronger: that the core built with **no
`modules/` directory at all**, and that CI asserted it. That property is gone on
purpose, and the distinction it collapsed is worth keeping straight, because two
different ideas were travelling under one word.

A **mergeable** core — branched from the real fork point, sharing upstream's
history, so `git merge upstream/playerbots-integration-gh` is an ordinary
operation — is the entire reason this repository exists. Nothing here touches it,
and nothing should.

A **pure** core — `modules/` physically deleted, so the core could not reach the
platform even by accident — was an addition on top of that, and it was a
reasonable one for exactly as long as upstream had no module system of its own.

Upstream built one anyway (`8415f1b`, see above). From that point on, purity was
no longer a property we maintained; it was a property we had to re-win against
upstream on every merge. Measured against `playerbots-integration-gh`, it
accounted for **8 of the 24 conflicts** — and the conflicts were the visible
half. The quiet half is worse: our deletion of `cmake/ConfigureModules.cmake` and
`modules/CMakeLists.txt` versus upstream's *unmodified* copies merges to "still
deleted" with no conflict at all, taking upstream's build with it, and reports
success while doing so.

Being 385 commits behind is not a position from which to insist on a structural
preference. `modules/` and `cmake/ConfigureModules.cmake` are back, upstream's
build arrangement is the reference, and the CI step that asserted the standalone
property has been removed along with the property.

What survives from the extension seam is whatever the merged tree genuinely uses
— `TW_EXT_SOURCE_MODULES_DIR`, `TW_EXT_MODULE_CONFIG_LIST`,
`TW_EXT_ENABLED_MODULES`, `tw_core_extensions`, `TW_EXTERNAL_MODULE_LOADER` and
`TW_EXTERNAL_PLAYERBOT_HOOKS` — and no more than that. Where upstream's
`CMakeLists.txt` already answers a question, upstream's answer wins.

Deciding how `twow-repo` consumes this repository — submodule, subtree, or
pinned checkout — is still open. The platform half of the split sits on
`twow-repo`'s `wip/core-submodule` branch and has not landed.

`docs/adr/ADR-0020-two-repo-upstream-split.md` lives in `twow-repo`, not here.
Its pure-core rationale is superseded by this section; the record on that side
needs the same correction.
