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

Read `UPSTREAM.lock`. Two things are already known and neither is small:

- Upstream is **379 commits ahead** of the fork point — 1,178 files, +851k lines,
  most of it vendoring the Eluna Lua engine.
- Upstream now carries **its own copy of the bot tree** at `src/modules/PlayerBots`,
  the path this project vacated by promoting it to `modules/mod-playerbots` in
  `twow-repo`. Two divergent copies of ike3's tree is a collision that needs a
  decision, not a merge strategy.

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

The core builds standalone. `cmake -S . -B build && cmake --build build --target
mangosd realmd` produces both servers from this repository alone, with no
`modules/` directory, no module framework, and no reference to `twow-repo`
anywhere in the build. `.github/workflows/ci.yml` does exactly that on every
push, and asserts the standalone property rather than assuming it.

That was not true until the extension seam landed. `cmake/ConfigureModules.cmake`
and the module framework used to live in this repository's root `CMakeLists.txt`,
and two of the core's own directories — `src/game` and `src/mangosd` — called
`GetModuleEffectiveLinkage("mod-playerbots")`, so the core could not configure
without a module framework loaded and knew one particular downstream module by
name while it was at it. All of that is `twow-repo`'s now.

What replaced it is three variables and one target, all defaulting to "nothing is
attached":

| | |
|---|---|
| `TW_EXT_SOURCE_MODULES_DIR` | source directory `AutoUpdater.cpp` scans for per-module SQL |
| `TW_EXT_MODULE_CONFIG_LIST` | extra `.conf` files `Config.cpp` reloads |
| `TW_EXT_ENABLED_MODULES` | module names `AutoUpdater.cpp` filters its SQL run on |
| `tw_core_extensions` | an `INTERFACE` library linked into `mangosd`, for a host to populate |

Plus two options, `TW_EXTERNAL_MODULE_LOADER` and `TW_EXTERNAL_PLAYERBOT_HOOKS`,
that tell the core somebody else is defining symbols it references —
`AddModulesScripts()` and the eleven playerbot host hooks. Both default OFF, and
the core compiles its own no-op stubs, which is what makes the standalone link
resolve. None of those names mentions a module, a module directory, or a module
framework: the core cannot tell whether what it is linking is `twow-repo`'s
`modules/` or something else entirely.

A standalone configure prints:

```
-- Host extensions       : none (standalone core)
```

Deciding how `twow-repo` consumes this repository — submodule, subtree, or
pinned checkout — is still open. The platform half of the split sits on
`twow-repo`'s `wip/core-submodule` branch and has not landed.
