# `cmake -P` runs this as a script, so no project() call sets policies for us
# and CMP0057 defaults to OLD -- under which `if(x IN_LIST list)` is not an
# operator at all but three unknown arguments, and the script dies with
# "Unknown arguments specified" rather than doing its job. That is exactly how
# this failed its first CI run while passing locally on a newer cmake.
cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0057 NEW)

# Registered as the ctest `sql_update_dirs_reachable`. Run with
#   cmake -DTW_CORE_ROOT=<repo root> -P this-file
#
# What it protects
# ----------------
# The DB auto-updater reads exactly three directories, and it computes them
# from configuration:
#
#   Database.AutoUpdate.Path / Database.AutoUpdate.AuthUpdateName
#   Database.AutoUpdate.Path / Database.AutoUpdate.CharUpdateName
#   Database.AutoUpdate.Path / Database.AutoUpdate.WorldUpdateName
#
# (plus a "cn" subdirectory of each, read only when NiHao = 1 -- see
# AutoUpdater::ProcessUpdates.) A .sql file anywhere else under sql/ is not a
# migration in any operational sense: nothing applies it, nothing reports that
# it was skipped, and no test fails. It just sits there looking applied.
#
# That is not hypothetical. sql/character_updates/ accumulated four migrations
# over months -- including one whose own header describes the ER_LOCK_DEADLOCK
# 1213 storm it fixes -- and not one of them ever ran on any deployment,
# because CharUpdateName is "character" and the path is
# "../../sql/database_updates/", i.e. sql/database_updates/character/. The
# directory name was one plausible-looking word away from being read, and
# nothing in the tree said so. This test is what says so.
#
# Why it is derived and not hardcoded
# -----------------------------------
# The reachable set is parsed out of src/mangosd/mangosd.conf.dist.in, the
# config a deployment actually ships. Hardcoding "database_updates/character"
# here would mean a future commit could repoint the config at a different
# directory and leave this guard cheerfully validating the old one -- the same
# class of silent drift the guard exists to catch. Repointing the config
# repoints the guard.
#
# The allowlist, and the rule for adding to it
# --------------------------------------------
# Some directories under sql/ hold SQL that is deliberately not auto-applied.
# Each is listed below WITH THE NAME OF WHAT READS IT. That comment is the
# whole point of the allowlist: an entry with no named reader is this bug
# re-forming under a different directory name. If you cannot name a reader,
# the directory is dead and belongs in database_updates/<target>/ or deleted,
# not on this list.

