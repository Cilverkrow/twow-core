# Test suites for mod-playerbots: the PersistentActiveRoster subsystem and the
# PlayerBot event store.
#
# Included directly from the ROOT CMakeLists under BUILD_TESTING, NOT from
# mod-playerbots.cmake. modules/CMakeLists.txt reads that file only for a module
# whose linkage is not "disabled", and MODULES defaults to "disabled" -- so
# suites declared there would be un-buildable in exactly the configuration a
# test run wants (it is also read twice per configure, once per phase, which
# would declare every target twice). The suites are
# source-level -- they compile the module's own .cpp/.h files directly, not the
# `modules`/`modules_playerbots` library -- so they need the sources on disk and
# nothing about the vendored bot tree's own build.
#
# Registered here, in order:
#
#   persistent_active_roster              unit, always built
#   playerbot_legacy_event_write_guard    source scan, no build step
#   playerbot_level_flag_early_return_guard  source scan, no build step
#   playerbot_config_key_usage            Python source scan, no build step
#   premade_specs_rate2                   Python unit, no DBC dependency
#   playerbot_event_store_contract        unit, always built
#   bot_dialogue_policy                    unit, always built
#   world_thread_command_queue            unit, always built
#   perfmon_collection                    unit, always built
#   perfmon_init_reachable                Python source scan, no build step
#   persistent_active_roster_database_tests       opt-in, needs MariaDB, no add_test
#   playerbot_event_store_database_tests          opt-in, needs MariaDB, no add_test
#
# The two adapter suites take their connection string on argv and so cannot be
# add_test()ed; CI invokes them out of ${CMAKE_BINARY_DIR}/adapter-bin directly.
#
# This mirrors twow-repo's modules/mod-playerbots/tests.cmake, which carried
# these suites before the module moved into core (ADR-0040).

