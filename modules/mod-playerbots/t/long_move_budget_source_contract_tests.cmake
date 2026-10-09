# twow-repo#541 (deep dive, budget (b), OB-00 go 10.10.2026): [BotUpdateSlow] (OB-30 chain19b) showed the
# slowest bot updates are mostly "move to travel target" - the route search of a long move without a cached
# route. Behind AiPlayerbot.LongMoveBudget.MaxPerSlot (default 0 = off) the #540 per-100-ms cap applies after
# the startup window too, with its own instance and limit; deferred moves return without retry or cooldown.
# Budget arithmetic: t/startup_travel_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 long-move budget: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 long-move budget: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("strategy/actions/MoveToTravelTargetAction.cpp" move)
read_source("StartupTravelPolicy.h" policy)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

require_text("${config_h}" "uint32 longMoveBudgetMaxPerSlot = 0;" "member default off")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.LongMoveBudget.MaxPerSlot\", 0)" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.LongMoveBudget.MaxPerSlot = 0" "documented key")

# Own instance, separate from the startup budget.
require_text("${policy}" "inline LongMoveBudget& RuntimeBudget() { static LongMoveBudget budget; return budget; }" "own instance")

# After the startup block, gated, only long uncached moves, deferred before MoveTo.
require_order("${move}" "startup_travel::SharedBudget().TryTake(nowMs, sPlayerbotAIConfig.startupTravelMaxLongMovesPerSlot)" "else if (sPlayerbotAIConfig.longMoveBudgetMaxPerSlot)" "startup window first")
require_order("${move}" "else if (sPlayerbotAIConfig.longMoveBudgetMaxPerSlot)" "if (startup_travel::IsLongMove(sameMap, sameMap ? botLocation.distance(location) : 0.0f, cached))\n        {\n            uint64 const nowMs" "only long moves")
require_order("${move}" "!startup_travel::RuntimeBudget().TryTake(nowMs, sPlayerbotAIConfig.longMoveBudgetMaxPerSlot)" "startup_travel::RuntimeCounters().deferred.fetch_add(1, std::memory_order_relaxed);\n                return true;" "deferred without retry")
require_order("${move}" "startup_travel::RuntimeCounters().deferred.fetch_add(1, std::memory_order_relaxed);" "bool canMove = MoveTo(mapId, x, y, z, false, false);" "budget before MoveTo")

# Minute line.
require_text("${move}" "\"[LongMoveBudget] long_moves=%u deferred=%u max_per_slot=%u slot_ms=%u\"" "minute line")
require_order("${move}" "LogStartupTravel();" "LogLongMoveBudget();" "logged from isUseful")

message(STATUS "long_move_budget source contract passed")
