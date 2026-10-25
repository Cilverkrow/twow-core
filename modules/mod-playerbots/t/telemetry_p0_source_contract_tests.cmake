# twow-repo#640 (TELE-01) telemetry P0, OB-00 25.10.2026: formats and sinks, no behaviour change, no switch.
#  1. BotLog: the date goes in as " ts=...Z" at the END of a [Tag] line; the "[HH:MM:SS]" prefix stays (parsers).
#  2. CSVs bot_events.csv and deaths.csv are appended, not cleared; the header only for a new or empty file; real UTC timestamps.
#  3. The lazy-roll summaries carry window_s= and a minute driver rolls them every minute (zeros included), enabled traces only.
# Paths only through PB_SOURCE_DIR (never CMAKE_SOURCE_DIR: twow-repo builds the core under /src/core).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source relative out)
  file(READ "${PB_SOURCE_DIR}/${relative}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle expected description)
  string(REPLACE "${needle}" "<HIT>" replaced "${text}")
  string(REGEX MATCHALL "<HIT>" hits "${replaced}")
  list(LENGTH hits found)
  if(NOT found EQUAL ${expected})
    message(FATAL_ERROR "Expected ${expected} x ${description}, found ${found}: ${needle}")
  endif()
endfunction()

read_source("BotLog.cpp" botlog)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("TravelMgr.cpp" travel)
read_source("strategy/actions/MovementActions.cpp" movement)
read_source("RandomPlayerbotMgr.cpp" rpm)
read_source("LazyRollReport.h" report_h)
read_source("PathFailPolicy.h" path_fail)
read_source("PosStuckPolicy.h" pos_stuck)

# ---- 1. BotLog: ts= at the end of a tag line, the prefix is untouched ----------------------------------------------
require_text("${botlog}" "std::strftime(buf, sizeof(buf), \" ts=%Y-%m-%dT%H:%M:%SZ\", &utc);" "UTC stamp format as a trailing field")
require_text("${botlog}" "gmtime_r(&now, &utc);" "thread-safe UTC conversion")
count_text("${botlog}" "if (_msg[0] == '[')" 2 "stamp only for lines that start with a [Tag] (POSIX and Windows macro)")
count_text("${botlog}" "fprintf(m_file, \"[%s] %s%s%s\\n\", _ts, prefix, _msg, _utc);" 2 "prefix [HH:MM:SS] first, the stamp last (both macros)")
reject_text("${botlog}" "fprintf(m_file, \"[%s] %s%s\\n\"" "the old line format without the stamp")
count_text("${botlog}" "std::strftime(_ts, sizeof(_ts), \"%H:%M:%S\", &_lt);" 2 "the [HH:MM:SS] prefix stays (both macros)")

# ---- 2. CSVs: real UTC, appended, header only for a new or empty file -------------------------------------------------
require_text("${config_h}" "std::string GetTimestampUtcStr();" "UTC timestamp declaration")
require_text("${config_cpp}" "std::string PlayerbotAIConfig::GetTimestampUtcStr()" "UTC timestamp definition")
require_text("${config_cpp}" "gmtime_r(&t, &utc);" "UTC timestamp uses gmtime")
require_text("${config_h}" "bool openLog(std::string fileName, char const* mode = \"a\", bool haslog = false, char const* header = nullptr);" "openLog with an optional header")
require_text("${config_cpp}" "if (header && mode && mode[0] == 'a' && fseek(file, 0, SEEK_END) == 0 && ftell(file) == 0)" "header only for an appended file that is empty")
set(all_sources "")
file(GLOB_RECURSE sources "${PB_SOURCE_DIR}/*.cpp")
foreach(source ${sources})
  file(READ "${source}" text)
  string(REPLACE "\r" "" text "${text}")
  string(APPEND all_sources "${text}")
endforeach()
reject_text("${all_sources}" "GetTimestampStr() << \"+00,\"" "a local-time stamp with a fixed +00")
string(REGEX MATCHALL "GetTimestampUtcStr\\(\\) << \"\\+00,\"" utc_hits "${all_sources}")
list(LENGTH utc_hits utc_count)
if(utc_count LESS 13)
  message(FATAL_ERROR "Expected at least 13 CSV lines with a real UTC stamp, found ${utc_count}")
endif()
require_text("${travel}" "sPlayerbotAIConfig.openLog(\"bot_events.csv\", \"a\", false, \"timestamp,bot,event,wkt,race,class,level,info1,info2\");" "bot_events.csv appended with a header")
require_text("${travel}" "sPlayerbotAIConfig.openLog(\"deaths.csv\", \"a\", false, \"timestamp,bot,race,class,level,wkt,target,target_level,target_hp_pct,adds_list,adds,travel\");" "deaths.csv appended with a header")
reject_text("${travel}" "openLog(\"bot_events.csv\", \"w\")" "bot_events.csv cleared at start")
reject_text("${travel}" "openLog(\"deaths.csv\", \"w\")" "deaths.csv cleared at start")

# ---- 3. Lazy-roll summaries: window and minute driver ----------------------------------------------------------------
require_text("${path_fail}" "inline std::uint32_t WindowMinutes(std::uint64_t seen, std::uint64_t now)" "window helper")
count_text("${path_fail}" "windowMinutes.store(WindowMinutes(seen, now), std::memory_order_relaxed);" 2 "window stored by both path_fail rolls")
require_text("${pos_stuck}" "windowMinutes.store(" "window stored by the PosStuck roll")
require_text("${movement}" "[RouteFailDetail] summary failed=%u logged=%u honest=%u window_s=%u" "RouteFailDetail window")
require_text("${movement}" "[MoveTo] summary partial_noop=%u logged=%u window_s=%u" "partial_noop window")
require_text("${movement}" "transport_waits=%u logged=%u window_s=%u" "TaxiUse window")
require_text("${travel}" "[PosStuck] summary detect=%u unstuck=%u teleport=%u logged=%u window_s=%u" "PosStuck window")

require_text("${report_h}" "void ReportMinute();" "minute driver")
require_text("${report_h}" "void ReportPosStuckSummary();" "PosStuck part of the driver")
string(FIND "${movement}" "void ai::lazy_roll::ReportMinute()" driver_at)
if(driver_at EQUAL -1)
  message(FATAL_ERROR "Missing definition of ai::lazy_roll::ReportMinute")
endif()
string(SUBSTRING "${movement}" ${driver_at} -1 driver)
foreach(pair
    "sPlayerbotAIConfig.pathRouteFailDetail|RollRouteFailSummary();"
    "sPlayerbotAIConfig.pathPartialNoop|RollPartialNoopSummary();"
    "sPlayerbotAIConfig.pathTaxiTrace|LogTaxiSummary(path_fail::SharedTaxiCounters());"
    "sPlayerbotAIConfig.pathTransportEdgeTrace|RollTransportEdgeSummary();"
    "sPlayerbotAIConfig.pathTransportDeckProbe|RollTransportBoardSummary();"
    "sPlayerbotAIConfig.pathPosStuck|ai::lazy_roll::ReportPosStuckSummary();")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 flag)
  list(GET parts 1 call)
  require_text("${driver}" "if (${flag})\n        ${call}" "driver rolls only an enabled trace (${flag})")
endforeach()

# the roster manager's minute report calls the driver (once a minute), after the [BotInbox] line
require_text("${rpm}" "    ai::lazy_roll::ReportMinute();" "minute driver call in the minute report")
string(FIND "${rpm}" "void ReportBotInbox()" inbox_at)
string(FIND "${rpm}" "ai::lazy_roll::ReportMinute();" call_at)
if(inbox_at EQUAL -1 OR call_at EQUAL -1 OR call_at LESS inbox_at)
  message(FATAL_ERROR "ReportMinute must be called inside the once-a-minute ReportBotInbox")
endif()
# the event paths and the driver print the same summary line: one definition per trace
count_text("${movement}" "[RouteFailDetail] summary" 1 "one RouteFailDetail summary format")
count_text("${movement}" "[MoveTo] summary partial_noop" 1 "one partial_noop summary format")
count_text("${travel}" "[PosStuck] summary" 1 "one PosStuck summary format")

message(STATUS "telemetry_p0_source_contract: all pins hold")
