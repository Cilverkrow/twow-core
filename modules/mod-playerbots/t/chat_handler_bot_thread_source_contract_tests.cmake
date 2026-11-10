# twow-repo#563 part 3 (HARM, OB-00 go 10.11.2026): PlayerbotAI::HandleCommand runs on the AsyncPackets thread, in parallel with the map threads.
# After parts 1 and 2 it still read the bot: the security check (LevelFor: group, master, gear score, area), the chat filter (strategies, group,
# values, equipment), wrote currentChat (read by TellPlayer on the bot thread) and filled the shared chatMap / unsecuredCommands lazily.
# Now HandleCommand only handles the text and queues one ChatCommandKind::Incoming; ProcessIncomingChat runs on the bot thread from
# HandleCommands. Carries a negative probe: the part-2 HandleCommand (security check and chat filter inside) must be rejected by the checker.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563 part 3 bot thread: missing ${description}: ${needle}")
  endif()
endfunction()

function(slice text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" b)
  string(FIND "${text}" "${end_marker}" e)
  if(b EQUAL -1 OR e EQUAL -1 OR NOT b LESS e)
    message(FATAL_ERROR "#563 part 3 bot thread: cannot find ${begin_marker}")
  endif()
  math(EXPR len "${e} - ${b}")
  string(SUBSTRING "${text}" ${b} ${len} piece)
  set(${out_var} "${piece}" PARENT_SCOPE)
endfunction()

# HandleCommand is "string only" when none of the bot-reading constructs appears in it.
function(handler_is_string_only text out_var)
  set(ok TRUE)
  foreach(banned "GetSecurity()" "CheckLevelFor(" "chatFilter" "currentChat" "chatMap" "unsecuredCommands" "HandleRemoteCommand"
                 "bot->" "SendPacket(" "TellPlayer(" "GetGroup()" "master")
    string(FIND "${text}" "${banned}" pos)
    if(NOT pos EQUAL -1)
      set(ok FALSE)
    endif()
  endforeach()
  set(${out_var} ${ok} PARENT_SCOPE)
endfunction()

# --- negative probe: the part-2 handler (security check and chat filter inside) must be rejected, the new one accepted ---
handler_is_string_only("    if (!IsAllowedCommand(filtered) && !GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE, type != CHAT_MSG_WHISPER, &fromPlayer))\n        return;\n    filtered = chatFilter.Filter(trim(filtered));\n    currentChat = std::pair<ChatMsg, time_t>(CHAT_MSG_WHISPER, 0);" probe_old)
if(probe_old)
  message(FATAL_ERROR "#563 part 3 bot thread: negative probe failed - the part-2 handler was accepted")
endif()
handler_is_string_only("    std::string filtered = text;\n    bool const inviteCheckDone = IsAllowedCommand(filtered);\n    QueueChatCommand(incoming);" probe_new)
if(NOT probe_new)
  message(FATAL_ERROR "#563 part 3 bot thread: positive probe failed - the string-only handler was rejected")
endif()

read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)

slice("${ai_cpp}" "void PlayerbotAI::HandleCommand(uint32 type" "void PlayerbotAI::ProcessIncomingChat(" handler)
slice("${ai_cpp}" "void PlayerbotAI::ProcessIncomingChat(" "void PlayerbotAI::HandleBotOutgoingPacket" process)
slice("${ai_cpp}" "void PlayerbotAI::HandleCommands()" "void PlayerbotAI::QueueChatCommand" drain)
slice("${ai_cpp}" "void PlayerbotAI::QueueChatCommand" "void PlayerbotAI::UpdateAIInternal" queue_fn)

handler_is_string_only("${handler}" handler_ok)
if(NOT handler_ok)
  message(FATAL_ERROR "#563 part 3 bot thread: HandleCommand (AsyncPackets thread) must only handle the text - no security check, chat filter, currentChat, remote command, bot access")
endif()
require_text("${handler}" "ChatCommandKind::Incoming" "HandleCommand queues the raw chat")
require_text("${handler}" "IsAllowedCommand(filtered)" "pure string test of the unsecured commands")

# ProcessIncomingChat (bot thread): the moved reads
require_text("${process}" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE" "INVITE security check")
require_text("${process}" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL" "ALLOW_ALL security check")
require_text("${process}" "chatFilter.Filter(text)" "chat filter")
require_text("${process}" "HandleRemoteCommand(filtered.substr(6))" "remote diagnostic command")
require_text("${process}" "ChatCommandKind::DoAction" "dispatch keeps the directive kinds")
string(FIND "${process}" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE" inv_pos)
string(FIND "${process}" "chatFilter.Filter(text)" flt_pos)
string(FIND "${process}" "CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL" all_pos)
if(NOT inv_pos LESS flt_pos OR NOT flt_pos LESS all_pos)
  message(FATAL_ERROR "#563 part 3 bot thread: order must stay INVITE check, chat filter, ALLOW_ALL check")
endif()
string(FIND "${process}" "QueueChatCommand(" raw_queue)
string(FIND "${process}" "holder.SetReplyChannel(replyChannel);" set_channel)
if(NOT raw_queue EQUAL -1 AND raw_queue LESS set_channel)
  message(FATAL_ERROR "#563 part 3 bot thread: commands queued from ProcessIncomingChat must carry the reply channel (enqueue lambda)")
endif()

# HandleCommands (bot thread): runs it and owns the currentChat write
require_text("${drain}" "case ChatCommandKind::Incoming:" "drain: Incoming")
require_text("${drain}" "ProcessIncomingChat(holder.GetType(), holder.GetCommand(), *directiveOwner" "drain: ProcessIncomingChat")
require_text("${drain}" "applyReplyChannel(holder);" "drain: reply channel applied")
require_text("${drain}" "walked.SetReplyChannel(holder.GetReplyChannel());" "drain: delayed queue command keeps the reply channel")
require_text("${drain}" "[ChatCmd] owner on another Map object" "measurement of owners on another Map object")

# the raw chat queue is bounded
require_text("${queue_fn}" "cmd.GetKind() == ChatCommandKind::Incoming && chatCommands.size() >= 256" "bounded raw chat queue")

# no currentChat write outside the bot-thread functions
string(REGEX MATCHALL "currentChat = " writes "${ai_cpp}")
list(LENGTH writes n_writes)
if(NOT n_writes EQUAL 2)
  message(FATAL_ERROR "#563 part 3 bot thread: expected 2 currentChat writes (ProcessIncomingChat and the drain), found ${n_writes}")
endif()

# the lazily filled shared tables are gone
foreach(banned "chatMap" "unsecuredCommands")
  string(FIND "${ai_cpp}" "${banned}" p1)
  string(FIND "${ai_h}" "${banned}" p2)
  if(NOT p1 EQUAL -1 OR NOT p2 EQUAL -1)
    message(FATAL_ERROR "#563 part 3 bot thread: ${banned} (lazily filled shared table) must not come back")
  endif()
endforeach()
require_text("${ai_cpp}" "ChatChannelPrefix const kChatChannelPrefixes[]" "constant channel prefix table")
require_text("${ai_cpp}" "char const* const kUnsecuredCommands[]" "constant unsecured command table")

# the holder carries the reply channel and the copy keeps it
require_text("${ai_h}" "this->replyChannel = other.replyChannel;" "copy constructor copies the reply channel")
require_text("${ai_h}" "    Incoming," "Incoming kind")
