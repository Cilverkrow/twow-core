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

# 21 known call sites after 8.8 plus the definition and three comments (see #474).
set(allowed 25)
if (queue_calls GREATER allowed)
  message(FATAL_ERROR "New QueuePacket call in the bot module (${queue_calls} > ${allowed}): bot packets are never processed - call the handler directly (#474)")
endif()

file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
string(FIND "${ai_cpp}" "bot->GetSession()->HandleGameObjectUseOpcode(packet);" direct)
if (direct EQUAL -1)
  message(FATAL_ERROR "UseGameObjectDirect must call HandleGameObjectUseOpcode")
endif()
message(STATUS "BOT_QUEUEPACKET_CONTRACT=PASS queue_calls=${queue_calls}")
