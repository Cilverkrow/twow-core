# Included from modules/CMakeLists.txt, once in the DISCOVERY phase and once
# AFTER the module targets exist (POST_TARGETS).
#
# There is deliberately no `if(NOT BUILD_PLAYERBOTS) return()` here any more.
# modules/CMakeLists.txt includes this file ONLY for a module whose effective
# linkage is not "disabled" -- so by the time it is read, the bot sources are
# already being compiled into `modules_playerbots` (or into a shared library).
# The guard could therefore never prevent the build; all it could do was skip
# the wiring of a module that was being built anyway, which is precisely what
# it did for `-DMODULES=static` with BUILD_PLAYERBOTS left at its OFF default:
# no CMANGOS / MANGOSBOT_ZERO / ENABLE_PLAYERBOTS, no botpch.h force-include,
# no Boost. That is not a disabled subsystem, it is a broken one -- the vendored
# #ifdef ladders fall off the end of non-void functions (see the PUBLIC note
# below) and cmangos-compat-shim.h never reaches a translation unit.
#
# src/game already made exactly this move for PlayerbotStubs.cpp: it asks
# GetModuleEffectiveLinkage("mod-playerbots") rather than BUILD_PLAYERBOTS,
# because "was the option set" and "was the module built" are different
# questions and only the second one decides whether the eleven host hooks need
# stubbing. This file was still asking the first, and a consumer that enables
# the module without knowing about core's option -- a platform repository
# consuming core with add_subdirectory() -- got the broken half.
#
# BUILD_PLAYERBOTS keeps its meaning for the standalone core: it is the
# convenience switch that turns the module build on (root CMakeLists.txt flips
# MODULES to "static" for it) and it still drives mangosd's Windows Boost
# library search path. It is no longer a second, weaker gate on the wiring.

# CMAKE_CURRENT_LIST_DIR: this file's own directory, whichever module root it
# was found under, and whoever is the top-level project. The old spelling
# ${CMAKE_SOURCE_DIR}/modules/mod-playerbots is the same path only while the
# core IS the whole project.
set(PB_ROOT "${CMAKE_CURRENT_LIST_DIR}")

# Boost. The vendored sources reach for it directly (TravelNode's mmap scan uses
# boost::filesystem::directory_iterator) and botpch.h used to carry the headers;
# the old vendor CMakeLists did the find_package and the linking. Both have to
# come along, or the build fails deep inside a source file with
# "'boost::filesystem' has not been declared".
find_package(Boost 1.70 REQUIRED COMPONENTS thread filesystem system)

