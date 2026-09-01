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
| deployment, CI, docs, ADRs, tests | `twow-repo` |

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

Read `UPSTREAM.lock`. Two things are already known and neither is small:

- Upstream is **379 commits ahead** of the fork point — 1,178 files, +851k lines,
  most of it vendoring the Eluna Lua engine.
- Upstream now carries **its own copy of the bot tree** at `src/modules/PlayerBots`,
  the path this project vacated by promoting it to `modules/mod-playerbots` in
  `twow-repo`. Two divergent copies of ike3's tree is a collision that needs a
  decision, not a merge strategy.

## Building

This repository does not build standalone yet. It still carries
`cmake/ConfigureModules.cmake` and the module-framework parts of the root
`CMakeLists.txt`, because the core cannot configure without them — separating
that cleanly, and deciding how `twow-repo` consumes this as a submodule, is the
remaining structural work.
