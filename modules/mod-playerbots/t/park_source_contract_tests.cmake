# twow-repo#541/#551 (owner 07.10.2026): rndbot park / unpark - a parked bot stands at an inn of its
# own faction and level band, its hearthstone bound server side and checked in the database, with a
# reduced AI tick. Nothing happens without the console command. Pure rules: park_policy_tests.cpp.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)

# Console commands.
require_text("${mgr}" "handlers[\"park\"] = &RandomPlayerbotMgr::HandleConsolePark;" "rndbot park")
require_text("${mgr}" "handlers[\"unpark\"] = &RandomPlayerbotMgr::HandleConsoleUnpark;" "rndbot unpark")

# Processing every pass, without a new return in UpdateAIInternal (perfmon_init_reachable).
region("${mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" pre_init)
require_text("${pre_init}" "
    ProcessParkedBots();" "parked bots processed each pass (an active call, not a comment)")

# Inn of the bot's race and level band, nearest; hearthstone bound server side, checked in the DB.
region("${mgr}" "bool RandomPlayerbotMgr::ParkBot(" "void RandomPlayerbotMgr::UnparkBot(" park_bot)
require_text("${park_bot}" "innCacheLevel[bot->getRace()][bot->GetLevel()]" "inn of race and level band")
require_text("${park_bot}" "ai::park::ChooseSpot(ParkPoint(bot), points, occupied)" "nearest spot with room (capacity)")
require_order("${park_bot}" "spot = inns[index];" "spot = cities[index];" "inns first, capital spots as overflow")
require_text("${park_bot}" "ai::park::SlotOffset(ParkPoint(spot), slot)" "own place >= 2 yd from the others")
require_text("${park_bot}" "ai->HasRealPlayerMaster() || bot->GetGroup()" "no bot of a real player and no group member")
region("${mgr}" "void RandomPlayerbotMgr::ProcessParkedBots()" "void RandomPlayerbotMgr::ParkBindCheck(" process)
require_text("${process}" "bot->SetHomebindToLocation(entry.inn, entry.area);" "server-side bind")
require_text("${process}" "FROM `character_homebind` WHERE `guid` = '%u'" "bind checked in the database")
require_text("${mgr}" "if (found && ai::park::BindMatches(stored, ParkPoint(entry.inn)))" "stored bind compared with the inn")
require_text("${mgr}" "if (ai::park::RetryBind(entry.tries))" "bind retried")
require_text("${mgr}" "ReleaseParkSpot(it->second);
    parkedBots.erase(it);" "slot released on unpark")
foreach(state travel bind bound parked bind_retry bind_failed unparked)
  require_text("${mgr}" "[Park] state=${state} bot=%u" "[Park] ${state} line")
endforeach()

# A parked bot is left alone by the random-bot manager and the quest rescue.
region("${mgr}" "bool RandomPlayerbotMgr::ProcessBot(Player* player)" "void RandomPlayerbotMgr::Revive(" process_bot)
require_order("${process_bot}" "if (player && IsParkedBot(player->GetGUIDLow()))\n        return false;" "Randomize(player);" "no randomize for a parked bot")
require_text("${mgr}" "!ai->TakeQuestRescueRequest() || IsParkedBot(guid)" "no rescue teleport for a parked bot")

# Reduced AI tick, every update still in combat.
require_order("${ai}" "if (!ai::park::AiUpdateDue(bot->IsInCombat(), nowMs, lastParkedUpdateMs))" "SlowUpdateProbe const slowUpdateProbe" "parked check before the AI update")
require_text("${ai_h}" "std::atomic<bool> parked{ false };" "parked flag set by the world thread")

# Visibility stage 2 (#541/#551, owner approval 07.10.): with AiPlayerbot.Park.HideFromBots (default
# off) a parked bot is hidden from other bots (core Player flag); unpark always clears it.
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.Park.HideFromBots\", false)" "HideFromBots default off")
require_text("${park_bot}" "bot->SetHiddenFromBots(sPlayerbotAIConfig.parkHideFromBots);" "flag set on park only by the switch")
region("${mgr}" "void RandomPlayerbotMgr::UnparkBot(" "void RandomPlayerbotMgr::ProcessParkedBots()" unpark)
require_text("${unpark}" "    bot->SetHiddenFromBots(false);" "flag cleared on every unpark")

# Deep dive L1 (#541): a bot log line below the active level is dropped before the 4 KB format and
# before BotLog's global mutex (all map threads used to serialise on it for every disabled outDebug).
file(READ "${PB_SOURCE_DIR}/BotLog.cpp" botlog)
foreach(fn "outDetail" "outDebug" "outBasic")
  region("${botlog}" "void BotLog::${fn}(const char* fmt, ...)" "\n}" fn_body)
  require_text("${fn_body}" "BOTLOG_SKIP_BELOW(" "level check first in ${fn}")
  require_order("${fn_body}" "BOTLOG_SKIP_BELOW(" "BOTLOG_IMPL(" "level check before format and lock in ${fn}")
endforeach()
require_text("${botlog}" "if (!m_file && !Log::Instance().HasLogLevelOrHigher(level))" "skip only without bot log file and below both log levels")
message(STATUS "PARK_SOURCE_CONTRACT=PASS")
