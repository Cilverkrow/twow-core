# twow-repo#575 F9-2 (CAUSE.md 3b): Player::ResurrectPlayer clears a graveyard repop still pending from the release, so a
# revived player is never moved alive to the graveyard by Player::Update; [Repop] stale_cleared once per minute as proof.
if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# require_order(text first second label): both present, first before second
function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "Order broken (${label}): ${first} before ${second}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/src/game/Objects/RepopPolicy.h" policy)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
file(READ "${TW_CORE_ROOT}/src/game/Maps/MapManager.h" mgr_h)
file(READ "${TW_CORE_ROOT}/src/game/Maps/MapManager.cpp" mgr_cpp)

require_text("${policy}" "    inline bool ClearStale(bool& pending)\n    {\n        bool const stale = pending;\n        pending = false;\n        return stale;\n    }" "the clear rule")
require_text("${player}" "#include \"RepopPolicy.h\"" "the policy include")

# The clear sits in ResurrectPlayer after the death state is ALIVE (a hardcore character that is not revived keeps its repop)
# and before the function's next statement.
string(FIND "${player}" "void Player::ResurrectPlayer(float restore_percent, bool applySickness, bool forceHc)" res)
if (res EQUAL -1)
  message(FATAL_ERROR "Missing Player::ResurrectPlayer")
endif()
string(SUBSTRING "${player}" ${res} 1500 resurrect)
require_order("${resurrect}" "    if (IsHardcore() && !forceHc)\n        return;" "    SetDeathState(ALIVE);" "the hardcore return before the revive")
require_text("${resurrect}" "    SetDeathState(ALIVE);\n\n    // twow-repo#575 F9-2 (RepopPolicy.h): a graveyard repop still pending from the release is stale now - it must\n    // never move the living player to the graveyard (bots: the death-loop escape landed back in the graveyard camp).\n    if (repop::ClearStale(m_repopAtGraveyardPending))\n        sMapMgr.CountStaleRepopCleared();" "the clear right after the revive")

# The pending repop still runs only through Player::Update, and only RepopAtGraveyard / the scheduler touch the flag besides it.
require_text("${player}" "        if (m_repopAtGraveyardPending && !HasPendingMovementChange())\n        {\n            RepopAtGraveyard();" "the deferred repop in Player::Update")
string(REGEX MATCHALL "m_repopAtGraveyardPending" uses "${player}")
list(LENGTH uses n)
if (NOT n EQUAL 5)
  message(FATAL_ERROR "m_repopAtGraveyardPending used ${n} times in Player.cpp (expected 5: init, Update, schedule, repop, revive) - recheck the stale-repop rule")
endif()

require_text("${mgr_h}" "        void CountStaleRepopCleared() { m_staleRepopCleared.fetch_add(1, std::memory_order_relaxed); }" "the counter")
require_text("${mgr_cpp}" "    ReportStaleRepops();         // twow-repo#575 F9-2: [Repop] once per minute" "the unconditional minute report")
require_text("${mgr_cpp}" "    sLog.outBasic(\"[Repop] stale_cleared=%llu\", (unsigned long long)cleared);" "the [Repop] minute line")
