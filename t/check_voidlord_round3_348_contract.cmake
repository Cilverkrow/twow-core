if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#348 round 3 (owner 2026-09-28): Hellfire visual for the pulse, boss 20 % larger.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260928180000_world.sql" m)
foreach (required
    "`school` = 5, `castingTimeIndex` = 1, `rangeIndex` = 1, `manaCost` = 0,"
    "`effectImplicitTargetA1` = 22, `effectImplicitTargetB1` = 15, `effectRadiusIndex1` = 14,"
    "`effectBasePoints1` = 235, `effectDieSides1` = 42, `effectBonusCoefficient1` = -1"
    "WHERE `entry` = 2951;"
    "UPDATE `creature_template` SET `scale` = 5.4 WHERE `entry` IN (65201, 65202);")
  string(FIND "${m}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing #348 round 3 statement: ${required}")
  endif()
endforeach()
foreach (forbidden "DELETE " "REPLACE " "INSERT " "WHERE `entry` = 45559")
  string(FIND "${m}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Forbidden #348 round 3 scope: ${forbidden}")
  endif()
endforeach()
message(STATUS "VOIDLORD_ROUND3_348_CONTRACT=PASS")
