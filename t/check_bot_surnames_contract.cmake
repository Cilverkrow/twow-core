if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 9.3 (twow-repo#518): display-only bot surnames. The character name stays
# the key; the surnames come from a read-only file read once at startup (no DB
# table), the name query sends "Name Surname", and only that full display name
# resolves back on incoming names - never the last word alone (owner 2026-10-07).
file(READ "${TW_CORE_ROOT}/src/game/Handlers/QueryHandler.cpp" query)
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world)
file(READ "${TW_CORE_ROOT}/src/game/World.h" world_h)
file(READ "${TW_CORE_ROOT}/src/game/ObjectMgr.cpp" objmgr)
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" dist)

# All three name query paths send the display name.
string(REGEX MATCHALL "sWorld\\.NameQueryDisplayName\\(" sends "${query}")
list(LENGTH sends send_count)
if (NOT send_count EQUAL 3)
  message(FATAL_ERROR "Bot surnames: expected 3 display-name sends in QueryHandler.cpp, found ${send_count}")
endif()

# Incoming names resolve centrally, before the name is checked.
string(FIND "${objmgr}" "sWorld.ResolveNameQueryDisplayName(name);" resolve_at)
string(FIND "${objmgr}" "if (!Utf8toWStr(name, wstr_buf))" utf_at)
if (resolve_at EQUAL -1 OR NOT resolve_at LESS utf_at)
  message(FATAL_ERROR "Bot surnames: normalizePlayerName must resolve display names first")
endif()

foreach (required
    "sConfig.GetStringDefault(\"Funserver.BotSurnames.File\", \"\")"
    "std::ifstream file(path);"
    "display.size() > 32"
    "m_nameQueryByDisplay.find(AsciiLower(name))")
  string(FIND "${world}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Bot surnames: missing ${required}")
  endif()
endforeach()

# No last-word mapping and no config-string source (probe leftovers).
foreach (forbidden "m_nameQueryByLast" "m_nameQueryByFirst" "Debug.NameQueryDisplayNames")
  string(FIND "${world}${world_h}${dist}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Bot surnames: ${forbidden} must be gone")
  endif()
endforeach()

# Off by default.
string(FIND "${dist}" "Funserver.BotSurnames.File = \"\"" dist_at)
if (dist_at EQUAL -1)
  message(FATAL_ERROR "Bot surnames: Funserver.BotSurnames.File must default to empty in mangosd.conf.dist.in")
endif()

message(STATUS "BOT_SURNAMES_CONTRACT=PASS")
