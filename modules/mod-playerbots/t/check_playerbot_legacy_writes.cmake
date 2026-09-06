# Registered as the ctest `playerbot_legacy_event_write_guard`. Run with
#   cmake -DPB_MODULE_DIR=<modules/mod-playerbots> -P this-file
#
# What it protects
# ----------------
# Every runtime write to the PlayerBot event store has to go through
# ai::PlayerbotDatabaseContract -- EventUpsertSql / EventDeleteSql, whose table
# name comes from sPlayerbotAIConfig.eventStoreTable and is therefore
# repointable by configuration and reloadable with .reload config. A .cpp that
# spells `ai_playerbot_random_bots` into a write statement itself opts out of
# all of that: it keeps writing to the character-database table no matter what
# the deployment configured, and it does so without a compile error, without a
# test failure and without a log line. Grepping is the only mechanical thing
# that catches it, so grepping is what this does. A hit is a FATAL_ERROR.
#
# The same hazard is why the pattern also fires on a schema-qualified spelling.
# Hardcoding `cv_bots`.`ai_playerbot_random_bots` is no better than hardcoding
# the bare name; it is the hardcoding that is the defect.
#
# What it deliberately does NOT scan, and why
# -------------------------------------------
# twow-repo's copy of this guard also scanned sql/other/delete_randombots.sql,
# delete_all_randombots.sql and reset_randombots.sql, because in that repository
# the event store had been moved to a dedicated `cv_bots` schema and any
# remaining reference to the unqualified table was by definition stale.
#
# Core has not made that move. `ai_playerbot_random_bots` in the character
# database IS core's default (PlayerbotAIConfig.cpp:
# GetStringDefault("AiPlayerbot.EventStoreTable", "ai_playerbot_random_bots")),
# and those three files are standalone administrative scripts a DBA pipes into
# a client by hand -- they have no config to read and nothing to honour. Porting
# the SQL half of the guard unchanged would have made this test FATAL_ERROR on
# the very first run against an unmodified core, which is not a guard, it is a
# broken build. So the scan is the source tree only, and the SQL files are left
# to the migration that would repoint them.
#
# If core ever adopts a dedicated event-store schema, add those three files back
# to the scanned set in the same commit that rewrites them.

if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "PB_MODULE_DIR is required")
endif()

file(GLOB_RECURSE runtime_files
  "${PB_MODULE_DIR}/src/*.cpp"
  "${PB_MODULE_DIR}/src/*.h")

if(NOT runtime_files)
  # An empty glob would make this test pass by scanning nothing, which is the
  # one failure mode a guard must never have.
  message(FATAL_ERROR
    "No sources found under ${PB_MODULE_DIR}/src -- the legacy event write "
    "guard scanned nothing and cannot report a pass.")
endif()

# Matched against the UPPERCASED file content, so the pattern is uppercase and
# the check is case-insensitive. The optional group ahead of the table name is
# a schema qualifier (`cv_bots`., tw_char., ...): qualified or not, a literal
# table name in a write statement is the thing being caught.
set(legacy_write_pattern
  "(DELETE[ \t\r\n]+FROM|INSERT[ \t\r\n]+INTO|UPDATE|REPLACE[ \t\r\n]+INTO|TRUNCATE([ \t\r\n]+TABLE)?)[ \t\r\n`]*([A-Z0-9_]+[.`]+)?AI_PLAYERBOT_RANDOM_BOTS")

foreach(path IN LISTS runtime_files)
  file(READ "${path}" content)
  string(TOUPPER "${content}" content_upper)
  string(REGEX MATCH "${legacy_write_pattern}" forbidden "${content_upper}")
  if(forbidden)
    message(FATAL_ERROR
      "Legacy PlayerBot event write found in ${path}: ${forbidden}\n"
      "Route it through ai::PlayerbotDatabaseContract (EventUpsertSql / "
      "EventDeleteSql) so it honours AiPlayerbot.EventStoreTable.")
  endif()
endforeach()

list(LENGTH runtime_files runtime_file_count)
message(STATUS
  "PlayerBot legacy event write guard passed (${runtime_file_count} files scanned)")
