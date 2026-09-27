# AGENTS.md: twow-core

This file is deliberately short. **The source of truth for engineering rules is
[`Cilverkrow/twow-repo` `AGENTS.md`](https://github.com/Cilverkrow/twow-repo/blob/main/AGENTS.md)**
(always read it fresh from `main`). The rules below add what is specific to this repository.
If the two contradict each other, the stricter rule applies and the contradiction gets reported (twow-repo#353).

## What lives here

- The server core (`src/game`, `src/shared`, `src/framework`, `mangosd`, `realmd`).
- **Project modules that are built into the core:** `modules/mod-playerbots`
  (bot logic, roster, BotMenu addon under `modules/mod-playerbots/addon/`), `modules/mod-dungeon-clear`.
  (`FORK-README.md` still describes the older split in which modules lived in twow-repo.
  For `mod-playerbots` the code here is authoritative.)
- World data and migrations: `sql/base`, **migrations in `sql/database_updates/<YYYYMMDDhhmmss>_<db>.sql`**.
- Deployment, config profiles, ADRs, runbooks, issues and roster data live in **twow-repo**.

## Gate and build

- The authoritative gate is the CI job **`Build core (Debian trixie)`** in `.github/workflows/ci.yml`:
  Release, `-DMODULES=static`, `-DBUILD_TESTING=ON`, ctest with a count guard. MSVC is only diagnostic.
- CI reports compiler warnings on changed lines (`tools/ci/warn_changed_tus.py`, twow-core#172).
  Report warnings with that tool instead of "0 warnings" (the release build uses `--no-warnings`).
- Local builds only in the Debian trixie toolchain, in a task-specific build directory outside the source tree.

## Conventions

- **SQL migrations:** LF line endings, idempotent (`INSERT IGNORE` / `NOT EXISTS`), a unique timestamp.
  Check before creating one that no other open PR uses the same file name.
  Live application happens individually with a ledger entry in the `migrations` table, never through auto-update.
- **Tests:** contract/policy tests under `t/` and in the module, registered in `tests.cmake`.
  Several PRs insert at the same anchor, so rebase before merging.
- **Config keys:** new keys go into `mangosd.conf.dist.in` / `aiplayerbot.conf.dist.in` with a **neutral default**
  (off/legacy behaviour). They are switched on in the twow-repo profile `config/canonical/profiles/funserver-test/`.
- **Diagnostics:** new behaviour logs a greppable line with a `[Prefix]` (for example `[ZoneEscape]`, `[ClassGrant]`).
- **Performance:** nothing unbounded in per-tick paths. No AI context values keyed per creature, item or target
  (see twow-repo#351). Locks in hot paths use a short `std::mutex` or striped locks, **no `std::shared_mutex`**
  (writer starvation, twow-core#171).
- PRs use `Refs twow-repo#…` (or `Fixes` only with full acceptance). Merge and deploy only with the owner's approval
  (see twow-repo `AGENTS.md` and the release-train issue twow-repo#319).

## Cloud sessions

Claude Code cloud sessions work on issues labelled `cloud` in twow-repo, following their "Cloud brief":
branch + PR only, **no merge, no deploy, no secrets, no live access**. Read this file and the
twow-repo `AGENTS.md` first.
