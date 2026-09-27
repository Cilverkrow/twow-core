if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# twow-repo#379: only the dwarf shaman may bypass the DBC race mask, and only for the three
# Horde shaman racial abilities (four spell ids).
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
string(FIND "${player}" "static bool IsDwarfShamanHordeRacial(" at)
if (at EQUAL -1)
  message(FATAL_ERROR "Missing IsDwarfShamanHordeRacial")
endif()
string(SUBSTRING "${player}" ${at} 900 helper)
require_text("${helper}" "if (race != RACE_DWARF || playerClass != CLASS_SHAMAN)" "dwarf shaman only")
foreach (spell 45504 45505 45514 45502)
  require_text("${helper}" "case ${spell}:" "allowed spell ${spell}")
endforeach()
string(REGEX MATCHALL "case [0-9]+:" cases "${helper}")
list(LENGTH cases case_count)
if (NOT case_count EQUAL 4)
  message(FATAL_ERROR "Exactly four allowed spells expected, found ${case_count}")
endif()
require_text("${player}"
  "if (abilityEntry->racemask && (abilityEntry->racemask & racemask) == 0 && !hordeRacialForDwarfShaman)"
  "race-mask bypass limited to the helper")
message(STATUS "DWARF_SHAMAN_RACIALS_CONTRACT=PASS")
