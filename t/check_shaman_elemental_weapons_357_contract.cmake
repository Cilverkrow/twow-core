if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

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

# twow-repo#357 route A: owner decision O-10 values for the Elemental Weapons ranks, exactly these.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927150000_world.sql" m)
foreach (required
    "SET effectBasePoints1 = 16 WHERE entry = 52970;"
    "SET effectBasePoints1 = 32 WHERE entry = 52971;"
    "SET effectBasePoints1 = 49 WHERE entry = 52972;"
    "SET effectBasePoints1 = 15 WHERE entry = 58248;"
    "SET effectBasePoints1 = 32 WHERE entry = 58249;"
    "SET effectBasePoints1 = 49 WHERE entry = 58250;"
    "SET effectBasePoints1 = 1 WHERE entry IN (52967, 52968, 52969);"
    "SET effectBasePoints3 = 9 WHERE entry = 16266;"
    "SET effectBasePoints3 = 19 WHERE entry = 29079;"
    "SET effectBasePoints3 = 29 WHERE entry = 29080;"
    "SET effectBasePoints1 = 14 WHERE entry = 58128;"
    "SET effectBasePoints1 = 19 WHERE entry = 58129;"
    "SET effectBasePoints1 = 24 WHERE entry = 58130;")
  require_text("${m}" "${required}" "#357 route A migration")
endforeach()
string(REGEX MATCHALL "UPDATE spell_template SET" updates "${m}")
list(LENGTH updates update_count)
if (NOT update_count EQUAL 13)
  message(FATAL_ERROR "#357 route A migration: expected 13 spell_template updates, got ${update_count}")
endif()
foreach (forbidden "DELETE " "REPLACE " "INSERT " "stackAmount" "castingTimeIndex")
  forbid_text("${m}" "${forbidden}" "#357 route A scope")
endforeach()

# Earthen Bulwark cap: 40 % of max health at 3/3 = build % x 4/3.
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_shaman.cpp" script)
require_text("${script}" "uint32(uint64(CalculatePct(owner->GetMaxHealth(), GetEarthenBulwarkBuildPct(aura))) * 4 / 3)" "#357 Earthen Bulwark cap")
message(STATUS "SHAMAN_ELEMENTAL_WEAPONS_357_CONTRACT=PASS")
