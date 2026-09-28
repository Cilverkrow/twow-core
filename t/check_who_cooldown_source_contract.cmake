if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#419 (owner approval 2026-09-28): the /who cooldown of player
# accounts is a config key instead of a fixed 30 s. The default keeps the
# old behaviour; the GM bypass stays as it was.

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${label}: ${needle}")
  endif()
endfunction()

file(READ "${TW_CORE_ROOT}/src/game/Handlers/MiscHandler.cpp" handler)
file(READ "${TW_CORE_ROOT}/src/game/World.h" world_h)
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world_cpp)
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" conf)

require_text("${world_h}" "CONFIG_UINT32_WHO_LIST_REQUEST_COOLDOWN," "config enum")
require_text("${world_cpp}" "setConfig(CONFIG_UINT32_WHO_LIST_REQUEST_COOLDOWN, \"WhoList.RequestCooldownSeconds\", 30);" "config key with default 30")
require_text("${conf}" "WhoList.RequestCooldownSeconds = 30" "documented default in mangosd.conf.dist")

require_text("${handler}" "sWorld.getConfig(CONFIG_UINT32_WHO_LIST_REQUEST_COOLDOWN)" "cooldown read from the config")
forbid_text("${handler}" "m_lastWhoRequest < 30" "fixed 30 s cooldown")
require_text("${handler}" "HasCustomFlag(CUSTOM_PLAYER_FLAG_BYPASS_WHO_COOLDOWN)" "unchanged bypass flag")
require_text("${handler}" "if (GetSecurity() == SEC_PLAYER)\n        m_lastWhoRequest = time(nullptr);" "only player accounts are limited")

message(STATUS "WHO_COOLDOWN_SOURCE_CONTRACT=PASS")