# This file's own directory. CMAKE_CURRENT_LIST_DIR is the directory of the
# file being processed, not of the file that include()d it, so this resolves to
# modules/mod-playerbots even though the include comes from the root
# CMakeLists.txt. The old ${CMAKE_SOURCE_DIR}/modules/mod-playerbots is the same
# path only while the core is the top-level project; under a platform that
# consumes it with add_subdirectory() it names the PLATFORM's modules tree, and
# every add_executable() below would list source files that are not there.
set(PB_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_package(Python3 COMPONENTS Interpreter REQUIRED)

option(BUILD_PLAYERBOT_EVENT_STORE_ADAPTER_TESTS
  "Build the isolated PlayerBot event-store database adapter test" OFF)

# --------------------------------------------------------------------------
# persistent_active_roster_tests -- the roster serialiser's unit suite.
#
# Hand-rolled assertions (no gtest): a plain main() returning non-zero on
# failure. Two translation units, no database, no test framework, which makes
# it the cheapest test in the tree.
# --------------------------------------------------------------------------

add_executable(persistent_active_roster_tests
  "${PB_MODULE_DIR}/t/persistent_active_roster_tests.cpp"
  "${PB_MODULE_DIR}/src/playerbot/PersistentActiveRoster.cpp")

target_include_directories(persistent_active_roster_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

if(WIN32)
  target_include_directories(persistent_active_roster_tests PRIVATE
    "${TW_CORE_ROOT}/dep/windows/include")
endif()

target_compile_definitions(persistent_active_roster_tests PRIVATE
  ROSTER_TEST_FIXTURE_DIR="${PB_MODULE_DIR}/t/fixtures")

# Variables rather than the OpenSSL::Crypto imported target: this repository's
# cmake/FindOpenSSL.cmake sits ahead of CMake's own module on CMAKE_MODULE_PATH.
# It sets OPENSSL_INCLUDE_DIR and OPENSSL_LIBRARIES but defines no imported
# targets, so OpenSSL::Crypto does not exist here. find_package(OpenSSL
# REQUIRED) has already run at the top level, so it is not repeated.
target_include_directories(persistent_active_roster_tests PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(persistent_active_roster_tests PRIVATE ${OPENSSL_LIBRARIES})

# ...and libcrypto by name, because OPENSSL_LIBRARIES is not enough here: this
# repository's FindOpenSSL.cmake searches only for "ssl", so on UNIX that
# variable resolves to libssl alone, and PersistentActiveRoster.cpp calls
# SHA256(), which lives in libcrypto. Without this the link fails with
# undefined references to SHA256.
if(UNIX)
  find_library(TW_OPENSSL_CRYPTO_LIBRARY NAMES crypto)
  if(NOT TW_OPENSSL_CRYPTO_LIBRARY)
    message(FATAL_ERROR
      "persistent_active_roster_tests needs libcrypto (SHA256) but it was not found; "
      "install the OpenSSL development package or set TW_OPENSSL_CRYPTO_LIBRARY.")
  endif()
  target_link_libraries(persistent_active_roster_tests PRIVATE ${TW_OPENSSL_CRYPTO_LIBRARY})
endif()

set_target_properties(persistent_active_roster_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME persistent_active_roster
  COMMAND persistent_active_roster_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# --------------------------------------------------------------------------
# playerbot_legacy_event_write_guard -- a source scan, not a compiled target.
#
# It greps every .cpp/.h under the module for a write statement naming the
# event-store table literally instead of going through
# ai::PlayerbotDatabaseContract. Nothing else in the tree catches that: a
# hardcoded table name compiles cleanly, links cleanly, and then quietly
# ignores AiPlayerbot.EventStoreTable forever.
#
# Registered with `cmake -P` rather than an executable so it costs no build
# time and runs on every ctest invocation, including a configure-only checkout.
# The script FATAL_ERRORs on a hit, and cmake -P exits non-zero on
# FATAL_ERROR, which is what ctest reads. See t/check_playerbot_legacy_writes.cmake
# for what it deliberately does not scan and why.
# --------------------------------------------------------------------------

add_test(NAME playerbot_legacy_event_write_guard
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/check_playerbot_legacy_writes.cmake")

# --------------------------------------------------------------------------
# playerbot_level_flag_early_return_guard -- also a source scan.
#
# AiPlayerbot.DisableRandomLevels was implemented as three blanket early
# returns that skipped spells, skills, professions, talents, mounts,
# reputations, repair and money along with the level roll. Bots were created
# unable to fight, so they never levelled, so the setting defeated the exact
# behaviour it exists to enable -- 5,021 of 5,039 characters at level 1 on the
# realm where it was found.
#
# Nothing else catches that shape: a blanket return compiles, links, runs, and
# produces a population that looks alive from everywhere except a database
# query. Same `cmake -P` form as the guard above, and for the same reason: it
# costs no build time and runs on a configure-only checkout.
# --------------------------------------------------------------------------

add_test(NAME playerbot_level_flag_early_return_guard
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/check_level_flag_early_returns.cmake")

# --------------------------------------------------------------------------
# premade_specs_rate2 -- deterministic unit coverage for the generated
# higher-rate talent paths. The live Turtle DBC verification remains an
# explicit generator invocation because DBC assets are not a source artifact.
# --------------------------------------------------------------------------

add_test(NAME premade_specs_rate2
  COMMAND "${Python3_EXECUTABLE}"
    "${PB_MODULE_DIR}/t/premade_specs_rate2_tests.py")

set_tests_properties(premade_specs_rate2 PROPERTIES
  ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# --------------------------------------------------------------------------
# playerbot_config_key_usage -- fails when a config key is loaded into a member
# that nothing ever reads.
#
# ~283 keys are loaded in PlayerbotAIConfig::Initialize. A member that is
# assigned and never read is a setting an operator can change to no effect, with
# no compile error (the assignment IS a use), no warning and no test. The only
# symptom is someone concluding the feature is broken.
#
# Python rather than `cmake -P` because the check has to distinguish a member
# assignment from a local's initialisation and a declaration from an assignment
# -- CMake's regex engine is not the tool for that, and getting it wrong makes a
# guard that reports false positives and then gets switched off.
#
# Six known-dead keys are allowlisted with reasons rather than deleted: they are
# upstream's, ADR-0040 keeps our delta upstream-shaped, and resolving them is
# twow-repo#8's decision. The allowlist is checked in both directions, so an
# entry that stops being dead is also an error.
# --------------------------------------------------------------------------

add_test(NAME playerbot_config_key_usage
  COMMAND "${Python3_EXECUTABLE}"
    "${PB_MODULE_DIR}/t/config_key_usage_tests.py"
    --module-dir "${PB_MODULE_DIR}")

set_tests_properties(playerbot_config_key_usage PROPERTIES
  ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# Quest-first policy is deliberately small and pure: the policy fixture locks
# the roster/default-off, level-boundary, reservation and safe-retirement
# contract, while the source scan pins the production hooks that apply it.
add_executable(quest_first_progression_policy_tests
  "${PB_MODULE_DIR}/t/quest_first_progression_policy_tests.cpp")

set_target_properties(quest_first_progression_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_first_progression_policy
  COMMAND quest_first_progression_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_first_progression_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/check_quest_first_progression_source_contract.cmake")

add_executable(progress_aware_turnin_recovery_policy_tests
  "${PB_MODULE_DIR}/t/progress_aware_turnin_recovery_policy_tests.cpp")

target_include_directories(progress_aware_turnin_recovery_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src")

set_target_properties(progress_aware_turnin_recovery_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME progress_aware_turnin_recovery_policy
  COMMAND progress_aware_turnin_recovery_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# #354/#292: who may direct a bot without GM rank, and which `.bot`/`.rndbot`
# commands stay GM tools. Decision table plus the call sites that use it.
add_executable(roster_control_policy_tests
  "${PB_MODULE_DIR}/t/roster_control_policy_tests.cpp")

target_include_directories(roster_control_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(roster_control_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME roster_control_policy
  COMMAND roster_control_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME roster_control_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/roster_control_source_contract_tests.cmake")

# twow-repo#419 variant B: `.bot roster`, the roster bot list without the /who
# cap. Filters, visibility, paging, rate limit and the BL1 addon wire format
# (golden file t/fixtures/roster_list_v1.txt), plus the call site.
add_executable(roster_list_policy_tests
  "${PB_MODULE_DIR}/t/roster_list_policy_tests.cpp")

target_include_directories(roster_list_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

target_compile_definitions(roster_list_policy_tests PRIVATE
  ROSTER_LIST_FIXTURE="${PB_MODULE_DIR}/t/fixtures/roster_list_v1.txt")

set_target_properties(roster_list_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME roster_list_policy
  COMMAND roster_list_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME roster_list_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/roster_list_source_contract_tests.cmake")

# #276: a dead bot waits for an active real-player master only for
# AiPlayerbot.DeadWaitForRealMasterSeconds, never indefinitely.
add_executable(master_wait_policy_tests
  "${PB_MODULE_DIR}/t/master_wait_policy_tests.cpp")

target_include_directories(master_wait_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(master_wait_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME master_wait_policy
  COMMAND master_wait_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME master_wait_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/master_wait_source_contract_tests.cmake")

# #303: a stale transport GUID must not freeze follow; far follow is logged.
add_test(NAME follow_transport_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/follow_transport_source_contract_tests.cmake")

# #303 part 2: no continental walk to a real-player master; far walks re-plan.
add_executable(far_follow_policy_tests
  "${PB_MODULE_DIR}/t/far_follow_policy_tests.cpp")

target_include_directories(far_follow_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(far_follow_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME far_follow_policy
  COMMAND far_follow_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME far_follow_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/far_follow_source_contract_tests.cmake")

# #308: every equip decision carries a reason code and the bagged-item
# decisions can be logged (default off) for the live equipment audit.
add_test(NAME equip_diagnostics_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/equip_diagnostics_source_contract_tests.cmake")

# #329 step 2: roster quest objectives travel on observed progress instead of
# the generic travel time budget.
add_test(NAME quest_objective_progress_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_objective_progress_source_contract_tests.cmake")

# --------------------------------------------------------------------------
# playerbot_event_store_contract_tests -- the unit suite for the three SQL
# builders in PlayerbotDatabaseContract.h. Same hand-rolled-assertion shape as
# the roster unit suite, and like it, cheap enough to be unconditional: one
# translation unit, header-only code under test, no database, no OpenSSL.
#
# t/stubs comes FIRST on the include path and ${PB_MODULE_DIR}/src is
# deliberately absent. PlayerbotDatabaseContract.h includes
# "playerbot/PlayerbotAIConfig.h", which is reachable only through
# ${PB_MODULE_DIR}/src -- so with src/playerbot alone on the path the real
# config header cannot be found at all, and t/stubs/playerbot supplies the
# minimal test double instead. That is not shadowing: there is exactly one
# candidate. Compiling the real one would mean compiling PlayerbotAIConfig.cpp
# and the whole module behind it.
# --------------------------------------------------------------------------

add_executable(playerbot_event_store_contract_tests
  "${PB_MODULE_DIR}/t/playerbot_event_store_contract_tests.cpp")

target_include_directories(playerbot_event_store_contract_tests PRIVATE
  "${PB_MODULE_DIR}/t/stubs"
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(playerbot_event_store_contract_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME playerbot_event_store_contract
  COMMAND playerbot_event_store_contract_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(profession_pair_policy_tests
  "${PB_MODULE_DIR}/t/profession_pair_policy_tests.cpp")

target_include_directories(profession_pair_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(profession_pair_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME profession_pair_policy
  COMMAND profession_pair_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# #276: alternate and travel graveyards stay within a bounded distance of the
# corpse, so a low-level bot is never revived in a far zone.
add_executable(graveyard_selection_policy_tests
  "${PB_MODULE_DIR}/t/graveyard_selection_policy_tests.cpp")

target_include_directories(graveyard_selection_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(graveyard_selection_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME graveyard_selection_policy
  COMMAND graveyard_selection_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME graveyard_selection_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/graveyard_selection_source_contract_tests.cmake")

# #389: formations v2 - filled slot grids for up to 40 bots (circle, half ring,
# wedge, triangle, block, column) with their own spacing and a radius cap.
add_executable(formation_grid_policy_tests
  "${PB_MODULE_DIR}/t/formation_grid_policy_tests.cpp")

target_include_directories(formation_grid_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(formation_grid_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME formation_grid_policy
  COMMAND formation_grid_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME formation_v2_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/formation_v2_source_contract_tests.cmake")

# #307: roster bots neither bind nor hearth to a zone clearly above their
# level (the Southshore trap).
add_executable(home_bind_policy_tests
  "${PB_MODULE_DIR}/t/home_bind_policy_tests.cpp")

target_include_directories(home_bind_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(home_bind_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME home_bind_policy
  COMMAND home_bind_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME home_bind_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/home_bind_source_contract_tests.cmake")

# #308: AiPlayerbot.AuctionHouse.Enabled = 0 removes the AH item usage and the
# AH sell/buy visits, and leaves vendor selling untouched.
add_test(NAME auction_house_switch_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/auction_house_switch_source_contract_tests.cmake")

# #307: quest targets across a continent below a minimum level, or in a zone
# clearly above the bot, are deferred instead of discovered by dying.
add_executable(route_danger_policy_tests
  "${PB_MODULE_DIR}/t/route_danger_policy_tests.cpp")

target_include_directories(route_danger_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(route_danger_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME route_danger_policy
  COMMAND route_danger_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(persistent_roster_profession_training_policy_tests
  "${PB_MODULE_DIR}/t/persistent_roster_profession_training_policy_tests.cpp")

target_include_directories(persistent_roster_profession_training_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(persistent_roster_profession_training_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME persistent_roster_profession_training_policy
  COMMAND persistent_roster_profession_training_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# The roster bag policy is intentionally independent from game objects. It
# verifies slot selection and hunter reserve semantics while the paired source
# contract below pins the only permitted runtime hook and Core equip sequence.
add_executable(persistent_roster_bag_policy_tests
  "${PB_MODULE_DIR}/t/persistent_roster_bag_policy_tests.cpp")

target_include_directories(persistent_roster_bag_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(persistent_roster_bag_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME persistent_roster_bag_policy
  COMMAND persistent_roster_bag_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(persistent_roster_starter_outfit_policy_tests
  "${PB_MODULE_DIR}/t/persistent_roster_starter_outfit_policy_tests.cpp")

target_include_directories(persistent_roster_starter_outfit_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(persistent_roster_starter_outfit_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME persistent_roster_starter_outfit_policy
  COMMAND persistent_roster_starter_outfit_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(persistent_roster_talent_spec_policy_tests
  "${PB_MODULE_DIR}/t/persistent_roster_talent_spec_policy_tests.cpp")

target_include_directories(persistent_roster_talent_spec_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(persistent_roster_talent_spec_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME persistent_roster_talent_spec_policy
  COMMAND persistent_roster_talent_spec_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# The remaining PR-1 trainer boundaries depend on game objects and cannot be
# exercised without a running world. Keep a narrow source-contract regression
# beside the policy unit test: it fails if a future generic trainer, autolearn
# or factory edit reintroduces a direct profession grant or removal.
add_test(NAME profession_pair_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/profession_pair_source_contract_tests.cmake")

add_test(NAME persistent_roster_profession_training_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/persistent_roster_profession_training_source_contract_tests.cmake")

# #335: quest givers/objectives use a quest tolerance (bot level + margin), not
# the grind gate that kept low-level bots without any quest route.
add_executable(quest_area_level_policy_tests
  "${PB_MODULE_DIR}/t/quest_area_level_policy_tests.cpp")

target_include_directories(quest_area_level_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(quest_area_level_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_area_level_policy
  COMMAND quest_area_level_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_area_level_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_area_level_source_contract_tests.cmake")

# #307: any destination a roster bot keeps dying at is skipped for a cooldown.
add_executable(destination_death_policy_tests
  "${PB_MODULE_DIR}/t/destination_death_policy_tests.cpp")

target_include_directories(destination_death_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(destination_death_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME destination_death_policy
  COMMAND destination_death_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME destination_death_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/destination_death_source_contract_tests.cmake")

# G4: death loop guard and death cooldown on the travel refresh path.
add_executable(death_loop_policy_tests
  "${PB_MODULE_DIR}/t/death_loop_policy_tests.cpp")

target_include_directories(death_loop_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(death_loop_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME death_loop_policy
  COMMAND death_loop_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME death_loop_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/death_loop_source_contract_tests.cmake")

# #307: shared danger map; roster bots avoid cells where other bots died to
# mobs clearly above their level.
find_package(Threads REQUIRED)

add_executable(danger_map_policy_tests
  "${PB_MODULE_DIR}/t/danger_map_policy_tests.cpp")

target_include_directories(danger_map_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

target_link_libraries(danger_map_policy_tests PRIVATE Threads::Threads)

set_target_properties(danger_map_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME danger_map_policy
  COMMAND danger_map_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME danger_map_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/danger_map_source_contract_tests.cmake")

# #307: roster bots leave zones clearly above their level.
add_executable(zone_escape_policy_tests
  "${PB_MODULE_DIR}/t/zone_escape_policy_tests.cpp")

target_include_directories(zone_escape_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(zone_escape_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME zone_escape_policy
  COMMAND zone_escape_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME zone_escape_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/zone_escape_source_contract_tests.cmake")

# #307: low-level roster bots grind within their reach and avoid repeated killers.
add_executable(grind_cap_policy_tests
  "${PB_MODULE_DIR}/t/grind_cap_policy_tests.cpp")

target_include_directories(grind_cap_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

target_link_libraries(grind_cap_policy_tests PRIVATE Threads::Threads)

set_target_properties(grind_cap_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME grind_cap_policy
  COMMAND grind_cap_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME grind_cap_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/grind_cap_source_contract_tests.cmake")

# #308: druid bear path 11.3 (tank), chosen by premade spec, not Primal Fury.
add_test(NAME bear_path_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/bear_path_source_contract_tests.cmake")

# #351: trainer and class-quest scans cached per class (level-up cost).
add_test(NAME trainer_cache_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/trainer_cache_source_contract_tests.cmake")
# #356: class quest tools the quest path misses (shaman totems), [ClassGrant].
add_executable(class_grant_policy_tests
  "${PB_MODULE_DIR}/t/class_grant_policy_tests.cpp")

target_include_directories(class_grant_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(class_grant_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME class_grant_policy
  COMMAND class_grant_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME class_grant_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/class_grant_source_contract_tests.cmake")

# #333: gathering targets open world only, plain level, no chat announcements.
add_test(NAME gather_level_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/gather_level_source_contract_tests.cmake")

add_test(NAME persistent_roster_bag_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/persistent_roster_bag_source_contract_tests.cmake")

# #357 / #367: a bot on a known premade path is not re-rolled onto another path.
add_test(NAME talent_path_hold_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/talent_path_hold_source_contract_tests.cmake")

# #333: roster bots gather and craft with their professions while levelling.
add_executable(profession_use_policy_tests
  "${PB_MODULE_DIR}/t/profession_use_policy_tests.cpp")

target_include_directories(profession_use_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(profession_use_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME profession_use_policy
  COMMAND profession_use_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME profession_use_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/profession_use_source_contract_tests.cmake")

# #301: a self-led one-member group is left (disbanded) on leave instead of
# stranding the roster bot as "already grouped".
add_executable(singleton_group_policy_tests
  "${PB_MODULE_DIR}/t/singleton_group_policy_tests.cpp")

target_include_directories(singleton_group_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(singleton_group_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME singleton_group_policy
  COMMAND singleton_group_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME singleton_group_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    "-DTW_CORE_ROOT=${TW_CORE_ROOT}"
    -P "${PB_MODULE_DIR}/t/singleton_group_source_contract_tests.cmake")

# #357 O-12: route B talent auras for 7.1 / 7.3 and the premade budget.
add_executable(spec_aura_policy_tests
  "${PB_MODULE_DIR}/t/spec_aura_policy_tests.cpp")

target_include_directories(spec_aura_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(spec_aura_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME spec_aura_policy
  COMMAND spec_aura_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME spec_aura_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/spec_aura_source_contract_tests.cmake")

# #365 step 1: bot-bot group config keys and the [BotGroup] diagnostic,
# default off, no behaviour change.
add_executable(bot_group_policy_tests
  "${PB_MODULE_DIR}/t/bot_group_policy_tests.cpp")

target_include_directories(bot_group_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(bot_group_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME bot_group_policy
  COMMAND bot_group_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME bot_group_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/bot_group_source_contract_tests.cmake")

# #277: "corpse run" on a dead, unreleased bot releases first instead of
# answering "I am not dead".
add_executable(corpse_run_policy_tests
  "${PB_MODULE_DIR}/t/corpse_run_policy_tests.cpp")

target_include_directories(corpse_run_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(corpse_run_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME corpse_run_policy
  COMMAND corpse_run_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME corpse_run_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/corpse_run_source_contract_tests.cmake")

# #405: quest objectives whose work phase keeps running out are skipped.
add_test(NAME quest_work_timeout_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_work_timeout_source_contract_tests.cmake")

# twow-repo#485: quest givers and takers bots cannot use are no travel targets
# (instance hand-in check, script-only game objects, cross-map stall key, death
# cooldown slot, continents-only cross-map rule).
add_test(NAME quest_taker_usable_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_taker_usable_source_contract_tests.cmake")

# #414: gather destinations are real herb/ore nodes only.
add_executable(gather_node_policy_tests
  "${PB_MODULE_DIR}/t/gather_node_policy_tests.cpp")

target_include_directories(gather_node_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(gather_node_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME gather_node_policy
  COMMAND gather_node_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME gather_node_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/gather_node_source_contract_tests.cmake")

# #422: death series and environmental gathering deaths.
add_executable(death_series_policy_tests
  "${PB_MODULE_DIR}/t/death_series_policy_tests.cpp")

target_include_directories(death_series_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(death_series_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME death_series_policy
  COMMAND death_series_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME death_series_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/death_series_source_contract_tests.cmake")

# #421: active quest search and the rescue teleport.
add_executable(quest_search_policy_tests
  "${PB_MODULE_DIR}/t/quest_search_policy_tests.cpp")

target_include_directories(quest_search_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(quest_search_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_search_policy
  COMMAND quest_search_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_search_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_search_source_contract_tests.cmake")

# #416 (7.5): value-cache eviction and prefix search.
add_executable(value_evict_policy_tests
  "${PB_MODULE_DIR}/t/value_evict_policy_tests.cpp")

target_include_directories(value_evict_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(value_evict_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME value_evict_policy
  COMMAND value_evict_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME value_evict_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/value_evict_source_contract_tests.cmake")

# twow-repo#357 S2-7: swords for shaman bots with the sword skills.
add_executable(shaman_weapon_policy_tests
  "${PB_MODULE_DIR}/t/shaman_weapon_policy_tests.cpp")

target_include_directories(shaman_weapon_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(shaman_weapon_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME shaman_weapon_policy
  COMMAND shaman_weapon_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME shaman_weapon_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/shaman_weapon_source_contract_tests.cmake")

# #416 (7.3): area levels without a world creature scan at runtime.
add_executable(area_level_policy_tests
  "${PB_MODULE_DIR}/t/area_level_policy_tests.cpp")

target_include_directories(area_level_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(area_level_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME area_level_policy
  COMMAND area_level_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME area_level_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/area_level_source_contract_tests.cmake")

# #416 (7.3): hourly [MemStores] diagnostic.
add_executable(mem_stores_policy_tests
  "${PB_MODULE_DIR}/t/mem_stores_policy_tests.cpp")

target_include_directories(mem_stores_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot"
  "${PB_MODULE_DIR}/../../src/game")

set_target_properties(mem_stores_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME mem_stores_policy
  COMMAND mem_stores_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME mem_stores_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    "-DCORE_SOURCE_DIR=${PB_MODULE_DIR}/../.."
    -P "${PB_MODULE_DIR}/t/mem_stores_source_contract_tests.cmake")

add_test(NAME hotfix81_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/hotfix81_source_contract_tests.cmake")

# #452: discarded route candidates free their temporary (portal) nodes.
add_test(NAME route_temp_nodes_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/route_temp_nodes_contract_tests.cmake")

# Hotfix 8.9 (twow-repo#474): [ItemUse] trace window.
add_executable(item_use_trace_tests
  "${PB_MODULE_DIR}/t/item_use_trace_tests.cpp")

target_include_directories(item_use_trace_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(item_use_trace_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME item_use_trace
  COMMAND item_use_trace_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# Hotfix 8.8 (twow-repo#474): no queued bot packets for game objects, allowlist for the rest.
add_test(NAME bot_queuepacket_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/bot_queuepacket_contract_tests.cmake")

# Hotfix 8.5 (twow-repo#329): declared profession purpose.
add_executable(gather_purpose_policy_tests
  "${PB_MODULE_DIR}/t/gather_purpose_policy_tests.cpp")

target_include_directories(gather_purpose_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(gather_purpose_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME gather_purpose_policy
  COMMAND gather_purpose_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# Hotfix 8.5 (twow-repo#329): turn-ins before gathering, grey quests dropped.
add_test(NAME hotfix85_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/hotfix85_source_contract_tests.cmake")

# Hotfix 8.3: fishing without recast loops.
add_executable(fishing_policy_tests
  "${PB_MODULE_DIR}/t/fishing_policy_tests.cpp")

target_include_directories(fishing_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(fishing_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME fishing_policy
  COMMAND fishing_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME fishing_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/fishing_source_contract_tests.cmake")

# Hotfix 8.2: unreachable targets.
add_executable(unreachable_policy_tests
  "${PB_MODULE_DIR}/t/unreachable_policy_tests.cpp")

target_include_directories(unreachable_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(unreachable_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME unreachable_policy
  COMMAND unreachable_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME unreachable_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/unreachable_source_contract_tests.cmake")

# Hotfix 8.1: [TankPath] diagnostic for bots on a tank path.
add_executable(tank_path_diag_policy_tests
  "${PB_MODULE_DIR}/t/tank_path_diag_policy_tests.cpp")

target_include_directories(tank_path_diag_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(tank_path_diag_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME tank_path_diag_policy
  COMMAND tank_path_diag_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME tank_path_diag_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/tank_path_diag_source_contract_tests.cmake")

# Train 8b: roster bots on their own skip red quests (accept/drop loops).
add_executable(quest_accept_policy_tests
  "${PB_MODULE_DIR}/t/quest_accept_policy_tests.cpp")

target_include_directories(quest_accept_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(quest_accept_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_accept_policy
  COMMAND quest_accept_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME quest_accept_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/quest_accept_source_contract_tests.cmake")

# #420: group bots buff their group only and drink late while following.
add_executable(group_buff_policy_tests
  "${PB_MODULE_DIR}/t/group_buff_policy_tests.cpp")

target_include_directories(group_buff_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(group_buff_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME group_buff_policy
  COMMAND group_buff_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME group_buff_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/group_buff_source_contract_tests.cmake")
# #416: no map thread waits for a destination job; time budget for the choice.
add_executable(travel_choose_policy_tests
  "${PB_MODULE_DIR}/t/travel_choose_policy_tests.cpp")

target_include_directories(travel_choose_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(travel_choose_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME travel_choose_policy
  COMMAND travel_choose_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME travel_choose_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/travel_choose_source_contract_tests.cmake")

# #416: slow bot updates are visible, route searches have a time budget.
add_executable(stall_guard_policy_tests
  "${PB_MODULE_DIR}/t/stall_guard_policy_tests.cpp")

target_include_directories(stall_guard_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

target_link_libraries(stall_guard_policy_tests PRIVATE Threads::Threads)

set_target_properties(stall_guard_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME stall_guard_policy
  COMMAND stall_guard_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME stall_guard_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/stall_guard_source_contract_tests.cmake")

# #405 (c): quest-only loot slots are the player's own.
add_executable(loot_slot_policy_tests
  "${PB_MODULE_DIR}/t/loot_slot_policy_tests.cpp")

target_include_directories(loot_slot_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(loot_slot_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME loot_slot_policy
  COMMAND loot_slot_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# #357 / #367: shaman and rogue tank paths (phase 1 skeleton, weight 0).
add_executable(tank_path_policy_tests
  "${PB_MODULE_DIR}/t/tank_path_policy_tests.cpp")

target_include_directories(tank_path_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(tank_path_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME tank_path_policy
  COMMAND tank_path_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME tank_path_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/tank_path_source_contract_tests.cmake")

# #341: role- and profession-aware group rolls for roster bots.
add_executable(loot_roll_policy_tests
  "${PB_MODULE_DIR}/t/loot_roll_policy_tests.cpp")

target_include_directories(loot_roll_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(loot_roll_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME loot_roll_policy
  COMMAND loot_roll_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME loot_roll_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/loot_roll_source_contract_tests.cmake")

# #365 step 2: ad-hoc quest groups.
add_executable(adhoc_group_policy_tests
  "${PB_MODULE_DIR}/t/adhoc_group_policy_tests.cpp")

target_include_directories(adhoc_group_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

target_link_libraries(adhoc_group_policy_tests PRIVATE Threads::Threads)

set_target_properties(adhoc_group_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME adhoc_group_policy
  COMMAND adhoc_group_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME adhoc_group_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/adhoc_group_source_contract_tests.cmake")

# #391: roster login waves after an L1 reset.
add_executable(login_wave_policy_tests
  "${PB_MODULE_DIR}/t/login_wave_policy_tests.cpp")

target_include_directories(login_wave_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(login_wave_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME login_wave_policy
  COMMAND login_wave_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME login_wave_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/login_wave_source_contract_tests.cmake")

# twow-repo#363: roster bots buy vendor gear upgrades up to a level cap.
add_executable(vendor_gear_policy_tests
  "${PB_MODULE_DIR}/t/vendor_gear_policy_tests.cpp")

target_include_directories(vendor_gear_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(vendor_gear_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME vendor_gear_policy
  COMMAND vendor_gear_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME vendor_gear_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/vendor_gear_source_contract_tests.cmake")

# OB-10 train 6: capped ammo / thrown weapon stock (142 Crude Throwing Axes).
add_executable(ammo_stock_policy_tests
  "${PB_MODULE_DIR}/t/ammo_stock_policy_tests.cpp")

target_include_directories(ammo_stock_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(ammo_stock_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ammo_stock_policy
  COMMAND ammo_stock_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ammo_stock_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/ammo_stock_source_contract_tests.cmake")

add_test(NAME persistent_roster_starter_outfit_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/persistent_roster_starter_outfit_source_contract_tests.cmake")

add_test(NAME persistent_roster_talent_spec_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_SOURCE_DIR=${PB_MODULE_DIR}/src/playerbot"
    -P "${PB_MODULE_DIR}/t/persistent_roster_talent_spec_source_contract_tests.cmake")

# Explicit player-requested catch-up is deliberately a narrow Core plus
# PlayerBot boundary. This source-level regression locks the exact live
# validator and action path without requiring a world database or a server.
add_test(NAME quest_catchup_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    "-DTW_CORE_ROOT=${TW_CORE_ROOT}"
    -P "${PB_MODULE_DIR}/t/quest_catchup_source_contract_tests.cmake")

# #379: the factory's race/class allow-list (incl. dwarf shaman, undead paladin),
# backed by playercreateinfo data, and a group fill that asks the factory.
add_test(NAME race_class_availability_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    "-DTW_CORE_ROOT=${TW_CORE_ROOT}"
    -P "${PB_MODULE_DIR}/t/race_class_availability_source_contract_tests.cmake")

# #275: `talk N` runs exactly the displayed gossip option (innkeeper bind via
# the client's Core handler) and never falls back to another option.
add_test(NAME gossip_select_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    "-DTW_CORE_ROOT=${TW_CORE_ROOT}"
    -P "${PB_MODULE_DIR}/t/gossip_select_source_contract_tests.cmake")

# #290 (owner 2026-09-26): every quest sharable behind a key; a roster bot
# admitted by the catch-up gets no "not eligible" first.
add_test(NAME quest_share_all_source_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    "-DTW_CORE_ROOT=${TW_CORE_ROOT}"
    -P "${PB_MODULE_DIR}/t/quest_share_all_source_contract_tests.cmake")

add_executable(bot_dialogue_policy_tests
  "${PB_MODULE_DIR}/t/bot_dialogue_policy_tests.cpp")

target_include_directories(bot_dialogue_policy_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

set_target_properties(bot_dialogue_policy_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME bot_dialogue_policy
  COMMAND bot_dialogue_policy_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# --------------------------------------------------------------------------
# world_thread_command_queue_tests -- the unit suite for the hand-off that
# keeps PlayerbotCommandServer connection threads out of the world's object
# graph (twow-repo issue #202). Same hand-rolled-assertion shape as the two
# suites above: a plain main() returning non-zero on failure, no gtest, no
# database, no OpenSSL.
#
# Two translation units and no stubs directory, which is the whole point of
# the design: WorldThreadCommandQueue.{h,cpp} name no game header and no
# playerbot header, so they compile standalone with src/playerbot on the path
# and nothing of the bot tree behind them. If a future edit reaches for a
# Player* or a PlayerbotAI* in there, this target stops building -- which is
# exactly the guard wanted, because that pointer is the bug.
# --------------------------------------------------------------------------

add_executable(world_thread_command_queue_tests
  "${PB_MODULE_DIR}/t/world_thread_command_queue_tests.cpp"
  "${PB_MODULE_DIR}/src/playerbot/WorldThreadCommandQueue.cpp")

target_include_directories(world_thread_command_queue_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot")

# std::thread and std::promise. Unlike the other unit suites this one starts
# threads, and on GCC/libstdc++ that needs -pthread at both compile and link
# time or std::thread's constructor throws system_error at runtime. The two
# suites above link no threading library because they start no threads.
find_package(Threads REQUIRED)
target_link_libraries(world_thread_command_queue_tests PRIVATE Threads::Threads)

set_target_properties(world_thread_command_queue_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME world_thread_command_queue
  COMMAND world_thread_command_queue_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# Bot dialogue replies are generated asynchronously but delivered on the
# bot's world-thread tick. Random-bot sessions have no socket, so feeding a
# generated CMSG_MESSAGECHAT into WorldSession::_recvQueue silently strands
# it behind CanProcessPackets(). Keep the direct chat-handler dispatch and
# the non-chat QueuePacket fallback explicit.
add_test(NAME bot_dialogue_delivery_source_contract
  COMMAND ${CMAKE_COMMAND}
    -DPLAYERBOT_AI_SOURCE=${PB_MODULE_DIR}/src/playerbot/PlayerbotAI.cpp
    -P ${PB_MODULE_DIR}/t/bot_dialogue_delivery_source_contract_tests.cmake)

# --------------------------------------------------------------------------
# persistent_active_roster_database_tests -- the same serialiser against a
# live MariaDB. Opt-in: it needs a running database, so it is not part of a
# plain testing build.
# --------------------------------------------------------------------------

if(BUILD_PERSISTENT_ROSTER_ADAPTER_TESTS)

add_executable(persistent_active_roster_database_tests
  "${PB_MODULE_DIR}/t/persistent_active_roster_database_tests.cpp"
  "${PB_MODULE_DIR}/src/playerbot/PersistentActiveRoster.cpp"
  "${PB_MODULE_DIR}/src/playerbot/PersistentActiveRosterDatabase.cpp")

target_include_directories(persistent_active_roster_database_tests PRIVATE
  "${PB_MODULE_DIR}/src/playerbot"
  "${TW_CORE_ROOT}/src/shared"
  "${TW_CORE_ROOT}/src/framework"
  "${TW_CORE_BINARY_ROOT}/src/shared"
  "${CMAKE_BINARY_DIR}"
  ${ACE_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR})

# The bundled Windows headers must not be on the include path elsewhere: they
# shadow the system OpenSSL and MySQL headers that ${OPENSSL_INCLUDE_DIR} and
# ${MYSQL_INCLUDE_DIR} already point at.
if(WIN32)
  target_include_directories(persistent_active_roster_database_tests PRIVATE
    "${TW_CORE_ROOT}/dep/include-windows"
    "${TW_CORE_ROOT}/dep/windows/include")
endif()

target_compile_definitions(persistent_active_roster_database_tests PRIVATE
  ROSTER_DATABASE_INJECTED_ONLY)

target_link_libraries(persistent_active_roster_database_tests PRIVATE
  shared
  framework
  ${ACE_LIBRARIES})

if(WIN32)
  # Separate debug/release import libraries are a Windows arrangement.
  # Elsewhere MYSQL_DEBUG_LIBRARY and OPENSSL_DEBUG_LIBRARIES are empty, and a
  # `debug` keyword followed by nothing is a hard CMake error:
  #   The "debug" argument must be followed by a library.
  target_link_libraries(persistent_active_roster_database_tests PRIVATE
    optimized ${MYSQL_LIBRARY}
    optimized ${OPENSSL_LIBRARIES}
    debug ${MYSQL_DEBUG_LIBRARY}
    debug ${OPENSSL_DEBUG_LIBRARIES}
    ws2_32)
else()
  # libcrypto by name, for the same reason the unit suite above needs it.
  # Linking `shared` does not reliably drag it in: with --as-needed (the
  # default on Debian/Ubuntu) a static library contributes nothing the final
  # link has not already asked for.
  target_link_libraries(persistent_active_roster_database_tests PRIVATE
    ${MYSQL_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${TW_OPENSSL_CRYPTO_LIBRARY})
endif()

set_target_properties(persistent_active_roster_database_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/adapter-bin")

endif()

# --------------------------------------------------------------------------
# playerbot_event_store_database_tests -- the event-store contract against a
# live MariaDB: 4000 queued writes to one row, 6400 across 64 rows, one precise
# delete, and InnoDB's deadlock counter checked before and after. Opt-in behind
# its own option for the same reason the roster adapter suite is: a plain
# BUILD_TESTING build must never require a running database.
#
# Separate from BUILD_PERSISTENT_ROSTER_ADAPTER_TESTS on purpose. The two
# suites need different schemas -- the roster one needs the roster migration,
# this one needs ai_playerbot_random_bots with a UNIQUE key over
# (owner, bot, event) -- so a CI job can enable one without being forced to
# provision the other. See the SCHEMA PRECONDITION note at the top of
# t/playerbot_event_store_database_tests.cpp: core's shipped schema declares
# that index NON-unique, and the suite refuses to run rather than reporting a
# meaningless failure.
#
# Its connection string arrives on argv, exactly as the roster adapter suite's
# does, which is why neither is registered with add_test(): ctest has no way to
# pass a disposable port to an add_test() command without baking it into the
# CMake cache. Both land in adapter-bin/ for a CI step to invoke directly.
# --------------------------------------------------------------------------

if(BUILD_PLAYERBOT_EVENT_STORE_ADAPTER_TESTS)

add_executable(playerbot_event_store_database_tests
  "${PB_MODULE_DIR}/t/playerbot_event_store_database_tests.cpp")

# t/stubs first, and no ${PB_MODULE_DIR}/src -- the same arrangement as the
# contract suite above, and for the same reason.
target_include_directories(playerbot_event_store_database_tests PRIVATE
  "${PB_MODULE_DIR}/t/stubs"
  "${PB_MODULE_DIR}/src/playerbot"
  "${TW_CORE_ROOT}/src/shared"
  "${TW_CORE_ROOT}/src/framework"
  "${TW_CORE_BINARY_ROOT}/src/shared"
  "${CMAKE_BINARY_DIR}"
  ${ACE_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR})

# The bundled Windows headers must not be on the include path elsewhere: they
# shadow the system OpenSSL and MySQL headers that ${OPENSSL_INCLUDE_DIR} and
# ${MYSQL_INCLUDE_DIR} already point at.
if(WIN32)
  target_include_directories(playerbot_event_store_database_tests PRIVATE
    "${TW_CORE_ROOT}/dep/include-windows"
    "${TW_CORE_ROOT}/dep/windows/include")
endif()

target_link_libraries(playerbot_event_store_database_tests PRIVATE
  shared
  framework
  ${ACE_LIBRARIES})

if(WIN32)
  # Separate debug/release import libraries are a Windows arrangement.
  # Elsewhere MYSQL_DEBUG_LIBRARY and OPENSSL_DEBUG_LIBRARIES are empty, and a
  # `debug` keyword followed by nothing is a hard CMake error:
  #   The "debug" argument must be followed by a library.
  target_link_libraries(playerbot_event_store_database_tests PRIVATE
    optimized ${MYSQL_LIBRARY}
    optimized ${OPENSSL_LIBRARIES}
    debug ${MYSQL_DEBUG_LIBRARY}
    debug ${OPENSSL_DEBUG_LIBRARIES}
    ws2_32)
else()
  # libcrypto by name, for the same reason the two suites above need it:
  # `shared` calls into it, and with --as-needed a static library contributes
  # nothing the final link has not already asked for.
  target_link_libraries(playerbot_event_store_database_tests PRIVATE
    ${MYSQL_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${TW_OPENSSL_CRYPTO_LIBRARY})
endif()

set_target_properties(playerbot_event_store_database_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/adapter-bin")

endif()

# twow-repo#290: the BotMenu client addon (addon/BotMenu-1.12). The contract
# checks every menu command against the chat triggers and the 1.12 Lua 5.0
# rules; the harness runs the addon against stand-ins of the 1.12 menu API
# where a Lua interpreter is available (not part of the CI image).
add_test(NAME botmenu_addon_contract
  COMMAND "${CMAKE_COMMAND}"
    "-DPB_MODULE_DIR=${PB_MODULE_DIR}"
    -P "${PB_MODULE_DIR}/t/botmenu_addon_contract_tests.cmake")

find_program(TW_LUA_EXECUTABLE NAMES lua5.1 lua5.0 lua)
if(TW_LUA_EXECUTABLE)
  add_test(NAME botmenu_addon_harness
    COMMAND "${TW_LUA_EXECUTABLE}" "${PB_MODULE_DIR}/t/botmenu_addon_harness.lua"
      "${PB_MODULE_DIR}/addon/BotMenu-1.12")
endif()

# --------------------------------------------------------------------------
# perfmon_collection / perfmon_init_reachable -- does the bot performance
# monitor actually collect anything?
#
# It did not. `.perfmon toggle` reported "Performance monitor enabled" and
# `.perfmon tick` then printed nothing, because PerformanceMonitor::Init() is the
# only writer of mapsData, start() returns nullptr for an unregistered
# (mapId, instanceId), and the single call to Init() sat near the bottom of
# RandomPlayerbotMgr::UpdateAIInternal -- below the randomBotAutologin/enabled
# gate and below both returns of the persistent-roster branch. On a persistent
# roster the call was unreachable and the instrument was inert. The platform's
# tick-latency roster gate depends on this monitor, so a dead monitor is a dead
# safety net.
#
# Two suites, because the bug had two halves and neither test catches the other:
#
#   perfmon_collection      the collector's contract: no bucket means no
#                           collection, a bucket plus a probe means a real
#                           elapsed time comes out and a report is printed.
#                           Compiles the REAL PerformanceMonitor.cpp.
#   perfmon_init_reachable  the call site: Init() is reached before the first
#                           return of RandomPlayerbotMgr::UpdateAIInternal, and
#                           the map-less (0, 0) bucket is registered explicitly.
#
# The relocated copy below is the awkward part, and it is not avoidable. A
# quote-include is searched in the directory of the file containing the directive
# before any -I path, so while PerformanceMonitor.cpp sits next to the real
# playerbot.h, `#include "playerbot.h"` finds that -- and the real one is
# Spell.h, World.h, ObjectMgr.h, Chat.h and the whole game library behind them.
# configure_file(COPYONLY) puts the unmodified .cpp somewhere with no sibling
# headers, where t/stubs-perfmon is the only candidate, and re-runs CMake when
# the original changes. The translation unit compiled is still the shipping one,
# byte for byte; only its neighbours change.
# --------------------------------------------------------------------------

find_package(Threads REQUIRED)

configure_file(
  "${PB_MODULE_DIR}/src/playerbot/PerformanceMonitor.cpp"
  "${CMAKE_BINARY_DIR}/perfmon-test-src/PerformanceMonitor.cpp"
  COPYONLY)

add_executable(perfmon_collection_tests
  "${PB_MODULE_DIR}/t/perfmon_collection_tests.cpp"
  "${CMAKE_BINARY_DIR}/perfmon-test-src/PerformanceMonitor.cpp")

# t/stubs-perfmon FIRST, and no ${PB_MODULE_DIR}/src: the same arrangement the
# event-store suites use with t/stubs, for the same reason. src/playerbot is on
# the path because the header under test, PerformanceMonitor.h, is the real one.
target_include_directories(perfmon_collection_tests PRIVATE
  "${PB_MODULE_DIR}/t/stubs-perfmon"
  "${PB_MODULE_DIR}/src/playerbot")

# The vendor's three feature macros, as mod-playerbots.cmake sets them on the
# module target. CMANGOS is load-bearing here and not cosmetic:
# PerformanceMonitorOperation declares its `started` timestamp under
# `#ifdef CMANGOS` while the constructor assigns it unconditionally, so without
# the macro the real .cpp does not compile.
target_compile_definitions(perfmon_collection_tests PRIVATE
  CMANGOS MANGOSBOT_ZERO ENABLE_PLAYERBOTS)

# The suite holds probes open with std::this_thread::sleep_for, because the
# monitor records whole milliseconds and drops a zero-length measurement.
target_link_libraries(perfmon_collection_tests PRIVATE Threads::Threads)

set_target_properties(perfmon_collection_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME perfmon_collection
  COMMAND perfmon_collection_tests
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME perfmon_init_reachable
  COMMAND "${Python3_EXECUTABLE}"
    "${PB_MODULE_DIR}/t/perfmon_init_reachable_tests.py"
    --module-dir "${PB_MODULE_DIR}")

set_tests_properties(perfmon_init_reachable PROPERTIES
  ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
