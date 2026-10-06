if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#518 probe (test realm only): display-only bot surnames in the name
# query. The character name stays the key; the probe is off unless
# Debug.NameQueryDisplayNames lists names, and a display may only extend the name.
file(READ "${TW_CORE_ROOT}/src/game/Handlers/QueryHandler.cpp" query)
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world)
file(READ "${TW_CORE_ROOT}/src/game/ObjectMgr.cpp" objmgr)
file(READ "${TW_CORE_ROOT}/src/mangosd/mangosd.conf.dist.in" dist)

# All three name query paths send the display name.
string(REGEX MATCHALL "sWorld\\.NameQueryDisplayName\\(" sends "${query}")
list(LENGTH sends send_count)
if (NOT send_count EQUAL 3)
  message(FATAL_ERROR "Name query probe: expected 3 display-name sends in QueryHandler.cpp, found ${send_count}")
endif()

# Incoming names resolve centrally, before the name is checked.
string(FIND "${objmgr}" "sWorld.ResolveNameQueryDisplayName(name);" resolve_at)
string(FIND "${objmgr}" "if (!Utf8toWStr(name, wstr_buf))" utf_at)
if (resolve_at EQUAL -1 OR NOT resolve_at LESS utf_at)
  message(FATAL_ERROR "Name query probe: normalizePlayerName must resolve display names first")
endif()

foreach (required
    "sConfig.GetStringDefault(\"Debug.NameQueryDisplayNames\", \"\")"
    "display.size() > 32"
    "display.compare(0, name.size() + 1, name + \" \") != 0"
    "if (m_nameQueryDisplay.empty())")
  string(FIND "${world}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Name query probe: missing ${required}")
  endif()
endforeach()

# Off by default.
string(FIND "${dist}" "Debug.NameQueryDisplayNames = \"\"" dist_at)
if (dist_at EQUAL -1)
  message(FATAL_ERROR "Name query probe: Debug.NameQueryDisplayNames must default to empty in mangosd.conf.dist.in")
endif()

message(STATUS "NAME_QUERY_DISPLAY_PROBE_CONTRACT=PASS")
