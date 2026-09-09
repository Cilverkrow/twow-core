# `sql/` — what is applied, by what, and when

The one thing to know: **`sql/database_updates/<target>/` is the canonical home
for a migration.** Nothing else under `sql/` is applied automatically, and a
`.sql` file filed anywhere else will parse, review cleanly, and never run.

## The canonical directory

The DB auto-updater (`src/shared/Database/AutoUpdater.cpp`,
`AutoUpdater::ProcessUpdates`) reads exactly three directories, and it computes
them from configuration rather than from a compiled-in constant:

| Setting (`src/mangosd/mangosd.conf.dist.in`) | Shipped value |
| --- | --- |
| `Database.AutoUpdate.Path` | `../../sql/database_updates/` |
| `Database.AutoUpdate.AuthUpdateName` | `auth` |
| `Database.AutoUpdate.CharUpdateName` | `character` |
| `Database.AutoUpdate.WorldUpdateName` | `world` |

which resolves to:

| Directory | Database |
| --- | --- |
| `sql/database_updates/auth/` | login (`tw_logon`) |
| `sql/database_updates/character/` | characters (`tw_char`) |
| `sql/database_updates/world/` | world (`tw_world`) |

Each also gets a `cn/` child, walked only when `NiHao = 1` — the CN-only
region migrations.

A new migration goes in the directory for the database it changes. That is the
only placement that gets it applied.

### Two things about how the ledger works

**A migration's identity is its bytes, not its path.** `GetMigrationKey` is
`Module + ":" + hash`, and the hash is the SHA-1 of the file's contents
(`AutoUpdater.cpp`). Moving a file between directories does not replay it.
*Editing* one does: an edited file is a new hash, therefore a new migration,
and the old row stays in the ledger and is reported as "exists in DB but not as
file". Migrations are forward-only and must be replay-safe.

**The updater ignores statement results.** `ExecuteUpdate` runs each statement
and then records the migration as applied regardless of whether any of them
failed. A migration that can fail must therefore assert its own end state — see
the `CHECK` tail in
`database_updates/character/20260906120000_ai_playerbot_random_bots_unique_event_key.sql`
and the same tail in `..._random_bots_index.sql`.

### Module tables

The updater also walks `modules/<name>/data/sql/<target>/` for enabled modules.
**No such directory exists in this tree.** `modules/mod-playerbots/sql/` is an
operator-imported installer, not a migration path, and several of its files
open with `DROP TABLE IF EXISTS`. A migration that alters a module table must
therefore be table-conditional (do nothing when the table is absent), and any
index it adds should also be declared in the module's own `CREATE TABLE`, or a
re-import of the installer silently removes it and the ledger will never replay
the migration that put it there.

## Everything else under `sql/`, and what reads it

| Directory | Read by | Applied automatically? |
| --- | --- | --- |
| `database_updates/<auth\|character\|world>/` | the DB auto-updater | **yes** |
| `database_updates/*.sql` (top level) | `setup_databases.sh` / `setup_databases.bat`, which glob `database_updates/*.sql` only; `INSTALL-LINUX.md` and `INSTALL-WINDOWS.md` document the same loop | no — install-time bootstrap, once |
| `create_databases.sql` | `setup_databases.sh` / `.bat`, first | no — install-time |
| `base/` | imported by hand; `README.md`, `INSTALL-LINUX.md`, `INSTALL-WINDOWS.md`, `PLAYERBOTS_QUICKSTART.md` | no — 186 files, 131 MB of world content, deliberately never auto-applied |
| `tools/` | an operator, by hand, per server; plus `tools/probe_migration_overlap.py`, `audit_migration_content.py`, `extract_missing_templates.py` which read the migration tree | no — depends on per-server data |
| `wip_updates/` | nothing yet; `CONTRIBUTING.md` stages SQL here per table until it is folded into a real migration | no — by design |

## The guard

`t/check_sql_update_dirs_reachable.cmake`, registered as the ctest
`sql_update_dirs_reachable`, fails the build if any directory under `sql/`
contains `.sql` files and is neither reachable by the auto-updater nor on the
allowlist in that script. It derives the reachable set by parsing
`mangosd.conf.dist.in`, so repointing the config repoints the guard.

**Every allowlist entry must name what reads it.** An entry with no named
reader is `sql/character_updates/` re-forming: that directory held four
migrations for months — including one whose header describes the
`ER_LOCK_DEADLOCK` (1213) storm it fixes — and not one of them was ever applied
by any deployment, because nothing read the directory and nothing said so.
