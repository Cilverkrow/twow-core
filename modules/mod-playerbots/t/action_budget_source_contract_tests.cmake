# twow-repo#541 (owner 11.10.2026): AiPlayerbot.Perf.ActionBudget (default 0; 1 max 3 / 2 max 1 / 3 spread). An
# expensive action without budget goes back into the queue unchanged and the bot's tick ends; MaxDeferrals in a
# row force it. Off: the engine walk as before.

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
    message(FATAL_ERROR "#541 action budget: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 action budget: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#541 action budget: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#541 action budget: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/Engine.cpp" engine)
read_source("ActionBudgetPolicy.h" policy)
read_source("RandomPlayerbotMgr.cpp" mgr)

require_text("${config_h}" "uint32 perfActionBudget = 0;" "switch default off")
require_text("${config_cpp}" "perfActionBudget = std::min<uint32>(3, uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.Perf.ActionBudget\", 0))));" "config 0..3, default 0")
require_text("${conf_dist}" "AiPlayerbot.Perf.ActionBudget = 0" "documented")
require_text("${conf_dist}" "AiPlayerbot.Perf.ActionBudgetActions =" "action list documented")

# Engine: deferral = push back + end of this bot's tick. twow-repo#591 (CLI-PATH, 15.10.2026): checked only after
# isUseful and isPossible, right before the execution - a useless action never uses a slot.
between("${engine}" "bool Engine::DoNextAction(" "\nvoid Engine::" walk)
require_order("${walk}" "bool isPossible = action->isPossible();" "if (action && sPlayerbotAIConfig.perfActionBudget &&" "budget after isPossible")
require_order("${walk}" "if (action && sPlayerbotAIConfig.perfActionBudget &&" "actionExecuted = ListenAndExecute(action, event);" "budget before the execution")
between("${walk}" "if (action && sPlayerbotAIConfig.perfActionBudget &&" "actionExecuted = ListenAndExecute(action, event);" budget_block)
require_order("${budget_block}" "PushAgain(actionNode, relevance, event, true);" "break;" "deferred action re-queued, tick ends")
string(FIND "${budget_block}" "delete actionNode" deletes)
if(NOT deletes EQUAL -1)
  message(FATAL_ERROR "#541 action budget: a deferred node is re-queued, never deleted")
endif()
require_text("${budget_block}" "actionBudgetDeferrals = 0;" "deferral streak reset when the action runs")
# Live v46.1 (11.10., BotSave): WorldTimer::tick() is never called in this core, tickTime() stays 0 - a budget keyed on
# it never refills. The window comes from the real clock.
require_text("${budget_block}" "uint32 const window = ai::action_budget::Window(WorldTimer::getMSTime());" "budget window from the clock")
string(FIND "${budget_block}" "tickTime()" frozen_tick)
if(NOT frozen_tick EQUAL -1)
  message(FATAL_ERROR "#541 action budget: must not be keyed on WorldTimer::tickTime() (never advanced)")
endif()

# Policy: no starvation; limits as documented.
require_text("${policy}" "if (!mode || deferralsInARow >= MaxDeferrals)\n            return true;" "forced after MaxDeferrals")
require_text("${policy}" "return mode == 1 ? 3u : mode == 2 ? 1u : 0u;" "limits 3 / 1")
require_text("${policy}" "thread_local ThreadBudget budget;" "per map thread")

# Minute line only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.perfActionBudget)" "[ActionBudget] mode=%u executed=%llu deferred=%llu forced=%llu max_deferrals=%u" "minute line behind the switch")

message(STATUS "action_budget source contract passed")
