if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#593 (NAMES-01, owner 2026-10-15): CharacterNames.SkipProfanityReserved skips only the
# NamesProfanity/NamesReserved DBC patterns (ValidateName). Length, allowed letters and the
# reserved_name table stay. Default off in the code and in the distributed config.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#593: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#593: forbidden ${label}: ${needle}")
  endif()
endfunction()

read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world_cpp)
read_source("src/game/Database/DBCStores.cpp" dbc)
read_source("src/game/ObjectMgr.cpp" objectmgr)
read_source("src/mangosd/mangosd.conf.dist.in" conf)

# 1. Switch: declared, read from the config, default off.
require_text("${world_h}" "CONFIG_BOOL_NAMES_SKIP_PROFANITY_RESERVED," "config enum entry")
require_text("${world_cpp}"
  "setConfig(CONFIG_BOOL_NAMES_SKIP_PROFANITY_RESERVED, \"CharacterNames.SkipProfanityReserved\", false);"
  "config key with default false")

# 2. One early return at the top of ValidateName, before either pattern list is used.
string(FIND "${dbc}" "uint8 ValidateName(std::wstring const& name)\n{\n" fn_at)
if (fn_at EQUAL -1)
  message(FATAL_ERROR "#593: ValidateName not found")
endif()
string(SUBSTRING "${dbc}" ${fn_at} -1 fn)
string(FIND "${fn}" "if (sWorld.getConfig(CONFIG_BOOL_NAMES_SKIP_PROFANITY_RESERVED))\n        return CHAR_NAME_SUCCESS;" skip_at)
string(FIND "${fn}" "for (std::wregex const& regex : NamesProfaneValidators)" profane_at)
string(FIND "${fn}" "for (std::wregex const& regex : NamesReservedValidators)" reserved_at)
if (skip_at EQUAL -1 OR profane_at EQUAL -1 OR reserved_at EQUAL -1)
  message(FATAL_ERROR "#593: ValidateName lacks the switch or one of the pattern loops")
endif()
if (NOT skip_at LESS profane_at OR NOT skip_at LESS reserved_at)
  message(FATAL_ERROR "#593: the switch must come before both pattern loops in ValidateName")
endif()
require_text("${dbc}" "#include \"World.h\"" "World.h include for sWorld")

# 3. The switch is used in exactly that one place: no other name check is skipped.
file(GLOB_RECURSE sources "${TW_CORE_ROOT}/src/game/*.cpp" "${TW_CORE_ROOT}/src/game/*.h")
set(uses 0)
foreach (source ${sources})
  if (source MATCHES "/World\\.(h|cpp)$")
    continue()
  endif()
  file(READ "${source}" text)
  string(FIND "${text}" "CONFIG_BOOL_NAMES_SKIP_PROFANITY_RESERVED" at)
  if (NOT at EQUAL -1)
    math(EXPR uses "${uses} + 1")
    if (NOT source MATCHES "Database/DBCStores\\.cpp$")
      message(FATAL_ERROR "#593: the switch is used outside ValidateName (${source})")
    endif()
  endif()
endforeach()
if (NOT uses EQUAL 1)
  message(FATAL_ERROR "#593: expected the switch in exactly one source file, found ${uses}")
endif()

# 4. Length, allowed letters and the reserved_name table are untouched.
require_text("${objectmgr}" "return CHAR_NAME_TOO_LONG;" "name length check")
require_text("${objectmgr}" "return CHAR_NAME_TOO_SHORT;" "min length check")
require_text("${objectmgr}" "return CHAR_NAME_MIXED_LANGUAGES;" "allowed letters check")
require_text("${objectmgr}" "return m_ReservedNames.find(wstr) != m_ReservedNames.end();" "reserved_name table check")

# 5. Documented in the distributed config, default off.
require_text("${conf}" "# CharacterNames.SkipProfanityReserved" "config documentation")
require_text("${conf}" "\nCharacterNames.SkipProfanityReserved = 0\n" "config default 0")

message(STATUS "NAMES_SKIP_PROFANITY_593_CONTRACT=PASS")
