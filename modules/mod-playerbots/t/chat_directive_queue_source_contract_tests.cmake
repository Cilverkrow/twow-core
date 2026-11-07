# twow-repo#563 (HARM, OB-10 verdict 07.11.2026): PlayerbotAI::HandleCommand runs on the AsyncPackets thread (a player's chat,
# PACKET_PROCESS_DB_QUERY), in parallel with the map threads. It must not act on the bot's engine, values or strategies itself:
# "d " / "do " / command:do, "reset" and "logout cancel" go into the per-bot queue (chatCommandsMutex) and HandleCommands runs them on the
# bot's own thread, in queue order. This contract pins "no direct DoSpecificAction/Reset from HandleCommand".

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563 chat directive queue: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#563 chat directive queue: ${description} must not appear in the off-thread function: ${needle}")
  endif()
endfunction()

function(slice text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" b)
  string(FIND "${text}" "${end_marker}" e)
  if(b EQUAL -1 OR e EQUAL -1 OR NOT b LESS e)
    message(FATAL_ERROR "#563 chat directive queue: cannot find function ${begin_marker}")
  endif()
  math(EXPR len "${e} - ${b}")
  string(SUBSTRING "${text}" ${b} ${len} piece)
  set(${out_var} "${piece}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)

slice("${ai_cpp}" "void PlayerbotAI::HandleCommand(uint32 type" "void PlayerbotAI::HandleBotOutgoingPacket" on_thread_off)
slice("${ai_cpp}" "void PlayerbotAI::HandleCommands()" "void PlayerbotAI::QueueChatCommand" drain)

# HandleCommand (AsyncPackets thread): nothing that touches the bot's engine directly.
forbid_text("${on_thread_off}" "DoSpecificAction(" "DoSpecificAction")
forbid_text("${on_thread_off}" "Reset(true)" "Reset(true)")
forbid_text("${on_thread_off}" "HandleLogoutCancelOpcode" "HandleLogoutCancelOpcode")
forbid_text("${on_thread_off}" "SetShouldLogOut(false)" "SetShouldLogOut(false)")

# ... they are queued instead, once per branch (the "d "/"do " and command:do branches share the kind).
require_text("${on_thread_off}" "ChatCommandKind::DoAction" "DoAction directive")
require_text("${on_thread_off}" "ChatCommandKind::Reset" "Reset directive")
require_text("${on_thread_off}" "ChatCommandKind::LogoutCancel" "LogoutCancel directive")
string(REGEX MATCHALL "ChatCommandKind::DoAction" do_hits "${on_thread_off}")
list(LENGTH do_hits do_count)
if(NOT do_count EQUAL 2)
  message(FATAL_ERROR "#563 chat directive queue: expected 2 DoAction queue sites (d/do and command:do), found ${do_count}")
endif()

# HandleCommands (bot thread, drained from the reaction engine) executes them.
require_text("${drain}" "ChatCommandKind::DoAction" "drain: DoAction")
require_text("${drain}" "DoSpecificAction(holder.GetCommand(), event)" "drain: DoSpecificAction")
require_text("${drain}" "Reset(true);" "drain: Reset")
require_text("${drain}" "HandleLogoutCancelOpcode(p);" "drain: logout cancel")
# the directives are handled before the delay check, so queue order is kept, and each command is taken under the lock one at a time
string(FIND "${drain}" "ChatCommandKind::Chat" dir_pos)
string(FIND "${drain}" "time_t checkTime = holder.GetTime();" delay_pos)
if(dir_pos EQUAL -1 OR delay_pos EQUAL -1 OR NOT dir_pos LESS delay_pos)
  message(FATAL_ERROR "#563 chat directive queue: directive dispatch must come before the delayed-command check")
endif()
require_text("${drain}" "std::scoped_lock lock(chatCommandsMutex);" "drain: queue taken under chatCommandsMutex")

# the kind is part of the holder and survives the copy
require_text("${ai_h}" "enum class ChatCommandKind" "kind enum")
require_text("${ai_h}" "this->kind = other.kind;" "kind copied by the copy constructor")
