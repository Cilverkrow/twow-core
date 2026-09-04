# Testlab pipeline (Windows)

One PowerShell script that takes a Windows machine from "nothing set up" to a running
Turtle-WoW testlab with playerbots: it verifies the client data, installs the vcpkg
dependencies, clones/updates the source, builds it, lays out the server directory, creates
and fills all four databases, writes the configuration files and generates the launchers.

It is the automated form of [`INSTALL-WINDOWS.md`](../../INSTALL-WINDOWS.md). Read that
document if you want to know *why* any particular step is there — this one only tells you
how to run it.

**Windows only.** PowerShell 5.1 or newer, and it drives MSVC. On Linux, follow
[`INSTALL-LINUX.md`](../../INSTALL-LINUX.md) instead.

---

## What you have to supply

The script automates everything it can, but four things cannot be downloaded for you:

| What | Where it goes | Notes |
|---|---|---|
| **Client data** | `<testlab>\server\data\` | `dbc`, `maps`, `vmaps`, `mmaps`, extracted from a **Turtle WoW 1.18.1 client, build 7272**. See `INSTALL-WINDOWS.md` §4. |
| **Portable MariaDB** | `<testlab>\server\mariadb-10.3.39-winx64\` | Any portable MariaDB works — pass `-MariaDbFolderName` if your folder is named differently. It must already be initialised and startable. |
| **Visual Studio 2022** | — | Workload *Desktop development with C++*. |
| **CMake ≥ 3.16 and Git** | on `PATH` | — |

vcpkg itself is not installed for you either, but the packages inside it are: point
`-VcpkgDirectory` at your vcpkg checkout and the script installs ACE and the ten Boost
libraries the playerbots module needs.

## Folder layout

The script works inside a *testlab root* that holds the source checkout and the server
side by side:

```
<testlab root>\
├─ Setup-Testlab.ps1          <- this script (when you copy it out)
├─ dbc_verifier.json          <- DBC hash manifest (ships next to the script)
├─ tortoise-wow\              <- created by the script (git clone)
├─ server\
│  ├─ data\                   <- YOU provide: dbc, maps, vmaps, mmaps
│  ├─ mariadb-10.3.39-winx64\ <- YOU provide: portable MariaDB
│  ├─ bin\  etc\  lib\  ...   <- created by the script
│  └─ 1.Start mysql.bat …     <- created by the script
├─ pipeline_console.log       <- full transcript of every run
└─ server_build.log           <- compiler output of the last build
```

## Running it

Two ways, both fine:

**Copy it out** (simplest — the testlab root is wherever the script sits):

```powershell
mkdir C:\WOW\testlab
copy tools\testlab_pipeline\Setup-Testlab.ps1  C:\WOW\testlab\
copy tools\testlab_pipeline\dbc_verifier.json  C:\WOW\testlab\
cd C:\WOW\testlab
# put server\data\ and server\mariadb-…\ in place first, then:
.\Setup-Testlab.ps1
```

**Run it in place** from a checkout, pointing at a testlab elsewhere:

```powershell
.\tools\testlab_pipeline\Setup-Testlab.ps1 -WorkspaceRoot C:\WOW\testlab
```

Start MariaDB (`server\1.Start mysql.bat`) before running — the pipeline needs it up.

### Parameters

| Parameter | Default | What it does |
|---|---|---|
| `-WorkspaceRoot` | the script's folder | Testlab root, as laid out above. |
| `-VcpkgDirectory` | `C:\WOW\vcpkg` | vcpkg checkout providing ACE + Boost. |
| `-RootPassword` | `mangos` | MariaDB `root` password. |
| `-DbPassword` | `mangos` | Password for the `mangos` service user the script creates. |
| `-MariaDbFolderName` | `mariadb-10.3.39-winx64` | Portable MariaDB folder name inside `server\`. |
| `-SkipBotRegen` | off | Keeps existing characters/accounts: dumps `tw_char` + `tw_logon` first and restores them at the end. |
| `-applyPatches` | — | Semicolon-separated commit hashes to cherry-pick from the Penqle remote, e.g. `-applyPatches "0ee0748;abc1234"`. |

Nothing needs editing inside the script — override on the command line instead:

```powershell
.\Setup-Testlab.ps1 -VcpkgDirectory D:\vcpkg -RootPassword "hunter2" -SkipBotRegen
```

## What a run does

| Step | |
|---|---|
| 00 | Variables, run lock, temporary MariaDB credential files |
| 01 | Client data present + every DBC checked against `dbc_verifier.json` |
| 02 | `vcpkg install` for ACE and Boost |
| 03 | Clone or pull the source, update submodules; optional cherry-picks |
| — | *(`-SkipBotRegen`)* verified `mysqldump` of `tw_char` + `tw_logon` |
| 04 | Stop running servers, wipe generated server dirs, drop databases |
| 05 | `create_databases.sql`, then all 186 world files from `sql\base` |
| 06 | Create the `mangos` database user |
| — | *(`-SkipBotRegen`)* restore the dumps |
| 07 | `character_inventory_copy` honor hotfix table |
| 08 | CMake configure + Release build → `server_build.log` |
| 09 | Install, sort binaries into `bin\`/`tools\`, DLLs into `lib\`, configs into `etc\` |
| 10 | Rewrite paths in `mangosd.conf` |
| 11 | Import the playerbots module SQL |
| 12 | Scale the bot population down in `aiplayerbot.conf` |
| 13 | Insert the local realm into `tw_logon.realmlist` |
| 14 | Create `logs\`, `honor\`, `pdump\`, `lua_scripts\` |
| 15 | Generate the three launcher `.bat` files (only if missing) |

Afterwards: `server\1.Start mysql.bat`, then `2.Realm server.bat`, then
`3.World server.bat`. Create your account from the mangosd console with `account create`.

### Build flags it passes

Two of these are easy to get wrong by hand and are the reason a manual Windows build
often fails where Linux succeeds:

- `-DUSE_PCH=OFF -DUSE_PCH_OLD=OFF` — the `/FI` force-include fallback only runs when
  `USE_PCH_OLD` is off, and it defaults to **on** for MSVC.
- `-DBUILD_PLAYERBOTS=ON` alongside `-DMODULE_MOD_PLAYERBOTS=static` — the module's
  sources compile either way, but without `BUILD_PLAYERBOTS` it never receives its
  compile definitions or the `botpch.h` force-include.

## Safety behaviour

- **Single instance per machine.** The pipeline drops databases and wipes the server
  directory, so two overlapping runs would corrupt the result. A named mutex
  (`Global\TortoiseWoW-Testlab-Pipeline`, falling back to `Local\` when the account lacks
  the privilege to create a global object) refuses the second run with exit code 2. The
  kernel releases it the moment the owning process ends, so a run killed with Ctrl+C or
  Task Manager cannot lock the testlab out.
  Beside it, `pipeline_running.lock` records PID, start time, user and machine so you can
  see *who* is running; a file left behind by a killed run is recognised as stale and taken
  over automatically.
- **`-SkipBotRegen` verifies its own backup.** Step 05 imports `create_databases.sql`,
  which carries `DROP TABLE` for every table in `tw_char`, so the character data is
  genuinely dropped and restored rather than left alone. The dump is therefore checked for
  a non-trivial size and mysqldump's `-- Dump completed` trailer *before* anything
  destructive runs; if it looks wrong the pipeline stops with your data still intact.
- **No passwords on the command line.** Credentials go into temporary MariaDB option files
  (readable only by the current user, deleted on exit) rather than `-p<password>`
  arguments, which any local user can read out of the process list.
- **Uncommitted source changes are stashed, not discarded**, when `-applyPatches` needs a
  clean tree. Recover them with `git -C tortoise-wow stash pop`.

## Known caveats

- **Database migrations.** The pipeline points `Database.AutoUpdate.Path` at the
  repository's `sql\database_updates` and leaves the rest to the server's auto-updater.
  `INSTALL-WINDOWS.md` §5 explains when that is not enough and how to apply the migrations
  by hand.
- **Full rebuild every run.** Step 08 deletes `build\` before configuring, so every run is
  a cold build of the whole tree. That is deliberate — it is what makes the result
  reproducible — but it costs the usual half hour or more.
