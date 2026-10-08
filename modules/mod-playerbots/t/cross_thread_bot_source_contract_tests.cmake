# twow-repo#563 (phase 1, OB-00 go 09.10.2026): a bot's AI state is only changed on the bot's own
# thread. X1: WorldSession::SendPacket into a bot session (the CanPacketSend hook, sender's thread)
# only queues the packet; UpdateAI handles the queue first thing, before the park check.
# X2: ListSpellsAction fills its shared static tables once (std::call_once) and only reads them.
# Inbox logic: t/bot_packet_inbox_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#563: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#563: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#563: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotScripts.cpp" scripts)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("BotPacketInbox.h" inbox_h)
read_source("strategy/actions/ListSpellsAction.cpp" list_spells)

# X1: the hook only queues.
string(FIND "${scripts}" "bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override" hook_at)
if(hook_at EQUAL -1)
  message(FATAL_ERROR "#563: CanPacketSend not found")
endif()
string(SUBSTRING "${scripts}" ${hook_at} 900 hook)
require_text("${hook}" "ai->QueueBotOutgoingPacket(packet);" "hook queues the packet")
forbid_text("${hook}" "ai->HandleBotOutgoingPacket(packet);" "the hook must not handle the packet on the sender's thread")

# The inbox is locked and bounded.
require_text("${inbox_h}" "std::lock_guard<std::mutex> lock(mutex);" "inbox lock")
require_text("${inbox_h}" "if (queue.size() >= capacity)" "inbox bound")
require_text("${ai_h}" "ai::BoundedInbox<std::unique_ptr<WorldPacket>> botPacketInbox{ ai::BotPacketInboxCapacity };" "inbox member")

function_body("${ai_cpp}" "void PlayerbotAI::QueueBotOutgoingPacket(const WorldPacket& packet)" queue_body)
require_text("${queue_body}" "botPacketInbox.Push(std::make_unique<WorldPacket>(packet));" "queue copies the packet")
forbid_text("${queue_body}" "HandleBotOutgoingPacket" "queueing must not handle")

function_body("${ai_cpp}" "void PlayerbotAI::HandleQueuedBotPackets()" drain_body)
require_order("${drain_body}" "botPacketInbox.Drain(packets);" "HandleBotOutgoingPacket(*packet);" "drain, then handle")

# The drain is the first thing in UpdateAI, before the park check.
function_body("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" update_body)
require_order("${update_body}" "HandleQueuedBotPackets();" "if (parked)" "drain before the park check")

# HandleBotOutgoingPacket has exactly one caller: the drain.
string(REGEX MATCHALL "HandleBotOutgoingPacket\\(\\*packet\\)|ai->HandleBotOutgoingPacket\\(" callers "${ai_cpp}${scripts}")
list(LENGTH callers caller_count)
if(NOT caller_count EQUAL 1)
  message(FATAL_ERROR "#563: HandleBotOutgoingPacket must only be called from HandleQueuedBotPackets (found ${caller_count})")
endif()

# Drops are counted server-wide and reported once per minute from the world-thread pass.
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
require_order("${inbox_h}" "queue.pop_front();" "InboxDroppedTotal().fetch_add(1, std::memory_order_relaxed);" "drop counter")
require_text("${rnd_mgr}" "\"[BotInbox] dropped=%llu largest_batch=%llu capacity=%u\"" "minute line")
string(FIND "${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_at)
string(SUBSTRING "${rnd_mgr}" ${upd_at} 1200 upd_head)
require_order("${upd_head}" "\n    ProcessParkedBots();" "\n    ReportBotInbox();" "minute line in the world-thread pass")

# X2: shared tables filled once, read with find.
require_text("${list_spells}" "static std::once_flag listSpellsTablesOnce;" "once flag")
require_order("${list_spells}" "std::call_once(listSpellsTablesOnce" "skillSpells[skillLine->spellId] = skillLine;" "table fill inside call_once")
require_order("${list_spells}" "std::call_once(listSpellsTablesOnce" "vendorItems.insert(" "vendor fill inside call_once")
forbid_text("${list_spells}" "if (skillSpells.empty())" "lazy unlocked fill")
forbid_text("${list_spells}" "if (vendorItems.empty())" "lazy unlocked fill")
forbid_text("${list_spells}" "= skillSpells[spellId];" "operator[] on the shared table")

message(STATUS "cross_thread_bot source contract passed")
