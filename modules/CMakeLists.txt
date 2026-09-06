GetModuleSourceList(MODULES_MODULE_LIST)

set(MODULES_PUBLIC_INCLUDES)
set(MODULES_STATIC_SOURCES)
set(MODULES_STATIC_MODULES)
set(MODULES_DYNAMIC_MODULES)
set(MODULES_DYNAMIC_PROJECTS)
set(MODULES_ENABLED_MODULES)

# mod-playerbots alone is large enough that folding it into the shared
# `modules` static archive pushes that archive's COFF size past the 4GB
# LNK1248 limit (full type/line debug info per object, MSVC Release build).
# It gets its own static library instead - still fully static-linked into
# mangosd (no export/import boundary, since core and playerbots call into
# each other directly with no dllexport plumbing), just a second archive.
set(MODULES_SPLIT_STATIC_MODULES mod-playerbots)
set(MODULES_PLAYERBOTS_STATIC_SOURCES)
set(MODULES_PLAYERBOTS_PUBLIC_INCLUDES)

if(MODULES STREQUAL "dynamic")
  set(MODULES_DEFAULT_LINKAGE "dynamic")
elseif(MODULES STREQUAL "static")
  set(MODULES_DEFAULT_LINKAGE "static")
else()
  set(MODULES_DEFAULT_LINKAGE "disabled")
endif()