if(NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

set(sql_root "${TW_CORE_ROOT}/sql")
set(conf_file "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in")

if(NOT EXISTS "${conf_file}")
  message(FATAL_ERROR
    "Cannot read ${conf_file} -- the reachable migration directories are "
    "derived from it and this guard cannot report a pass without it.")
endif()

# --------------------------------------------------------------------------
# 1. Derive the reachable set from the shipped config.
# --------------------------------------------------------------------------

file(STRINGS "${conf_file}" conf_lines)

function(tw_conf_value key out_var)
  set(found "")
  foreach(line IN LISTS conf_lines)
    string(REGEX REPLACE "\r$" "" line "${line}")
    # Settings only. A commented-out line documents a default; it does not set
    # one, and treating it as a setting would let a stale comment define what
    # this guard considers reachable.
    if(line MATCHES "^[ \t]*#")
      continue()
    endif()
    if(line MATCHES "^[ \t]*${key}[ \t]*=[ \t]*(.*)$")
      set(found "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  string(REGEX REPLACE "[ \t]+$" "" found "${found}")
  string(REGEX REPLACE "^\"(.*)\"$" "\\1" found "${found}")
  set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

tw_conf_value("Database\\.AutoUpdate\\.Path" conf_path)
tw_conf_value("Database\\.AutoUpdate\\.AuthUpdateName" conf_auth)
tw_conf_value("Database\\.AutoUpdate\\.CharUpdateName" conf_char)
tw_conf_value("Database\\.AutoUpdate\\.WorldUpdateName" conf_world)

foreach(pair "Database.AutoUpdate.Path;conf_path"
             "Database.AutoUpdate.AuthUpdateName;conf_auth"
             "Database.AutoUpdate.CharUpdateName;conf_char"
             "Database.AutoUpdate.WorldUpdateName;conf_world")
  list(GET pair 0 pair_key)
  list(GET pair 1 pair_var)
  if(NOT ${pair_var})
    message(FATAL_ERROR
      "${pair_key} is not set in ${conf_file}. The auto-updater falls back to "
      "a built-in default when a setting is absent, so this guard can no "
      "longer tell which directories are read. Restore the setting, or teach "
      "this script the same fallback AutoUpdater::ProcessUpdates uses.")
  endif()
endforeach()

# Database.AutoUpdate.Path is relative to the server's working directory
# (".../bin"), not to the repository, so it cannot be joined to TW_CORE_ROOT
# directly. What it does contain is the repository-relative tail: everything
# from the "sql/" component onward. Anchor on that.
string(REGEX REPLACE "\\\\" "/" conf_path "${conf_path}")
string(REGEX REPLACE "/+$" "" conf_path "${conf_path}")
if(NOT conf_path MATCHES "(^|/)sql(/(.*))?$")
  message(FATAL_ERROR
    "Database.AutoUpdate.Path is \"${conf_path}\", which does not name a path "
    "under this repository's sql/ directory. This guard maps that setting onto "
    "the source tree and cannot do so any more. Either restore a path under "
    "sql/, or retire this test in the same commit that moves the migrations "
    "out of the repository.")
endif()
set(updates_subdir "${CMAKE_MATCH_3}")

# The three target directories, plus the per-region "cn" child of each, which
# ProcessUpdates walks when NiHao = 1.
set(reachable_dirs "")
foreach(target "${conf_auth}" "${conf_char}" "${conf_world}")
  if(updates_subdir)
    set(target_dir "${updates_subdir}/${target}")
  else()
    set(target_dir "${target}")
  endif()
  list(APPEND reachable_dirs "${target_dir}" "${target_dir}/cn")
endforeach()

# --------------------------------------------------------------------------
# 2. The allowlist. EVERY entry names its reader.
# --------------------------------------------------------------------------
#
# Paths are relative to sql/. "." is sql/ itself.
set(allowed_dirs
  # sql/create_databases.sql -- read by sql/setup_databases.sh and
  # sql/setup_databases.bat, which pipe it into the client before anything else.
  "."

  # The bootstrap import: sql/setup_databases.sh and setup_databases.bat glob
  # "${UPDATES_DIR}"/*.sql -- the top level of database_updates only, not its
  # subdirectories. INSTALL-LINUX.md and INSTALL-WINDOWS.md document the same
  # loop by hand. These are world-schema baselines applied once at install time,
  # ahead of the auto-updater.
  "${updates_subdir}"

  # The world content dump: 186 files imported by hand at install time.
  # Named readers: README.md ("Manually import all sql scripts in the sql/base
  # folder"), INSTALL-LINUX.md, INSTALL-WINDOWS.md, PLAYERBOTS_QUICKSTART.md.
  # Deliberately never auto-applied -- it is 131 MB of content, not migrations.
  "base"

  # Operator scripts run by hand against per-server data, plus the Python
  # auditing tools that read them. Named readers: README.md ("Two are
  # deliberately manual, in sql/tools/, because both depend on per-server
  # data"), INSTALL-LINUX.md, and sql/tools/probe_migration_overlap.py /
  # audit_migration_content.py / extract_missing_templates.py.
  "tools"

  # Work-in-progress SQL, staged per table until it is folded into a real
  # migration. Named reader: CONTRIBUTING.md ("Any SQL updates should be placed
  # in sql/wip_updates/ ... They will be merged into a proper full update when
  # deemed appropriate"). Not applied by anything, on purpose.
  "wip_updates"

)

# --------------------------------------------------------------------------
# 3. Glob what is actually on disk and compare.
# --------------------------------------------------------------------------

file(GLOB_RECURSE sql_files RELATIVE "${sql_root}" "${sql_root}/*.sql")

if(NOT sql_files)
  # An empty glob would make this test pass by scanning nothing, which is the
  # one failure mode a guard must never have.
  message(FATAL_ERROR
    "No .sql files found under ${sql_root} -- the migration reachability "
    "guard scanned nothing and cannot report a pass.")
endif()

set(sql_dirs "")
foreach(rel IN LISTS sql_files)
  get_filename_component(dir "${rel}" DIRECTORY)
  if(NOT dir)
    set(dir ".")
  endif()
  list(APPEND sql_dirs "${dir}")
endforeach()
list(REMOVE_DUPLICATES sql_dirs)
list(SORT sql_dirs)

set(orphans "")
foreach(dir IN LISTS sql_dirs)
  if(dir IN_LIST reachable_dirs)
    continue()
  endif()
  if(dir IN_LIST allowed_dirs)
    continue()
  endif()
  list(APPEND orphans "${dir}")
endforeach()

if(orphans)
  string(REPLACE ";" "\n    sql/" orphan_text "${orphans}")
  string(REPLACE ";" ", " reachable_text "${reachable_dirs}")
  message(FATAL_ERROR
    "Unreachable SQL directories under sql/:\n"
    "    sql/${orphan_text}\n"
    "Nothing applies the .sql files in there. The DB auto-updater reads only:\n"
    "    ${reachable_text}\n"
    "(derived from Database.AutoUpdate.Path / AuthUpdateName / CharUpdateName / "
    "WorldUpdateName in src/mangosd/mangosd.conf.dist.in, plus the per-region "
    "cn/ child of each.)\n"
    "Either move the files into the target directory for their database, or "
    "add the directory to allowed_dirs in this script WITH A COMMENT NAMING "
    "WHAT READS IT. An allowlist entry with no named reader is the defect this "
    "guard exists to catch.")
endif()

list(LENGTH sql_files sql_file_count)
list(LENGTH sql_dirs sql_dir_count)
message(STATUS
  "SQL update directory reachability guard passed "
  "(${sql_file_count} files in ${sql_dir_count} directories under sql/).")
