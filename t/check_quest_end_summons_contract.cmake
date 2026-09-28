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

# twow-repo#348: owner-decided quest completion summons, replay-safe and scoped.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260926170000_world.sql" m)
foreach (required
    "INSERT IGNORE INTO creature_template SELECT * FROM tw_348_void_lord;"
    "health_min = 1459830, health_max = 1459830, faction = 14"
    "loot_id = 0"
    "INSERT IGNORE INTO creature_spells"
    "34769, 100, 6, 2, 2, 2, 2"
    "WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41936 AND command = 10)"
    "WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41935 AND command = 10)"
    "WHERE NOT EXISTS (SELECT 1 FROM quest_end_scripts WHERE id = 41966 AND command = 10)")
  require_text("${m}" "${required}" "#348 migration")
endforeach()
# Only the temporary copy is updated; no live template, loot or spawn row is changed.
string(REGEX REPLACE "UPDATE tw_348_void_lord SET" "" without_temp_update "${m}")
foreach (forbidden "UPDATE " "DELETE " "REPLACE " "creature_loot_template" "INSERT INTO `creature`")
  forbid_text("${without_temp_update}" "${forbidden}" "#348 scope")
endforeach()
# twow-repo#348 round 2: scripted split encounter; only 65201/65202 templates are touched.
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260927130000_world.sql" m2)
foreach (required "SET scale = 4.5, script_name = 'boss_twisting_rift_voidlord'" "WHERE entry = 65201;" "entry = 65202, script_name = 'npc_twisting_rift_voidsplit', loot_id = 0" "INSERT IGNORE INTO creature_template SELECT * FROM tw_348_void_split;")
  require_text("${m2}" "${required}" "#348 round 2 migration")
endforeach()
foreach (forbidden "DELETE " "REPLACE " "creature_loot_template" "INSERT INTO `creature`")
  forbid_text("${m2}" "${forbidden}" "#348 round 2 scope")
endforeach()
file(READ "${TW_CORE_ROOT}/src/scripts/world/blasted_lands.cpp" script)
foreach (required "newscript->Name = \"boss_twisting_rift_voidlord\";" "newscript->Name = \"npc_twisting_rift_voidsplit\";" "SPELL_VOID_HELLFIRE             = 2951" "VOID_SPLIT_STEPS                = 4" "static float const VOID_START_SCALE = 5.4f;")
  require_text("${script}" "${required}" "#348 split script")
endforeach()
message(STATUS "QUEST_END_SUMMONS_CONTRACT=PASS")
