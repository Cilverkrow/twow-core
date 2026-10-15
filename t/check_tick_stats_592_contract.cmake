if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#592 (PATH-02, phase 2 group A no. 1): PerformanceLog.TickStatsExtended appends " over20= over50=" to the
# "Tick stats" line (twow-repo#351). Off (default) the line is byte-identical: the #351 format text is kept and the
# switch only picks the extended format. Pure measurement, no clock read, no behaviour change.
# Counting and format: t/tick_stats_test.cpp.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#592: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#592: forbidden ${label}: ${needle}")
  endif()
endfunction()

read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world_cpp)
read_source("src/game/TickStats.h" stats)
read_source("src/game/PerformanceMonitor.cpp" perfmon)
read_source("src/mangosd/mangosd.conf.dist.in" dist)

# 1. Switch: declared, read with default off, documented off.
require_text("${world_h}" "CONFIG_BOOL_PERFLOG_TICK_STATS_EXTENDED," "config enum entry")
require_text("${world_cpp}" "setConfig(CONFIG_BOOL_PERFLOG_TICK_STATS_EXTENDED, \"PerformanceLog.TickStatsExtended\", false);" "default-off switch")
require_text("${dist}" "# PerformanceLog.TickStatsExtended" "config documentation")
require_text("${dist}" "\nPerformanceLog.TickStatsExtended = 0\n" "documented off")

# 2. The #351 line text is unchanged; the extended text is that line plus the two counters.
set(base "Tick stats: interval=%us ticks=%u p50=%ums p95=%ums p99=%ums max=%ums over100=%u over200=%u")
require_text("${stats}" "\"${base}\",\n            intervalSeconds, s.count, s.p50, s.p95, s.p99, s.max, s.over100, s.over200);" "#351 format, off")
require_text("${stats}" "\"${base} over20=%u over50=%u\",\n        intervalSeconds, s.count, s.p50, s.p95, s.p99, s.max, s.over100, s.over200, s.over20, s.over50);" "extended format, on")
require_text("${stats}" "    if (!extended)\n        return std::snprintf(" "off picks the #351 format")

# 3. Strictly above the edges, like over100 / over200.
require_text("${stats}" "constexpr uint32_t TICK_STATS_OVER20_MS = 20;" "20 ms edge")
require_text("${stats}" "constexpr uint32_t TICK_STATS_OVER50_MS = 50;" "50 ms edge")
require_text("${stats}" "        if (v > TICK_STATS_OVER20_MS)\n            ++s.over20;" "over20 strictly above")
require_text("${stats}" "        if (v > TICK_STATS_OVER50_MS)\n            ++s.over50;" "over50 strictly above")

# 4. The monitor writes exactly one line through the shared formatter, once per interval; no own format text.
require_text("${perfmon}" "FormatTickStatsLine(Line, sizeof(Line), TickStatsElapsedMs / 1000, Summary, sWorld.getConfig(CONFIG_BOOL_PERFLOG_TICK_STATS_EXTENDED));" "formatter with the switch")
require_text("${perfmon}" "sLog.out(LOG_PERFORMANCE, \"%s\", Line);" "line written as is")
forbid_text("${perfmon}" "\"Tick stats: interval=" "a second copy of the format text in PerformanceMonitor")
string(FIND "${perfmon}" "if (TickStatsElapsedMs < sWorld.getConfig(CONFIG_UINT32_PERFLOG_TICK_STATS_INTERVAL) * 1000)\n\t\treturn;" interval_at)
string(FIND "${perfmon}" "FormatTickStatsLine(" format_at)
if (interval_at EQUAL -1 OR format_at EQUAL -1 OR NOT interval_at LESS format_at)
  message(FATAL_ERROR "#592: the line must stay behind the once-per-interval return")
endif()
foreach (clock "steady_clock" "getMSTime" "NowUs")
  forbid_text("${stats}" "${clock}" "clock read in TickStats.h")
endforeach()

message(STATUS "TICK_STATS_592_CONTRACT=PASS")
