# Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
# Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

set(EXECUTABLE_NAME mangosd)
set (EXECUTABLE_SRCS 
	CliRunnable.h
	DynamicModules.h
	Master.h
	WorldRunnable.h
	CliRunnable.cpp
	DynamicModules.cpp
	Main.cpp
	Master.cpp
	WorldRunnable.cpp

)


# With nothing attached to the extension seam, AddModulesScripts() -- which
# DynamicModules.cpp calls unconditionally at startup -- has no definition
# anywhere and mangosd does not link. This supplies an empty one. A host that
# generates a real loader sets TW_EXTERNAL_MODULE_LOADER and the two do not
# collide.
if(NOT TW_EXTERNAL_MODULE_LOADER)
  list(APPEND EXECUTABLE_SRCS
    ModulesScriptLoaderStub.cpp
  )
endif()

if(WIN32)
  list(APPEND EXECUTABLE_SRCS
    mangosd.rc
  )
  
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D__ACE_INLINE__")
endif()

include_directories(
  ${TW_CORE_ROOT}/src/shared
  ${TW_CORE_ROOT}/dep/include/g3dlite
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
  ${TW_CORE_ROOT}/src/game/MapNodes
  ${TW_CORE_ROOT}/src/game/Maps
  ${TW_CORE_ROOT}/src/game/Maps/Pool
  ${TW_CORE_ROOT}/src/game/Movement
  ${TW_CORE_ROOT}/src/game/Movement/spline
  ${TW_CORE_ROOT}/src/game/Objects
  ${TW_CORE_ROOT}/src/game/OutdoorPvP
  # PlayerBots include dir removed - Penqle stub binned
  ${TW_CORE_ROOT}/src/game/Protocol
  ${TW_CORE_ROOT}/src/game/Spells
  ${TW_CORE_ROOT}/src/game/Threat
  ${TW_CORE_ROOT}/src/game/Transports
  ${TW_CORE_ROOT}/src/game/vmap
  ${TW_CORE_BINARY_DIR}/src/shared
  ${TW_CORE_BINARY_DIR}
  ${ACE_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIR}
  ${OPENSSL_INCLUDE_DIR}
)

if(WIN32)
  include_directories(
    ${TW_CORE_ROOT}/dep/include-windows
  )
endif()

if (ENABLE_PROFILING)
    include_directories(
        ${OPTICK_INCLUDE_DIR}
    )
endif()

# Display some debug informations at server startup
set(DEFINITIONS
  ${DEFINITIONS}
  MANGOS_ALLOC_LIB="${ALLOC_LIB_INFO_STRING}"
)
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${DEFINITIONS}")

add_executable(${EXECUTABLE_NAME}
  ${EXECUTABLE_SRCS}
)

if(USE_SCRIPTS)
  target_link_libraries(${EXECUTABLE_NAME}
    game
    scripts
    shared
    framework
    g3dlite
    ${ACE_LIBRARIES}
  )
else()
  target_link_libraries(${EXECUTABLE_NAME}
    game
    #scripts
    shared
    framework
    g3dlite 
    ${ACE_LIBRARIES}
  )
endif()

if(USE_DISCORD_BOT)
    target_link_libraries(${EXECUTABLE_NAME}
	dpp
  )
endif()

# The bot library is not named here any more. It is modules/mod-playerbots, and
# the root CMakeLists links `modules` into this executable, which carries every
# enabled module's target with it.

target_link_libraries(${EXECUTABLE_NAME}
    libdeflate
)

# The Windows Boost link-directory hunt that used to sit here has moved to
# modules/mod-playerbots/mod-playerbots.cmake in twow-repo. It was here only
# because the unresolved Boost references arrive at this executable's link line,
# but the reason they arrive is that one module drags them in -- and asking
# `GetModuleEffectiveLinkage("mod-playerbots")` from the core meant the core
# could not configure without the module framework loaded. The module puts the
# directory on its own target now, and it reaches this link line through the
# link chain instead.


if(WIN32)
  target_link_libraries(${EXECUTABLE_NAME}
    zlib
    optimized ${MYSQL_LIBRARY}
    optimized ${OPENSSL_LIBRARIES}
    debug ${MYSQL_DEBUG_LIBRARY}
    debug ${OPENSSL_DEBUG_LIBRARIES}
  )
  if(MINGW)
    target_link_libraries(${EXECUTABLE_NAME}
      -lws2_32
    )
  endif()

  if (ENABLE_PROFILING)
      target_link_libraries(${EXECUTABLE_NAME}
        ${OPTICK_LIBRARY})
  endif()

  if(PLATFORM MATCHES X86)
    target_link_libraries(${EXECUTABLE_NAME})
  endif()
endif()

if(UNIX)
  target_link_libraries(${EXECUTABLE_NAME}
    ${MYSQL_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${OPENSSL_EXTRA_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${CMAKE_DL_LIBS}
  )
endif()

set(EXECUTABLE_LINK_FLAGS "")

if(UNIX)
  set(EXECUTABLE_LINK_FLAGS "-pthread ${EXECUTABLE_LINK_FLAGS} -rdynamic")
endif()

if(APPLE)
  set(EXECUTABLE_LINK_FLAGS "-framework Carbon ${EXECUTABLE_LINK_FLAGS}")
endif()

set_target_properties(${EXECUTABLE_NAME} PROPERTIES LINK_FLAGS
  "${EXECUTABLE_LINK_FLAGS}"
)

install(TARGETS ${EXECUTABLE_NAME} DESTINATION ${BIN_DIR})
install(FILES run-mangosd DESTINATION ${BIN_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mangosd.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/mangosd.conf.dist)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mangosd.conf.dist DESTINATION ${CONF_DIR})

if(WIN32 AND MSVC)
  # Every configuration, not just Debug: a Release minidump needs the symbols
  # just as much, and RelWithDebInfo produced a .pdb that never got installed.
  install(FILES $<TARGET_PDB_FILE:${EXECUTABLE_NAME}> DESTINATION ${BIN_DIR} OPTIONAL)
endif()