if(WIN32)
  set(MODULES_BUILD_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/modules")
else()
  set(MODULES_BUILD_RUNTIME_DIR "${CMAKE_BINARY_DIR}/modules")
endif()

set(MODULES_INSTALL_RUNTIME_DIR "${LIBS_DIR}/modules")

# Rooted at TW_CORE_ROOT, not CMAKE_SOURCE_DIR. CMAKE_SOURCE_DIR is the
# TOP-LEVEL source directory, so under a platform that consumes the core with
# add_subdirectory() every one of these pointed into the PLATFORM, which holds
# no src/ and no dep/ -- every module translation unit then failed on the first
# core header it included. TW_CORE_ROOT is "this directory, whoever is on top"
# and is identical to CMAKE_SOURCE_DIR when the core is built standalone.
#
# CMAKE_CURRENT_SOURCE_DIR stays: this file IS the modules tree that drives the
# build (TW_MODULES_DIR), and that is what the entry means -- the core's own
# modules/ is not necessarily it.
set(MODULES_COMMON_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${TW_CORE_ROOT}/src/scripts
  ${TW_CORE_ROOT}/src/shared
  ${TW_CORE_ROOT}/src/framework
  ${TW_CORE_ROOT}/src/framework/Network
  ${TW_CORE_ROOT}/src/game
  ${TW_CORE_ROOT}/src/game/AI
  ${TW_CORE_ROOT}/src/game/Anticheat
  ${TW_CORE_ROOT}/src/game/AuctionHouse
  ${TW_CORE_ROOT}/src/game/Battlegrounds
  ${TW_CORE_ROOT}/src/game/Chat
  ${TW_CORE_ROOT}/src/game/Commands
  ${TW_CORE_ROOT}/src/game/Database
  ${TW_CORE_ROOT}/src/game/Group
  ${TW_CORE_ROOT}/src/game/Guild
  ${TW_CORE_ROOT}/src/game/Handlers
  ${TW_CORE_ROOT}/src/game/LFG
  ${TW_CORE_ROOT}/src/game/Mail
  ${TW_CORE_ROOT}/src/game/Maps
  ${TW_CORE_ROOT}/src/game/Maps/Pool
  ${TW_CORE_ROOT}/src/game/Movement
  ${TW_CORE_ROOT}/src/game/Movement/spline
  ${TW_CORE_ROOT}/src/game/Objects
  ${TW_CORE_ROOT}/src/game/OutdoorPvP
  ${TW_CORE_ROOT}/src/game/PlayerBots
  ${TW_CORE_ROOT}/src/game/Protocol
  ${TW_CORE_ROOT}/src/game/Spells
  ${TW_CORE_ROOT}/src/game/Threat
  ${TW_CORE_ROOT}/src/game/Transports
  ${TW_CORE_ROOT}/src/game/vmap
  ${TW_CORE_ROOT}/dep/include
  ${TW_CORE_ROOT}/dep/recastnavigation
  ${TW_CORE_ROOT}/dep/include/g3dlite
  ${TW_CORE_ROOT}/dep/recastnavigation/Detour/Include
  ${CMAKE_BINARY_DIR}
  ${TW_CORE_BINARY_ROOT}/src/shared
  ${TBB_INCLUDE_DIRS}
  ${ACE_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIR}
)

function(GetModuleProjectName module variable)
  string(TOLOWER "mod_${module}" MODULE_PROJECT_NAME)
  string(REGEX REPLACE "[^a-z0-9_]" "_" MODULE_PROJECT_NAME "${MODULE_PROJECT_NAME}")
  set(${variable} "${MODULE_PROJECT_NAME}" PARENT_SCOPE)
endfunction()

function(GetModuleLoaderFunction module variable)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" MODULE_FUNCTION_NAME "${module}")
  set(${variable} "Add${MODULE_FUNCTION_NAME}Scripts" PARENT_SCOPE)
endfunction()

function(GetModuleScopedProperty module_variable property_suffix variable)
  TortoiseGetGlobalProperty("TW_${module_variable}_${property_suffix}" PROPERTY_VALUE)
  set(${variable} ${PROPERTY_VALUE} PARENT_SCOPE)
endfunction()

function(ConfigureModuleLoader loader_name loader_out is_dynamic)
  set(TORTOISE_IS_DYNAMIC_MODULE_LOADER ${is_dynamic})
  set(TORTOISE_CURRENT_MODULE_PROJECT "${loader_name}")
  set(TORTOISE_MODULES_BUILD_DIRECTIVE "${CMAKE_BUILD_TYPE}")
  unset(TORTOISE_MODULES_FORWARD_DECL)
  unset(TORTOISE_MODULES_INVOKE)
  unset(TORTOISE_MODULES_EXTRA_INCLUDES)
  unset(TORTOISE_MODULES_EXTRA_INVOKE)

  unset(TW_ADD_SCRIPTS_INCLUDE)
  unset(TW_ADD_SCRIPTS_LIST)
  foreach(MODULE_NAME ${ARGN})
    ModuleNameToVariable("${MODULE_NAME}" MODULE_VARIABLE)
    GetModuleScopedProperty("${MODULE_VARIABLE}" "ADD_SCRIPTS_INCLUDE" MODULE_SCRIPT_INCLUDES)
    GetModuleScopedProperty("${MODULE_VARIABLE}" "ADD_SCRIPTS_LIST" MODULE_SCRIPT_LOADERS)
    list(APPEND TW_ADD_SCRIPTS_INCLUDE ${MODULE_SCRIPT_INCLUDES})
    list(APPEND TW_ADD_SCRIPTS_LIST ${MODULE_SCRIPT_LOADERS})
  endforeach()
  list(REMOVE_DUPLICATES TW_ADD_SCRIPTS_INCLUDE)

  foreach(TW_SCRIPT_INCLUDE ${TW_ADD_SCRIPTS_INCLUDE})
    set(TORTOISE_MODULES_EXTRA_INCLUDES "${TORTOISE_MODULES_EXTRA_INCLUDES}#include \"${TW_SCRIPT_INCLUDE}\"\n")
  endforeach()

  foreach(TW_SCRIPT_LOADER ${TW_ADD_SCRIPTS_LIST})
    string(REGEX REPLACE ";$" "" TW_SCRIPT_LOADER_CALL "${TW_SCRIPT_LOADER}")
    set(TORTOISE_MODULES_EXTRA_INVOKE "${TORTOISE_MODULES_EXTRA_INVOKE}    ${TW_SCRIPT_LOADER_CALL};\n")
  endforeach()

  foreach(MODULE_NAME ${ARGN})
    GetModuleLoaderFunction("${MODULE_NAME}" MODULE_LOADER_FUNCTION)
    set(TORTOISE_MODULES_FORWARD_DECL "${TORTOISE_MODULES_FORWARD_DECL}void ${MODULE_LOADER_FUNCTION}();\n")
    set(TORTOISE_MODULES_INVOKE "${TORTOISE_MODULES_INVOKE}    ${MODULE_LOADER_FUNCTION}();\n")
  endforeach()

  set(GENERATED_LOADER "${CMAKE_CURRENT_BINARY_DIR}/generated/${loader_name}/ModulesLoader.cpp")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/ModulesLoader.cpp.in.cmake" "${GENERATED_LOADER}")
  set(${loader_out} "${GENERATED_LOADER}" PARENT_SCOPE)
endfunction()

foreach(SOURCE_MODULE ${MODULES_MODULE_LIST})
  GetPathToModuleSource("${SOURCE_MODULE}" MODULE_SOURCE_PATH)
  ModuleNameToVariable("${SOURCE_MODULE}" MODULE_VARIABLE)
  GetModuleProjectName("${SOURCE_MODULE}" MODULE_PROJECT_NAME)

  if("${${MODULE_VARIABLE}}" STREQUAL "default")
    set(MODULE_LINKAGE "${MODULES_DEFAULT_LINKAGE}")
  else()
    set(MODULE_LINKAGE "${${MODULE_VARIABLE}}")
  endif()

  if(NOT MODULE_LINKAGE STREQUAL "disabled")
    set(TORTOISE_MODULE_CMAKE_PHASE DISCOVERY)
    set(TORTOISE_CURRENT_MODULE "${SOURCE_MODULE}")
    set(TORTOISE_CURRENT_MODULE_VARIABLE "${MODULE_VARIABLE}")
    set(TORTOISE_CURRENT_MODULE_LINKAGE "${MODULE_LINKAGE}")
    # Resolve through TW_MODULE_ROOTS rather than assuming the default root.
    # GetModuleSourceList discovers modules across every root, so hardcoding
    # ${CMAKE_SOURCE_DIR}/modules meant a module found under any OTHER root had
    # its .cmake looked for in a directory it is not in -- and because the
    # include is OPTIONAL, that is a silent skip, not an error. The module
    # compiles with none of its own wiring: no compile definitions, no link
    # libraries, no install rules, and no diagnostic.
    GetPathToModule("${SOURCE_MODULE}" MODULE_CMAKE_DIR)
    include("${MODULE_CMAKE_DIR}/${SOURCE_MODULE}.cmake" OPTIONAL)
    unset(TORTOISE_CURRENT_MODULE_LINKAGE)
    unset(TORTOISE_CURRENT_MODULE_VARIABLE)
    unset(TORTOISE_CURRENT_MODULE)
    unset(TORTOISE_MODULE_CMAKE_PHASE)

    list(APPEND MODULES_ENABLED_MODULES "${SOURCE_MODULE}")

    GetModuleScopedProperty("${MODULE_VARIABLE}" "SCRIPTS_SOURCES" MODULE_EXTRA_SCRIPT_SOURCES)
    if(MODULE_EXTRA_SCRIPT_SOURCES)
      list(REMOVE_DUPLICATES MODULE_EXTRA_SCRIPT_SOURCES)
    endif()
    GetModuleScopedProperty("${MODULE_VARIABLE}" "SCRIPTS_INCLUDE_DIRS" MODULE_EXTRA_SCRIPT_INCLUDE_DIRS)
    if(MODULE_EXTRA_SCRIPT_INCLUDE_DIRS)
      list(REMOVE_DUPLICATES MODULE_EXTRA_SCRIPT_INCLUDE_DIRS)
    endif()
  endif()

  if(NOT MODULE_LINKAGE STREQUAL "disabled")
    GetPathToModule("${SOURCE_MODULE}" MODULE_CONF_DIR)
    file(GLOB MODULE_CONFIG_DIST_FILES CONFIGURE_DEPENDS
      "${MODULE_CONF_DIR}/conf/*.conf.dist")

    foreach(MODULE_CONFIG_DIST_FILE ${MODULE_CONFIG_DIST_FILES})
      CopyModuleConfig("${MODULE_CONFIG_DIST_FILE}")
    endforeach()
  endif()

  if(MODULE_LINKAGE STREQUAL "static")
    CollectModuleSourceFiles("${MODULE_SOURCE_PATH}" MODULE_SOURCE_FILES)
    CollectModuleIncludeDirectories("${MODULE_SOURCE_PATH}" MODULE_INCLUDE_DIRS)
    if(SOURCE_MODULE IN_LIST MODULES_SPLIT_STATIC_MODULES)
      list(APPEND MODULES_PLAYERBOTS_STATIC_SOURCES ${MODULE_SOURCE_FILES})
      list(APPEND MODULES_PLAYERBOTS_STATIC_SOURCES ${MODULE_EXTRA_SCRIPT_SOURCES})
      list(APPEND MODULES_PLAYERBOTS_PUBLIC_INCLUDES ${MODULE_INCLUDE_DIRS})
      list(APPEND MODULES_PLAYERBOTS_PUBLIC_INCLUDES ${MODULE_EXTRA_SCRIPT_INCLUDE_DIRS})
    else()
      list(APPEND MODULES_STATIC_SOURCES ${MODULE_SOURCE_FILES})
      list(APPEND MODULES_STATIC_SOURCES ${MODULE_EXTRA_SCRIPT_SOURCES})
      list(APPEND MODULES_PUBLIC_INCLUDES ${MODULE_INCLUDE_DIRS})
      list(APPEND MODULES_PUBLIC_INCLUDES ${MODULE_EXTRA_SCRIPT_INCLUDE_DIRS})
    endif()
    list(APPEND MODULES_STATIC_MODULES "${SOURCE_MODULE}")
  elseif(MODULE_LINKAGE STREQUAL "dynamic")
    CollectModuleSourceFiles("${MODULE_SOURCE_PATH}" MODULE_SOURCE_FILES)
    CollectModuleIncludeDirectories("${MODULE_SOURCE_PATH}" MODULE_INCLUDE_DIRS)
    ConfigureModuleLoader("${SOURCE_MODULE}" MODULE_DYNAMIC_LOADER ON "${SOURCE_MODULE}")

    add_library(${MODULE_PROJECT_NAME} SHARED
      ${MODULE_SOURCE_FILES}
      ${MODULE_EXTRA_SCRIPT_SOURCES}
      ${MODULE_DYNAMIC_LOADER})

    target_include_directories(${MODULE_PROJECT_NAME}
      PUBLIC
        ${MODULES_COMMON_INCLUDES}
        ${MODULE_INCLUDE_DIRS}
        ${MODULE_EXTRA_SCRIPT_INCLUDE_DIRS})

    if(WIN32)
      target_link_libraries(${MODULE_PROJECT_NAME}
        PUBLIC
          game
          shared
          framework)
    else()
      target_link_libraries(${MODULE_PROJECT_NAME}
        PUBLIC
          fmt)
    endif()

    set_target_properties(${MODULE_PROJECT_NAME}
      PROPERTIES
        FOLDER "modules"
        OUTPUT_NAME "${SOURCE_MODULE}"
        LIBRARY_OUTPUT_DIRECTORY "${MODULES_BUILD_RUNTIME_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${MODULES_BUILD_RUNTIME_DIR}")

    install(TARGETS ${MODULE_PROJECT_NAME}
      LIBRARY DESTINATION "${MODULES_INSTALL_RUNTIME_DIR}"
      RUNTIME DESTINATION "${MODULES_INSTALL_RUNTIME_DIR}")

    list(APPEND MODULES_DYNAMIC_MODULES ${SOURCE_MODULE})
    list(APPEND MODULES_DYNAMIC_PROJECTS ${MODULE_PROJECT_NAME})
  elseif(NOT MODULE_LINKAGE STREQUAL "disabled")
    message(FATAL_ERROR "Unknown linkage \"${MODULE_LINKAGE}\" for module ${SOURCE_MODULE}.")
  endif()
endforeach()

ConfigureModuleLoader("static" MODULE_STATIC_LOADER OFF ${MODULES_STATIC_MODULES})

add_library(modules STATIC
  ModulesScriptLoader.h
  ${MODULE_STATIC_LOADER}
  ${MODULES_STATIC_SOURCES})

target_include_directories(modules
  PUBLIC
    ${MODULES_COMMON_INCLUDES}
    ${MODULES_PUBLIC_INCLUDES})

target_link_libraries(modules
  PUBLIC
    game)

set_target_properties(modules PROPERTIES FOLDER "modules")

if(MODULES_PLAYERBOTS_STATIC_SOURCES)
  add_library(modules_playerbots STATIC
    ${MODULES_PLAYERBOTS_STATIC_SOURCES})

  target_include_directories(modules_playerbots
    PUBLIC
      ${MODULES_COMMON_INCLUDES}
      ${MODULES_PLAYERBOTS_PUBLIC_INCLUDES})

  target_link_libraries(modules_playerbots
    PUBLIC
      game)

  set_target_properties(modules_playerbots PROPERTIES FOLDER "modules")
endif()

ProcessModuleConfigCopies(modules)

set(TORTOISE_MODULE_CMAKE_PHASE POST_TARGETS)
foreach(SOURCE_MODULE ${MODULES_ENABLED_MODULES})
  ModuleNameToVariable("${SOURCE_MODULE}" MODULE_VARIABLE)
  if("${${MODULE_VARIABLE}}" STREQUAL "default")
    set(TORTOISE_CURRENT_MODULE_LINKAGE "${MODULES_DEFAULT_LINKAGE}")
  else()
    set(TORTOISE_CURRENT_MODULE_LINKAGE "${${MODULE_VARIABLE}}")
  endif()
  set(TORTOISE_CURRENT_MODULE "${SOURCE_MODULE}")
  set(TORTOISE_CURRENT_MODULE_VARIABLE "${MODULE_VARIABLE}")
  GetPathToModule("${SOURCE_MODULE}" MODULE_CMAKE_DIR)
  include("${MODULE_CMAKE_DIR}/${SOURCE_MODULE}.cmake" OPTIONAL)
  unset(TORTOISE_CURRENT_MODULE_VARIABLE)
  unset(TORTOISE_CURRENT_MODULE)
  unset(TORTOISE_CURRENT_MODULE_LINKAGE)
endforeach()
unset(TORTOISE_MODULE_CMAKE_PHASE)

set(WORLDSERVER_DYNAMIC_MODULES_DEPENDENCIES ${MODULES_DYNAMIC_PROJECTS} PARENT_SCOPE)
set(WORLDSERVER_DYNAMIC_MODULES ${MODULES_DYNAMIC_MODULES} PARENT_SCOPE)
set(WORLDSERVER_ENABLED_MODULES ${MODULES_ENABLED_MODULES} PARENT_SCOPE)
set(MODULES_BUILD_RUNTIME_DIR "${MODULES_BUILD_RUNTIME_DIR}" PARENT_SCOPE)
