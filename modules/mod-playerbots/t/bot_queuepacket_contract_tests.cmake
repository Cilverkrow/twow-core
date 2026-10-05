# Hotfix 8.8 (twow-repo#474): bot sessions have no socket - WorldSession::CanProcessPackets()
# is false for them and packets queued with QueuePacket are never processed. Game objects
# are used through PlayerbotAI::UseGameObjectDirect; every other QueuePacket call site is on
# an allowlist (count) until 8.9 has checked it. A new call site fails here on purpose:
# call the opcode handler directly instead.
file(GLOB_RECURSE sources "${PB_SOURCE_DIR}/*.cpp")
set(queue_calls 0)
foreach (source ${sources})
  file(READ "${source}" text)
  string(FIND "${text}" "new WorldPacket(CMSG_GAMEOBJ_USE)" gouse)
  if (NOT gouse EQUAL -1)
    message(FATAL_ERROR "${source}: game objects must be used through UseGameObjectDirect, not a queued packet (#474)")
  endif()
  string(REGEX MATCHALL "QueuePacket\\(" calls "${text}")
  list(LENGTH calls n)
  math(EXPR queue_calls "${queue_calls} + ${n}")
endforeach()

# 8.9: CMSG_USE_ITEM (ImbueItem), CMSG_OPEN_ITEM and bot chat (behind AiPlayerbot.BotChat.Direct,
# default off) call their handlers directly. Left on the allowlist on purpose (#474): the
# console admin paths (sent as the real master), jump-landing MSG_MOVE_*, the non-instant
# logout, CMSG_SOCKET_GEMS (no effect here), the MANGOS-only branch and comments.
set(allowed 19)
if (queue_calls GREATER allowed)
  message(FATAL_ERROR "New QueuePacket call in the bot module (${queue_calls} > ${allowed}): bot packets are never processed - call the handler directly (#474)")
endif()

file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
string(FIND "${ai_cpp}" "bot->GetSession()->HandleGameObjectUseOpcode(packet);" direct)
if (direct EQUAL -1)
  message(FATAL_ERROR "UseGameObjectDirect must call HandleGameObjectUseOpcode")
endif()
string(REGEX MATCHALL "if \\(sPlayerbotAIConfig\\.botChatDirect\\)" chat_switches "${ai_cpp}")
list(LENGTH chat_switches chat_count)
if (NOT chat_count EQUAL 4)
  message(FATAL_ERROR "Say/Yell/Party/Guild must send through the chat handler behind BotChat.Direct (found ${chat_count}) (#474)")
endif()
foreach (needle "bot->GetSession()->HandleUseItemOpcode(*packet);" )
  string(FIND "${ai_cpp}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "ImbueItem must use the item through HandleUseItemOpcode (#474)")
  endif()
endforeach()
file(READ "${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" use_item)
string(FIND "${use_item}" "bot->GetSession()->HandleOpenItemOpcode(*packet);" open_item)
if (open_item EQUAL -1)
  message(FATAL_ERROR "OpenItem must open the item through HandleOpenItemOpcode (#474)")
endif()
string(FIND "${ai_cpp}" "[ItemUse] bot=%u level=%u uses=%u" item_use_line)
if (item_use_line EQUAL -1)
  message(FATAL_ERROR "[ItemUse] line missing (#474 acceptance)")
endif()
# Hotfix 8.11: an opened container's loot is taken whole (no reopen loop). Only the tail of the
# condition is pinned: #485 (core#279) put the skin loot exceptions in front of it.
file(READ "${PB_SOURCE_DIR}/strategy/actions/LootAction.cpp" loot_cpp)
string(FIND "${loot_cpp}" "!guid.IsItem() && !IsLootAllowed(itemQualifier, ai))" own_item)
if (own_item EQUAL -1)
  message(FATAL_ERROR "StoreLoot must take an opened container's loot whole (#474)")
endif()
message(STATUS "BOT_QUEUEPACKET_CONTRACT=PASS queue_calls=${queue_calls}")
