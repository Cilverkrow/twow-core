# twow-repo#551 (owner 11.10.2026): AiPlayerbot.Park.Persist (default 0). The park state lives in the existing bot event
# store (no schema change) and parked bots are parked again through ParkBot after a restart or login.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#551 park persist: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#551 park persist: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#551 park persist: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#551 park persist: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  string(REPLACE "${needle}" "" without "${text}")
  string(LENGTH "${text}" len_all)
  string(LENGTH "${without}" len_without)
  string(LENGTH "${needle}" len_needle)
  math(EXPR n "(${len_all} - ${len_without}) / ${len_needle}")
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("ParkPolicy.h" policy)

require_text("${config_h}" "bool parkPersist = false;" "switch default off")
require_text("${config_cpp}" "parkPersist = config.GetBoolDefault(\"AiPlayerbot.Park.Persist\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Park.Persist = 0" "documented")

# Write only with the switch, at both park sites, after the bot is registered.
between("${mgr}" "void RandomPlayerbotMgr::PersistPark(" "\n}\n" persist_fn)
require_order("${persist_fn}" "if (sPlayerbotAIConfig.parkPersist)" "SetEventValue(guid, ai::park::PersistEvent, 1, ai::park::PersistValidSeconds" "write behind the switch")
count_text("${mgr}" "PersistPark(bot->GetGUIDLow()," sites)
if(NOT sites EQUAL 2)
  message(FATAL_ERROR "#551 park persist: both park sites (here / inn) persist (found ${sites})")
endif()
require_order("${mgr}" "parkedBots[bot->GetGUIDLow()] = entry;\n        PersistPark(bot->GetGUIDLow(), \"here\");" "ai->SetParked(true);" "here site")

# Unpark deletes the row, on both unpark paths, also with the switch off.
between("${mgr}" "void RandomPlayerbotMgr::UnparkBot(Player* bot)" "\n}\n" unpark_fn)
require_text("${unpark_fn}" "ClearPersistedPark(bot->GetGUIDLow());" "unpark clears")
require_text("${mgr}" "ClearPersistedPark(guid);   // twow-repo#551" "console unpark of an offline bot clears")
between("${mgr}" "void RandomPlayerbotMgr::ClearPersistedPark(" "\n}\n" clear_fn)
string(FIND "${clear_fn}" "parkPersist" clear_gated)
if(NOT clear_gated EQUAL -1)
  message(FATAL_ERROR "#551 park persist: clearing must not depend on the switch (stale rows)")
endif()

# A logout (state=lost) keeps the row: the bot parks again at its next login.
between("${mgr}" "sLog.outBasic(\"[Park] state=lost bot=%u\", guid);" "continue;" lost_path)
string(FIND "${lost_path}" "ClearPersistedPark" lost_clears)
if(NOT lost_clears EQUAL -1)
  message(FATAL_ERROR "#551 park persist: a logout must keep the stored park state")
endif()

# Restore: behind the switch, throttled, through ParkBot (caps and faction checks), once per login.
between("${mgr}" "void RandomPlayerbotMgr::ProcessParkedBots()" "for (auto it = parkedBots.begin(); it != parkedBots.end();)" process_head)
require_order("${process_head}" "if (sPlayerbotAIConfig.parkPersist && now >= lastParkRestore + ai::park::RestoreIntervalSeconds)" "RestoreParkedBots();" "restore behind the switch, throttled")
between("${mgr}" "void RandomPlayerbotMgr::RestoreParkedBots()" "\n}\n" restore_fn)
require_text("${restore_fn}" "if (!ai || ai->parkRestoreChecked || IsParkedBot(guid))" "once per login, never twice")
require_text("${restore_fn}" "if (ParkBot(bot, ai::park::RestoreTarget(data.c_str()), reason))" "through ParkBot")
require_text("${restore_fn}" "else if (!ai::park::RestoreRetries(reason.c_str(), ai->parkRestoreAttempts))" "bounded retries")
string(FIND "${restore_fn}" "TeleportTo" restore_teleports)
if(NOT restore_teleports EQUAL -1)
  message(FATAL_ERROR "#551 park persist: the restore must not bypass ParkBot")
endif()

# No schema change: the existing event store only.
string(FIND "${mgr}" "CREATE TABLE" creates)
if(NOT creates EQUAL -1)
  message(FATAL_ERROR "#551 park persist: no new table (Ä15: no DB change in a hotfix train)")
endif()
require_text("${policy}" "constexpr char const* PersistEvent = \"park\";" "event key")

message(STATUS "park_persist source contract passed")
