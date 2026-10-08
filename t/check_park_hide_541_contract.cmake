if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#541/#551 (owner approval 07.10.2026, visibility stage 2): a parked bot (Player flag set by
# the bot module's rndbot park with AiPlayerbot.Park.HideFromBots) is not visible to other bots and
# sees only real players and units fighting it; creatures and real players see it as always.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#541/#551: missing ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541/#551: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("src/game/Objects/Player.h" player_h)
read_source("src/game/Objects/Player.cpp" player_cpp)
read_source("src/game/Objects/Unit.cpp" unit_cpp)

# Off unless the module sets it; a real client is a session with a socket.
require_text("${player_h}" "bool m_hiddenFromBots = false;" "flag off by default")
require_text("${player_cpp}" "return GetSession() && GetSession()->GetSocket();" "real client = session with a socket")
# A change rebuilds the visibility in the next map update (both directions), never stuck.
require_text("${player_cpp}" "AddUnitState(UNIT_STAT_PENDING_VIS_UPDATE);" "visibility rebuilt after a change")

# The rule only between players; creatures (aggro) and real players are untouched.
string(FIND "${unit_cpp}" "bool Unit::IsVisibleForOrDetect(" start)
string(SUBSTRING "${unit_cpp}" ${start} 6000 visible)
require_order("${visible}" "Player const* pDetectorPlayer = pDetector->ToPlayer();" "if (self && self->IsHiddenFromBots() && !pDetectorPlayer->HasClientSocket())" "rule after the detector is known")
require_text("${visible}" "    if (pDetectorPlayer)\n    {\n        Player const* self = ToPlayer();" "only for player detectors (creatures keep seeing a parked bot)")
require_text("${visible}" "if (self && self->IsHiddenFromBots() && !pDetectorPlayer->HasClientSocket())\n            return false;" "parked bot hidden from bots only")
require_text("${visible}" "else if (GetVictim() != pDetectorUnit && pDetectorUnit->GetVictim() != this)\n                return false;" "a parked bot still sees the units fighting it")

message(STATUS "PARK_HIDE_541_CONTRACT=PASS")
