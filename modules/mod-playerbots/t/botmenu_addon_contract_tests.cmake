if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "PB_MODULE_DIR is required")
endif()

# twow-repo#290: the BotMenu addon only writes chat commands; this keeps it
# honest against the server it talks to, and against the 1.12 Lua 5.0 client.
set(addon_dir "${PB_MODULE_DIR}/addon/BotMenu-1.12")
file(READ "${addon_dir}/BotMenu.lua" lua)
file(READ "${addon_dir}/BotMenu.toc" toc)
file(READ "${addon_dir}/BotMenu.xml" xml)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/triggers/ChatTriggerContext.h" triggers)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/values/Formations.cpp" formations)

# Commands and formations of PRs that are not merged yet. A name must leave
# this list as soon as the server has it - the test fails on a stale entry.
set(pending_commands "train;catchup quest")  # twow-core#145, twow-core#147
set(pending_formations "spear")              # twow-core#148

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

# 1.12 client.
require_text("${toc}" "## Interface: 11200" "1.12 interface version")
require_text("${toc}" "## SavedVariables: BotMenuDB" "saved switch")
require_text("${xml}" "inherits=\"UIMenuTemplate\"" "stock UIMenu template")
require_text("${xml}" "parent=\"ChatMenu\"" "hooked into the chat bubble menu")
foreach(forbidden ":match(" "string.match" "gmatch" ":SetSize(" "SendAddonMessage" "SendChatMessage")
  string(FIND "${lua}" "${forbidden}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "BotMenu.lua uses ${forbidden} (not on 1.12, or sends on its own)")
  endif()
endforeach()
# Lua 5.0 has no length operator; ignore '#' in comments and strings roughly
# by checking for its operator use patterns.
if(lua MATCHES "[=(,] *#[A-Za-z_]")
  message(FATAL_ERROR "BotMenu.lua uses the # length operator (Lua 5.1)")
endif()
if(lua MATCHES "[A-Za-z0-9_)] *% *[A-Za-z0-9_(]")
  message(FATAL_ERROR "BotMenu.lua uses the % operator (Lua 5.1)")
endif()

# Every menu command is a chat trigger the server registers.
string(REGEX MATCHALL "\\{ \"[^\"]+\", +\"[^\"]+\" \\}" entries "${lua}")
list(LENGTH entries entry_count)
if(entry_count LESS 30)
  message(FATAL_ERROR "Expected the full menu, found ${entry_count} entries")
endif()

foreach(entry IN LISTS entries)
  string(REGEX REPLACE "^\\{ \"[^\"]+\", +\"([^\"]+)\" \\}$" "\\1" command "${entry}")
  string(STRIP "${command}" command)

  # Longest registered prefix, like ExternalEventHelper::ParseChatCommand;
  # the trigger is looked up by its creators[] key (loot -> "add all loot").
  set(name "${command}")
  set(found "")
  while(NOT name STREQUAL "")
    string(FIND "${triggers}" "creators[\"${name}\"]" at)
    list(FIND pending_commands "${name}" pending_at)
    if(NOT at EQUAL -1 OR NOT pending_at EQUAL -1)
      set(found "${name}")
      break()
    endif()
    string(FIND "${name}" " " space REVERSE)
    if(space EQUAL -1)
      set(name "")
    else()
      string(SUBSTRING "${name}" 0 ${space} name)
    endif()
  endwhile()
  if(found STREQUAL "")
    message(FATAL_ERROR "Menu command '${command}' has no playerbot chat trigger")
  endif()

  if(command MATCHES "^formation (.+)$")
    set(value "${CMAKE_MATCH_1}")
    if(NOT value STREQUAL "?")
      string(FIND "${formations}" "formation == \"${value}\"" fat)
      list(FIND pending_formations "${value}" fpending)
      if(fat EQUAL -1 AND fpending EQUAL -1)
        message(FATAL_ERROR "Menu formation '${value}' is unknown to FormationValue::Load")
      endif()
    endif()
  endif()
endforeach()

# Stale pending entries: once merged, the name must leave the list.
foreach(name IN LISTS pending_commands)
  string(FIND "${triggers}" "creators[\"${name}\"]" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "'${name}' is registered now - remove it from pending_commands")
  endif()
endforeach()
foreach(value IN LISTS pending_formations)
  string(FIND "${formations}" "formation == \"${value}\"" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "'${value}' is a formation now - remove it from pending_formations")
  endif()
endforeach()

# Ten named formations in the menu (owner concept).
string(REGEX MATCHALL "\"formation [a-z]+\"" menu_formations "${lua}")
list(LENGTH menu_formations formation_count)
if(NOT formation_count EQUAL 10)
  message(FATAL_ERROR "Expected 10 formations in the menu, found ${formation_count}")
endif()

message(STATUS "BOTMENU_ADDON_CONTRACT=PASS entries=${entry_count}")
