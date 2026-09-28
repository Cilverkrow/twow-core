function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelNode.cpp" travel_node)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_source)
file(READ "${PB_SOURCE_DIR}/StallGuardPolicy.h" policy)

# #416 (a): every UpdateAI call is timed; a slow one is logged, at most once a minute per bot.
require_text("${ai_source}" "SlowUpdateProbe const slowUpdateProbe{ this, WorldTimer::getMSTime() };" "UpdateAI timed")
require_text("${ai_source}" "ai::stall_guard::ShouldLog(lastSlowUpdateLog, now, ai::stall_guard::SlowUpdateLogSeconds)" "rate limit")
require_text("${ai_source}" "[BotSlowUpdate] bot=%u" "visible slow update")

# #416 (b): the multi-pair route search has a time budget and a per-bot cooldown.
require_text("${travel_node}" "ai::stall_guard::BudgetExceeded(routeElapsed, ai::stall_guard::RouteBudgetMs)" "route budget")
require_text("${travel_node}" "ai::stall_guard::RouteCooldowns().IsBlocked(routeKey, routeNow)" "no retry during the cooldown")
require_text("${travel_node}" "[TravelRoute] state=budget_exceeded" "visible budget stop")

# Generous limits (no config keys, owner rule for train 7.1).
require_text("${policy}" "constexpr std::uint32_t RouteBudgetMs = 1000;" "1 s route budget")
require_text("${policy}" "constexpr std::uint32_t SlowUpdateMs = 1000;" "1 s slow update threshold")
