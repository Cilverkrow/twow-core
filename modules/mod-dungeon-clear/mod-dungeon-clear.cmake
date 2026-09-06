# MSVC: force _USE_MATH_DEFINES onto the compiler command line.
#
# MSVC's <math.h> only defines M_PI (and the rest of the M_* family) when
# _USE_MATH_DEFINES is defined BEFORE math.h is first included; it is #pragma
# once, so setting the macro later has no effect. AzerothCore sets it in
# src/common/Define.h, which works for core translation units because they reach
# Define.h before any standard math header — but a module TU that includes
# <cmath> (directly, or via <algorithm>/<vector>/G3D) before the first core
# header does not, and then every M_PI in that TU is undeclared. That breaks the
# CORE's own headers, not just ours: Position.h::NormalizeOrientation calls
#   std::fmod(o, 2.0f * static_cast<float>(M_PI))
# so the reported "error C2065: 'M_PI': undeclared identifier" is immediately
# followed by the cascade "error C2661: 'fmod': no overloaded function takes 1
# arguments" (the second argument failed to compile, so the call is seen with
# one). Nothing we can do inside our own sources fixes a core header — a
# command-line define is the only ordering-proof place for it.
#
# This file is included from modules/CMakeLists.txt AFTER the targets are
# created, so both linkage modes can be handled here. PRIVATE: it changes how
# these sources compile, nothing downstream. Our own sources additionally avoid
# M_PI entirely (DC_PI in Util/DungeonClearTuning.h), so this is only needed for
# the core headers we include.
# Spelled as a raw /D option rather than target_compile_definitions, and with a
# trailing '=', for one reason: src/common/Define.h line 38 ALSO does
#   #define _USE_MATH_DEFINES
# with an empty replacement list. A bare -D gives the macro the value 1, which is
# a NON-identical redefinition, and MSVC then emits
#   warning C4005: '_USE_MATH_DEFINES': macro redefinition
# once per translation unit — hundreds of lines of noise that bury real
# diagnostics. '/D_USE_MATH_DEFINES=' defines it EMPTY, identical to Define.h's,
# so the redefinition is legal and silent. target_compile_definitions cannot
# express this: CMake escapes "NAME=" into -DNAME="" (verified), which is a value
# of "" and warns just the same.
if (MSVC)
    foreach (DC_MATH_TARGET modules mod_mod-dungeon-clear)
        if (TARGET ${DC_MATH_TARGET})
            target_compile_options(${DC_MATH_TARGET} PRIVATE /D_USE_MATH_DEFINES=)
        endif()
    endforeach()
endif()

# The module's unit test suites are NOT declared here. They live in
# modules/mod-dungeon-clear/tests.cmake, included from the ROOT CMakeLists.txt
# under BUILD_TESTING -- this file is only read when the module's linkage is not
# "disabled", and it is read twice per configure (the DISCOVERY and POST_TARGETS
# phases in modules/CMakeLists.txt). See that file's header for the full reason,
# and for what the fast/full split buys.

# ---------------------------------------------------------------------------
# Tortoise port: reach the vendored playerbots tree.
#
# Upstream this module sits next to mod-playerbots, both of them AzerothCore
# modules compiled into the same `modules` library, so its includes resolve by
# themselves. That is now true here too - but the paths still have to be stated,
# and they have to be stated HERE rather than inherited.
#
# Why: mod-playerbots contributes its include paths only when BUILD_PLAYERBOTS
# is ON, and that option defaults to OFF. AcCompat.h below is force-included
# into every dungeon-clear translation unit and pulls cmangos-compat-shim.h out
# of the bot tree unconditionally, so a default-options build of THIS module
# needs the bot tree on its compile line whether or not the bots are built.
#
# These paths pointed at src/modules/PlayerBots until the bots moved into the
# module system on 2026-09-01, and the move did not update them. The directory
# had existed either way before, so the stale paths kept working for
# BUILD_PLAYERBOTS=OFF builds and the breakage only surfaced for someone
# building a fresh clone with default options:
#   AcCompat.h:72: fatal error: cmangos-compat-shim.h: No such file or directory
#
# The directory list mirrors what the bot module puts on its own compile line:
# its root, plus the three Penqle paths its headers reach through
# transitively. AcCompat.h - the AzerothCore-to-Penqle name and type shim -
# lives with the module and is force-included ahead of everything, because the
# names it maps appear in the upstream headers themselves, not only in code we
# could edit.
# ---------------------------------------------------------------------------

if(TORTOISE_MODULE_CMAKE_PHASE STREQUAL "POST_TARGETS")
  target_include_directories(modules
    PUBLIC
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/cmangos-compat-stubs
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/actions
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/triggers
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/values
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/generic
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/deathknight
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/druid
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/hunter
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/mage
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/paladin
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/priest
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/rogue
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/shaman
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/warlock
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/playerbot/strategy/warrior
      ${CMAKE_SOURCE_DIR}/modules/mod-playerbots/src/ahbot
      ${CMAKE_SOURCE_DIR}/src/game/MapNodes
      ${CMAKE_SOURCE_DIR}/src/framework/Network
      ${CMAKE_SOURCE_DIR}/dep/recastnavigation
      ${CMAKE_CURRENT_LIST_DIR}/src
      ${CMAKE_CURRENT_LIST_DIR}/src/compat)

  # playerbots used to be its own library; both modules share the `modules`
  # target now, so there is nothing left to link against.

  # AcCompat.h has to be forced ahead of every translation unit: NONE of this
  # module's 234 sources include it, and 47 of them use the LOG_* macros that
  # only exist there. The flag for that is compiler-specific, and getting it
  # wrong is silent - MSVC does not know `-include`, drops it with at most a
  # D9002, and every one of those 47 files then fails on undefined macros.
  # Reported from a Windows build on 2026-09-02 with both modules static.
  #
  # NOTE: this used to collide with mod-playerbots.cmake's own "/FI<botpch.h>"
  # on this same `modules` target - MSVC's project generator only keeps the
  # LAST /FI set via target_compile_options on a given target (confirmed via
  # the generated modules.vcxproj), so whichever of the two ran last silently
  # dropped the other module's prelude. Fixed properly upstream by giving
  # mod-playerbots its own `modules_playerbots` static library (see
  # modules/CMakeLists.txt), so there is only one /FI claim on `modules` again.
  if(MSVC)
    target_compile_options(modules PRIVATE
      "/FI${CMAKE_CURRENT_LIST_DIR}/src/AcCompat.h")
  else()
    target_compile_options(modules PRIVATE
      -include ${CMAKE_CURRENT_LIST_DIR}/src/AcCompat.h)
  endif()
endif()