# Both linkage modes: the static path folds this module into `modules`, the
# dynamic path gives it a target of its own. That target is `mod_mod_playerbots`
# with UNDERSCORES - the module system replaces the hyphens in `mod-playerbots`.
# I had written `mod_mod-playerbots` here, which matches nothing, so the dynamic
# build silently received no defines, no shim and no Boost and died in
# WorldPosition.h on `'discrete_distribution' is not a member of 'std'`. Both
# spellings are listed because a target that does not exist is skipped anyway.
foreach(PB_TARGET modules modules_playerbots mod_mod_playerbots mod_mod-playerbots)
  if(NOT TARGET ${PB_TARGET})
    continue()
  endif()

  #   CMANGOS         - selects the cmangos codepath in vendored headers
  #                     (vs. TrinityCore / MaNGOS-Zero alternates).
  #   MANGOSBOT_ZERO  - Classic (1.12). Switches level caps, talent trees,
  #                     spell ranges. MANGOSBOT_ONE for TBC, _TWO for WotLK.
  #   ENABLE_PLAYERBOTS - the vendor tree's own on/off wall.
  #
  # PUBLIC, not PRIVATE, and this is a bug fix rather than tidying.
  #
  # mod-dungeon-clear includes sixteen of this module's headers and therefore
  # compiled them with none of the three macros defined. That is not merely "two
  # views of the same type" -- it produces undefined behaviour, because the
  # vendored code is written as:
  #
  #     bool UnitIsDead(Unit *unit)
  #     {
  #     #ifdef MANGOS
  #         return unit->IsDead();
  #     #endif
  #     #ifdef CMANGOS
  #         return unit->IsDead();
  #     #endif
  #     }
  #
  # With neither macro set the body has no return statement at all, and falling
  # off the end of a non-void function is UB -- GCC plants a ud2 there. The
  # REF-005 warning inventory found 357 -Wreturn-type emissions in
  # ServerFacade.h alone, roughly one per dungeon-clear translation unit, which
  # is what pointed at this.
  #
  # These macros are a usage requirement of the library, not a private detail:
  # anything compiling these headers must compile them the same way this module
  # does. PUBLIC is what says that.
  target_compile_definitions(${PB_TARGET} PUBLIC CMANGOS MANGOSBOT_ZERO ENABLE_PLAYERBOTS)

  # The vendored sources were built with botpch.h. Besides speeding up their
  # build, it is their common compatibility boundary: cmangos-compat-shim.h
  # maps CMaNGOS names used throughout the bot sources to this core's API.
  # The aggregate module target used after the move had no PCH, so every one
  # of those declarations silently disappeared from the translation units.
  # botpch.h is not an optimisation here, it is the compatibility boundary
  # (cmangos-compat-shim.h: GuidSet, AreaTableEntry, GenericTransport, ...).
  # It has to reach EVERY translation unit whether or not precompiled headers
  # are in use. target_precompile_headers silently does nothing when PCH is
  # off (USE_PCH=OFF, or CMAKE_DISABLE_PRECOMPILE_HEADERS - the usual escape
  # from MSVC's PCH size limits), and until 2026-09-04 that was the only
  # path on any CMake >= 3.16: a Windows dynamic-modules build without PCH
  # died in WorldPosition.h / PlayerbotAI.h on exactly those shim names
  # ("GenericTransport: undeclared identifier", "m_banned: unknown override
  # specifier") while the same tree built on Linux with PCH. Force-include
  # the header instead whenever the PCH route is not taken.
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16" AND USE_PCH AND NOT CMAKE_DISABLE_PRECOMPILE_HEADERS)
    target_precompile_headers(${PB_TARGET} PRIVATE "${PB_ROOT}/botpch.h")
  elseif(MSVC)
    target_compile_options(${PB_TARGET} PRIVATE "/FI${PB_ROOT}/botpch.h")
  else()
    target_compile_options(${PB_TARGET} PRIVATE "-include${PB_ROOT}/botpch.h")
  endif()

  target_link_libraries(${PB_TARGET}
    PRIVATE Boost::thread
    PRIVATE Boost::filesystem
    PRIVATE Boost::system)

  # PUBLIC, not PRIVATE: mod-dungeon-clear includes "playerbot/playerbot.h" and
  # friends. It used to get these transitively through
  # target_link_libraries(modules PUBLIC playerbots); with both modules in one
  # target that link is gone, so the paths have to be on the target itself.
  #
  # Three roots, because the vendored sources use all three spellings:
  #   "playerbot/playerbot.h"       -> module root
  #   "PlayerbotMgr.h"              -> playerbot/
  #   "strategy/values/Foo.h"       -> playerbot/
  #
  # The core dirs below are NOT redundant with MODULES_COMMON_INCLUDES. They
  # used to reach the other module transitively: playerbots was its own library
  # and exported them PUBLIC, and mod-dungeon-clear picked them up through
  # target_link_libraries(modules PUBLIC playerbots). Merging both modules into
  # one target removed that link and with it the inheritance - the first build
  # after the move failed on mod-dungeon-clear (not on a bot source) with
  # "Config.h: No such file or directory". Recovered verbatim from the removed
  # root-CMakeLists block; duplicates with the common list are harmless.
  #
  # The core dirs are rooted at TW_CORE_ROOT for the same reason PB_ROOT is
  # CMAKE_CURRENT_LIST_DIR: CMAKE_SOURCE_DIR names the TOP-LEVEL project, which
  # is the platform's root as soon as the core is an add_subdirectory().
  target_include_directories(${PB_TARGET} PUBLIC
    ${PB_ROOT}
    ${PB_ROOT}/src
    ${PB_ROOT}/src/playerbot
    ${PB_ROOT}/src/ahbot
    ${PB_ROOT}/src/cmangos-compat-stubs
    ${TW_CORE_ROOT}/dep/include/g
    ${TW_CORE_ROOT}/src/framework
    ${TW_CORE_ROOT}/src/framework/Network
    ${TW_CORE_ROOT}/src/game
    ${TW_CORE_ROOT}/src/game/AI
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
    ${TW_CORE_ROOT}/src/game/MapNodes
    ${TW_CORE_ROOT}/src/game/Maps
    ${TW_CORE_ROOT}/src/game/Maps/Pool
    ${TW_CORE_ROOT}/src/game/Movement
    ${TW_CORE_ROOT}/src/game/Movement/spline
    ${TW_CORE_ROOT}/src/game/Objects
    ${TW_CORE_ROOT}/src/game/OutdoorPvP
    ${TW_CORE_ROOT}/src/game/PacketBroadcast
    ${TW_CORE_ROOT}/src/game/Protocol
    ${TW_CORE_ROOT}/src/game/Spells
    ${TW_CORE_ROOT}/src/game/Threat
    ${TW_CORE_ROOT}/src/game/Transports
    ${TW_CORE_ROOT}/src/game/vmap
    ${TW_CORE_ROOT}/src/shared
    ${TW_CORE_ROOT}/src/shared/Config
    ${TW_CORE_ROOT}/src/shared/Database
    ${TW_CORE_ROOT}/src/shared/Log
    ${TW_CORE_ROOT}/src/shared/Util)

  if(WIN32)
    target_include_directories(${PB_TARGET} PUBLIC
      ${TW_CORE_ROOT}/dep/windows/include
      ${TW_CORE_ROOT}/dep/windows/include/mysql)
  endif()
endforeach()

# Config file. Ported from the vendor CMakeLists (kept beside this one as
# CMakeLists.txt.vendor-reference): the expansion is chosen by project name and
# vanilla is the default, because this project is called TurtleWoW and matched
# none of the named cases - which is why aiplayerbot.conf.dist was never
# generated at all before that was fixed.
# PROJECT_NAME, not CMAKE_PROJECT_NAME: the latter is the TOP-LEVEL project's
# name, which is the consuming platform's under add_subdirectory(). Identical
# for a standalone core build.
if(${PROJECT_NAME} MATCHES "TBC")
  configure_file(${PB_ROOT}/src/playerbot/aiplayerbot.conf.dist.in.tbc
                 ${CMAKE_BINARY_DIR}/aiplayerbot.conf.dist)
elseif(${PROJECT_NAME} MATCHES "WoTLK")
  configure_file(${PB_ROOT}/src/playerbot/aiplayerbot.conf.dist.in.wotlk
                 ${CMAKE_BINARY_DIR}/aiplayerbot.conf.dist)
else()
  configure_file(${PB_ROOT}/src/playerbot/aiplayerbot.conf.dist.in
                 ${CMAKE_BINARY_DIR}/aiplayerbot.conf.dist)
endif()
if(NOT CONF_INSTALL_DIR)
  set(CONF_INSTALL_DIR ${CONF_DIR})
endif()
install(FILES ${CMAKE_BINARY_DIR}/aiplayerbot.conf.dist DESTINATION ${CONF_INSTALL_DIR})
