# twow-repo#541 audit A47 (owner 11.10.2026): AiPlayerbot.BotSave.IntervalSeconds (default 0 = core autosave). A bot
# saves itself on its own map thread (UpdateAI), holding back the core timer, at most MaxPerTick per thread and tick.

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
    message(FATAL_ERROR "#541 A47 bot save: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A47 bot save: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#541 A47 bot save: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#541 A47 bot save: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("BotSavePolicy.h" policy)

require_text("${config_h}" "uint32 botSaveIntervalSeconds = 0;" "switch default off")
require_text("${config_cpp}" "botSaveIntervalSeconds = uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.BotSave.IntervalSeconds\", 0)));" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.BotSave.IntervalSeconds = 0" "documented")
require_text("${conf_dist}" "AiPlayerbot.BotSave.MaxPerTick = 1" "budget documented")

# Own thread: called from UpdateAI right after the inbox drain, before the park early return.
between("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" "\n}\n" update_ai)
require_order("${update_ai}" "HandleQueuedBotPackets();" "if (sPlayerbotAIConfig.botSaveIntervalSeconds)\n        UpdateBotSave();" "after the inbox drain")
require_order("${update_ai}" "UpdateBotSave();" "if (parked)" "before the park early return (parked bots save too)")

between("${ai_cpp}" "void PlayerbotAI::UpdateBotSave()" "void PlayerbotAI::UpdateAI(" save_fn)
require_order("${save_fn}" "bot->SetSaveTimer(ai::bot_save::HoldCoreTimerMs(interval));" "if (!ai::bot_save::Due(now, lastBotSave, interval))" "core timer held on every call")
require_order("${save_fn}" "ai::bot_save::TryTake(ai::bot_save::BudgetWindow(WorldTimer::getMSTime()), sPlayerbotAIConfig.botSaveMaxPerTick, ai::bot_save::ThreadLocal())" "bot->SaveToDB();" "budget before the save")
# Live v46.1 (11.10.): WorldTimer::tick() is never called in this core, tickTime() stays 0 - a budget keyed on it never
# resets. The window comes from the real clock.
string(FIND "${save_fn}" "tickTime()" frozen_tick)
if(NOT frozen_tick EQUAL -1)
  message(FATAL_ERROR "#541 A47: the save budget must not be keyed on WorldTimer::tickTime() (never advanced)")
endif()
require_text("${policy}" "return nowMs / BudgetWindowMs;" "budget window from the clock")
require_order("${save_fn}" "bot->SaveToDB();" "lastBotSave = now;" "schedule moves on only after a save")
require_text("${save_fn}" "!bot->IsInWorld() || bot->IsBeingTeleported()" "no save while out of world / teleporting")
require_text("${policy}" "return (intervalSeconds + 60) * 1000;" "core timer held beyond the own interval")

# Minute line only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.botSaveIntervalSeconds)" "[BotSave] interval_s=%u max_per_tick=%u saves=%llu deferred=%llu avg_us=%llu total_ms=%llu" "minute line behind the switch")

message(STATUS "bot_save source contract passed")
