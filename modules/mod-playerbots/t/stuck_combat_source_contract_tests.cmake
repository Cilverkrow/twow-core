# Hotfix 8.12: a roster bot stuck in combat without progress is stopped and may be rescued.
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
foreach (needle
    "ai::stuck_combat::Step const step = stuckCombat.Observe(bot->IsInCombat(), questProgress.IdleSeconds(now), now);"
    "bot->CombatStop(true);"
    "bot->getHostileRefManager().deleteReferences();"
    "(!bot->IsInCombat() || rescueInCombat)"
    "[StuckCombat] state=%s bot=%u")
  string(FIND "${ai_cpp}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "stuck combat: missing ${needle}")
  endif()
endforeach()
string(FIND "${ai_cpp}" "!HasRealPlayerMaster() && bot->IsAlive() && !bot->IsInCombat() &&" old_gate)
if (NOT old_gate EQUAL -1)
  message(FATAL_ERROR "stuck combat: the rescue gate must allow a stuck combat")
endif()
message(STATUS "STUCK_COMBAT_CONTRACT=PASS")
