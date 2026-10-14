# twow-repo#591 (PERF-AB-01, live 14.10.2026): AiPlayerbot.Perf.ActionBudgetStartExempt / ActionBudgetLimit (default 0).
# Bots without a travel target choose one without budget; a configurable per-window limit.

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
    message(FATAL_ERROR "#591 budget start: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#591 budget start: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#591 budget start: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#591 budget start: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/Engine.cpp" engine)
read_source("ActionBudgetPolicy.h" policy)

require_text("${config_h}" "bool perfActionBudgetStartExempt = false;" "exempt default off")
require_text("${config_h}" "uint32 perfActionBudgetLimit = 0;" "limit default = mode")
require_text("${conf_dist}" "AiPlayerbot.Perf.ActionBudgetStartExempt = 0" "exempt documented")
require_text("${conf_dist}" "AiPlayerbot.Perf.ActionBudgetLimit = 0" "limit documented")

# Exempt only target choices of bots without an active target, only with the switch, before the budget.
require_order("${engine}" "sPlayerbotAIConfig.perfActionBudgetStartExempt &&" "if (action && sPlayerbotAIConfig.perfActionBudget && !budgetStartExempt &&" "exemption decided before the budget")
require_text("${engine}" "ai::action_budget::StartExempt(true, action->getName(), travelTarget && travelTarget->IsActive())" "by the bot's active travel target")
require_text("${engine}" "ai::action_budget::ThreadLocal(), sPlayerbotAIConfig.perfActionBudgetLimit))" "configured limit passed")
require_text("${policy}" "return switchOn && !hasActiveTravelTarget && IsTravelTargetChoice(action);" "exempt rule")
require_text("${policy}" "return limitOverride ? limitOverride : LimitFor(mode);" "0 = mode limit")
require_text("${policy}" "return mode == 1 ? 3u : mode == 2 ? 1u : 0u;" "mode limits unchanged")

message(STATUS "budget_start source contract passed")
