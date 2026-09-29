if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Missing ${label}: ${needle}")
  endif()
endfunction()

# twow-repo#443: Karrsh the Sentinel (62934) runs the C++ script with four own totem entries,
# reuses client spells (no new spell ids) and follows the owner timings.
set(script_file "${TW_CORE_ROOT}/src/scripts/dungeons/timbermaw_hold/boss_karrsh_the_sentinel.cpp")
file(READ "${script_file}" script)
file(READ "${TW_CORE_ROOT}/src/scripts/ScriptLoader.cpp" loader)
file(READ "${TW_CORE_ROOT}/src/scripts/CMakeLists.txt" scripts_cmake)
file(READ "${TW_CORE_ROOT}/sql/database_updates/20260930100000_world.sql" migration)

require_text("${loader}" "AddSC_boss_karrsh_the_sentinel();" "script loader registration")
require_text("${scripts_cmake}" "dungeons/timbermaw_hold/boss_karrsh_the_sentinel.cpp" "script source in build")

foreach (value "EarthbindAttemptMs  = 10000" "EarthbindRetryMs    = 8000" "EarthbindLifeMs     = 20000"
               "EarthbindMax        = 3" "EarthbindRange      = 45.0f" "RotatingTotemMs     = 20000"
               "RotatingLifeMs      = 15000" "RushMs              = 45000" "RushRange           = 30.0f"
               "FrostShockDurMs     = 60000" "GetHealthPercent() <= 50.0f")
  require_text("${script}" "${value}" "owner timing")
endforeach()

# Only existing spell ids (client visuals without a patch): nothing from the custom 903xx range.
string(REGEX MATCH "= 903[0-9][0-9]," custom_spell "${script}")
if (custom_spell)
  message(FATAL_ERROR "Karrsh must reuse client spells, found custom id ${custom_spell}")
endif()

foreach (entry 65300 65301 65302 65303)
  require_text("${script}" "= ${entry}," "totem entry in script")
  require_text("${migration}" "`entry` = ${entry}," "totem clone in migration")
endforeach()
require_text("${migration}" "`script_name` = 'boss_karrsh_the_sentinel'" "boss script binding")
require_text("${migration}" "`script_name` = 'npc_karrsh_totem'" "totem script binding")
require_text("${migration}" "`health_min` = 800, `health_max` = 800" "owner totem health")

# Statements insert or update only; deletes live in the rollback comment.
string(REGEX REPLACE "--[^\n]*" "" statements "${migration}")
foreach (forbidden "DELETE" "REPLACE INTO" "TRUNCATE")
  string(FIND "${statements}" "${forbidden}" at)
  if (NOT at EQUAL -1)
    message(FATAL_ERROR "Karrsh migration must not ${forbidden}")
  endif()
endforeach()

message(STATUS "KARRSH_443_CONTRACT=PASS")
