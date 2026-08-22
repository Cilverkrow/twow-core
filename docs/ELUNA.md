# Eluna integration

This branch integrates [Eluna](https://github.com/ElunaLuaEngine/Eluna) into
the Turtle WoW MaNGOS core as the `src/modules/Eluna` Git submodule. The
submodule is pinned to commit `1b06f28ff3a00054d915d824c725fb4283fee74d`, the
revision shared by the maintained VMaNGOS and cMaNGOS integrations when this
port was prepared.

The host side targets Eluna's VMaNGOS API (`ELUNA_VMANGOS`) and the vanilla
client expansion (`ELUNA_EXPANSION=0`). Do not replace those definitions with
the generic MaNGOS adapter: this repository has Turtle-specific stores,
timers, GUID handling, script objects, and gameplay extensions.

## Checkout

Clone recursively, or initialize Eluna after cloning:

```sh
git submodule update --init --recursive src/modules/Eluna
```

CMake stops with an actionable error when Eluna is enabled but the submodule
has not been initialized.

## Configure and build

Eluna and its Lua 5.2 runtime are enabled by default:

```sh
cmake -S . -B build -DBUILD_ELUNA=ON -DELUNA_LUA_VERSION=lua52
cmake --build build --config Release --target mangosd
ctest --test-dir build -C Release --output-on-failure
```

`ELUNA_LUA_VERSION` accepts `lua51`, `lua52`, `lua53`, or `lua54`. Lua source
archives are downloaded from lua.org with pinned SHA-256 checksums and linked
statically. `BUILD_ELUNA_TESTS=ON` builds a small runtime smoke test. Use
`BUILD_ELUNA=OFF` for a core-only regression build.

`BUILD_ELUNA` is the compile-time switch; `Eluna.Enabled` is the independent
runtime switch. A build that contains Eluna can therefore run with Lua disabled
without needing a second binary.

Windows still requires the normal ACE development dependency. For example,
pass its prefix as `-DACE_ROOT=C:/path/to/ace` when CMake cannot discover it.

## Runtime configuration

The distributed `mangosd.conf` enables Eluna and reads scripts from
`./lua_scripts`, relative to the server working directory. The relevant keys
are:

- `Eluna.Enabled`
- `Eluna.TraceBack`
- `Eluna.ReloadCommand`
- `Eluna.UseUnsafeMethods`
- `Eluna.UseDeprecatedMethods`
- `Eluna.OnlyOnMaps`
- `Eluna.ScriptPath`
- `Eluna.RequirePaths`
- `Eluna.RequireCPaths`
- `Eluna.ReloadSecurityLevel`
- `ElunaErrorLogFile`

Place server scripts under `lua_scripts`. Eluna's bundled extensions are
installed into that directory by the CMake install target. With reloads
enabled, an authorized account can use `.reload eluna`.

## Turtle-specific architecture

Eluna owns one global Lua state and optional per-map states. World and map
lifecycle calls are wired directly into the core so state creation, updates,
and teardown stay ordered. Other events are bridged through Turtle's existing
`ScriptObject` registries where possible, preserving the existing C++ script
precedence and cancellation behavior. Hooks not represented by that registry
are placed at their concrete gameplay lifecycle points.

Lua state access is single-threaded. When Eluna is enabled, a map configured
for parallel motion, object, or visibility updates is forced back to the
single-threaded path. This is intentional: invoking one Lua state concurrently
would corrupt its stack and event queues.

The integration includes:

- global/map state lifecycle, configuration reload, startup, shutdown, and
  timed updates;
- creature Lua AI fallback and instance data fallback after native C++ scripts;
- player, chat/command, packet, loot, trade, mail, group, guild, auction,
  battleground, weather, and game-event dispatch;
- creature, gameobject, item, gossip, quest, area-trigger, summon, and dummy
  spell-effect dispatch;
- Eluna object event processors and the VMaNGOS method bindings required by
  the pinned engine revision.

## Verification checklist

Before merging an Eluna update:

1. Confirm `git submodule status src/modules/Eluna` reports the expected commit.
2. Configure and build `mangosd` with `BUILD_ELUNA=ON`.
3. Run `ctest`; `eluna.lua-runtime` must pass.
4. Configure and build `mangosd` with `BUILD_ELUNA=OFF`.
5. Start a configured development realm, confirm the Eluna startup banner and
   no errors in `ElunaErrors.log`, load a small login hook, and exercise
   `.reload eluna`.

A live realm smoke test requires the local authentication, character, and
world databases and is intentionally not encoded into CTest.

## Updating the submodule

Treat an Eluna revision change as an API update, not a routine dependency bump.
Move the submodule deliberately, record the new commit in the parent repository,
then repeat the complete verification checklist above. In particular, review
the VMaNGOS bindings for changed method signatures or newly registered userdata
before adapting Turtle-specific core types.

```sh
git -C src/modules/Eluna fetch origin
git -C src/modules/Eluna checkout <reviewed-commit>
git add src/modules/Eluna
```

## Local Turtle WoW realm smoke test

The integration was verified on Windows with an isolated, loopback-only test
realm. The local environment is deliberately not tracked. It used:

- a portable x64 MariaDB 12.3.3 instance on `127.0.0.1:3310`;
- `realmd` on `127.0.0.1:3724` and `mangosd` on `127.0.0.1:8090`;
- an x64 ACE prefix supplied through `ACE_ROOT`;
- all 190 world dumps from `sql/base`, followed by the canonical world and
  character migrations under `sql/database_updates`;
- maps, vmaps, and DBC files from the matching Turtle WoW client in the
  repository-level, ignored `data` directory; and
- generated server configs with `Eluna.Enabled = 1` and a local
  `lua_scripts` directory.

Copy the required ACE and MariaDB runtime DLLs beside the Release server
binaries before starting the realm. Keep the portable database, credentials,
generated configs, client data, logs, and runtime scripts outside Git. Stock
MaNGOS 1.12 maps or DBC files are not a valid substitute for this fork's
custom data.

The live Aura smoke script created an unsaved, timed creature, applied spell
1126 (Mark of the Wild), and exercised the pinned VMaNGOS Aura API against
real server userdata: aura creation and lookup, identity/caster/owner
accessors, duration and stack mutation, and removal. Its success marker was
`ELUNA_AURA_SMOKE_PASSED`; the temporary creature left no persistent row.

## Known Aura compatibility limit

The current online Aura index lists `Aura:GetSpellInfo`, but the pinned
VMaNGOS adapter does not register the `SpellInfo` userdata layer or that Aura
method. The smoke test reports this as
`ELUNA_AURA_GETSPELLINFO_UNAVAILABLE_VMANGOS`; the other twelve documented
Aura methods are exercised directly.
