# Test suites for the PersistentActiveRoster subsystem in mod-playerbots.
#
# Included directly from the ROOT CMakeLists under BUILD_TESTING, NOT from
# mod-playerbots.cmake. mod-playerbots.cmake returns immediately when
# BUILD_PLAYERBOTS is OFF (its default here), which would make these targets
# un-buildable in exactly the configuration a test run wants. Both suites are
# source-level -- they compile PersistentActiveRoster's own .cpp files
# directly, not the `modules`/`modules_playerbots` library -- so they need the
# sources on disk and nothing about the vendored bot tree's own build.
#
# This mirrors twow-repo's modules/mod-playerbots/tests.cmake, which carried
# these same two suites before the module moved into core (ADR-0040).

set(PB_MODULE_DIR "${CMAKE_SOURCE_DIR}/modules/mod-playerbots")

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
