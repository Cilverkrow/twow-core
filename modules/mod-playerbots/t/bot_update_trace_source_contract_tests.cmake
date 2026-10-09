# twow-repo#541 (spikes per region update; owner approval for measurement counters, OB-00 09.10.2026):
# [BotUpdate] measures the wall time of PlayerbotAI::UpdateAI behind AiPlayerbot.BotUpdateTrace
# (default 0). Off: UpdateAI is called exactly as before, nothing is timed. On: one line per minute
# plus [BotUpdateSlow] for the five slowest calls. Pure logic: t/bot_update_trace_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 BotUpdate: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 BotUpdate: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotScripts.cpp" scripts)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("BotUpdateTrace.h" trace_h)

# Switch default off; off = the old plain call.
require_text("${config_h}" "bool botUpdateTrace = false;" "switch member default off")
require_text("${config_cpp}" "botUpdateTrace = config.GetBoolDefault(\"AiPlayerbot.BotUpdateTrace\", false);" "config default off")
require_text("${conf_dist}" "AiPlayerbot.BotUpdateTrace = 0" "documented key")

string(FIND "${scripts}" "void OnUpdate(Player* player, uint32 diff) override" on_update_at)
if(on_update_at EQUAL -1)
  message(FATAL_ERROR "#541 BotUpdate: OnUpdate not found")
endif()
string(SUBSTRING "${scripts}" ${on_update_at} 1800 on_update)
require_order("${on_update}" "if (!sPlayerbotAIConfig.botUpdateTrace)\n                    ai->UpdateAI(diff);" "auto const start = std::chrono::steady_clock::now();" "off path first, untimed")
require_order("${on_update}" "auto const start = std::chrono::steady_clock::now();" "ai::bot_update::Global().Add(us," "timed call recorded")
# The description (bot name, map, last action) is built only for slow calls, inside the recorder.
require_order("${trace_h}" "if (us <= slowThreshold.load(std::memory_order_relaxed))" "slow.push_back({ us, describe() });" "describe only slow calls")

# Minute line in the world-thread pass, after the parked bots; no return before PerfMon Init.
require_text("${rnd_mgr}" "\"[BotUpdate] calls=%llu avg_us=%llu p50_us=%llu p90_us=%llu p99_us=%llu p999_us=%llu max_us=%llu over10ms=%llu over20ms=%llu\"" "minute line")
require_text("${rnd_mgr}" "\"[BotUpdateSlow] us=%llu %s\"" "slowest calls")
string(FIND "${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_at)
string(SUBSTRING "${rnd_mgr}" ${upd_at} 1400 upd_head)
require_order("${upd_head}" "\n    ProcessParkedBots();" "\n    ReportBotUpdate();" "minute line in the world-thread pass")

message(STATUS "bot_update_trace source contract passed")
