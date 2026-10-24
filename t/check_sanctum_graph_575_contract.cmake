cmake_policy(VERSION 3.16)
if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#575 (islands L1-30 #4, Ae17 world-data hotfix): Wizard's Sanctum portal nodes of the playerbot graph.
# The through-floor walk links 1312->1315 and 1313->1314 (2-point paths through 120 yd of floors) leave the graph, the
# missing tower-top walk links 1312<->1314 come in; the portal links 1314->1313 (704) and 1315->1312 (702) stay.
# db-init re-creates the playerbot tables from the module base dump after the migrations, so the dump must carry the
# same end state.
# 1. Migration: LF, issue, Rollback, table guard by DISTINCT table names, node position guard, backup before the
#    deletes, deletes only backed-up rows, INSERT IGNORE of the new rows, end-state CHECK, nothing forbidden; the
#    playerbot tables are only named inside prepared statements (fresh bootstrap, twow-repo#634).
# 2. Base dump = migrated end state.
set(mig "${TW_CORE_ROOT}/sql/database_updates/20261023000000_world.sql")
set(dump "${TW_CORE_ROOT}/modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql")

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#575 sanctum ${label}: missing ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "#575 sanctum ${label}: must not contain ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#575 sanctum ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# 1. Migration
if (NOT EXISTS "${mig}")
  message(FATAL_ERROR "#575 sanctum: missing ${mig}")
endif()
file(READ "${mig}" text)
file(READ "${mig}" hex HEX)
if (hex MATCHES "^(..)*0d")
  message(FATAL_ERROR "#575 sanctum migration: CRLF")
endif()
require_text("${text}" "twow-repo#575" "issue")
require_text("${text}" "-- Rollback (exact, mangosd stopped):" "rollback")
require_text("${text}" "(SELECT COUNT(DISTINCT `TABLE_NAME`) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE()" "table guard")
require_text("${text}" "SET @ps_do := (@ps_tables = 3 AND @ps_nodes = 4);" "node guard")
require_text("${text}" "TINYINT(1) NOT NULL CHECK (`ok` = 1)" "end-state check")
string(REGEX REPLACE "(^|\n)--[^\n]*" "\\1" body "${text}")
string(TOUPPER "${body}" upper)
foreach (forbidden "DROP TABLE " "TRUNCATE" "REPLACE INTO" "ALTER TABLE" "UPDATE ")
  forbid_text("${upper}" "${forbidden}" "forbidden statement")
endforeach()
require_order("${body}" "INSERT IGNORE INTO `ai_playerbot_travelnode_link_bak_575s`" "DELETE l FROM `ai_playerbot_travelnode_link` l" "link backup before delete")
require_order("${body}" "INSERT IGNORE INTO `ai_playerbot_travelnode_path_bak_575s`" "DELETE p FROM `ai_playerbot_travelnode_path` p" "path backup before delete")
require_text("${body}" "AND ((l.`node_id` = 1312 AND l.`to_node_id` = 1315) OR (l.`node_id` = 1313 AND l.`to_node_id` = 1314))" "backup selects exactly the two links")
require_text("${body}" "JOIN `ai_playerbot_travelnode_link_bak_575s` k ON k.`node_id` = l.`node_id` AND k.`to_node_id` = l.`to_node_id`" "link delete only backed-up rows")
require_text("${body}" "JOIN `ai_playerbot_travelnode_link_bak_575s` k ON k.`node_id` = p.`node_id` AND k.`to_node_id` = p.`to_node_id`" "path delete only backed-up rows")
require_text("${body}" "VALUES (1312, 1314, 1, 0, 8.894, 0, 0, 1, 0, 0, 0), (1314, 1312, 1, 0, 8.894, 0, 0, 1, 0, 0, 0)" "new links")
require_order("${body}" "DELETE l FROM `ai_playerbot_travelnode_link` l" "INSERT IGNORE INTO `ai_playerbot_travelnode_link`\n" "new links after the delete")
string(REGEX MATCHALL "DELETE " deletes "${upper}")
list(LENGTH deletes n_deletes)
if (NOT n_deletes EQUAL 2)
  message(FATAL_ERROR "#575 sanctum: expected exactly 2 DELETE statements, found ${n_deletes}")
endif()
# 1b. Fresh bootstrap (twow-repo#634): db-init applies the migration before the playerbot tables exist, and MariaDB
#     resolves every table a plain statement names, even in an IF() branch that is not taken. So a playerbot table may
#     only be named inside a quoted string (prepared statement or the information_schema guard).
string(REGEX REPLACE "'([^']|'')*'" "''" unquoted "${body}")
forbid_text("${unquoted}" "`ai_playerbot_travelnode" "playerbot table outside a prepared statement")
require_text("${body}" "SET @ps_sql := IF(@ps_tables = 3, 'SELECT COUNT(*) INTO @ps_nodes FROM `ai_playerbot_travelnode`" "node guard as prepared statement")

# 2. Base dump
file(READ "${dump}" d)
forbid_text("${d}" "\n\t(1312, 1315, 1, 0," "dump through-floor link 1312->1315")
forbid_text("${d}" "\n\t(1313, 1314, 1, 0," "dump through-floor link 1313->1314")
forbid_text("${d}" "\n\t(1312, 1315, 0, 0," "dump path 1312->1315")
forbid_text("${d}" "\n\t(1313, 1314, 0, 0," "dump path 1313->1314")
require_text("${d}" "\n\t(1312, 1314, 1, 0, 8.894, 0, 0, 1, 0, 0, 0),\n" "dump link 1312->1314")
require_text("${d}" "\n\t(1314, 1312, 1, 0, 8.894, 0, 0, 1, 0, 0, 0),\n" "dump link 1314->1312")
require_text("${d}" "\n\t(1312, 1314, 0, 0, -9014.94, 873.326, 148.616),\n\t(1312, 1314, 1, 0, -9020.09, 880.502, 147.576),\n" "dump path 1312->1314")
require_text("${d}" "\n\t(1314, 1312, 0, 0, -9020.09, 880.502, 147.576),\n\t(1314, 1312, 1, 0, -9014.94, 873.326, 148.616),\n" "dump path 1314->1312")
require_text("${d}" "\n\t(1314, 1313, 2, 704, 0.1, 0, 3, 1, 0, 0, 0),\n" "dump portal 704 kept")
require_text("${d}" "\n\t(1315, 1312, 2, 702, 0.1, 0, 3, 1, 0, 0, 0),\n" "dump portal 702 kept")

message(STATUS "#575 sanctum graph contract OK")
