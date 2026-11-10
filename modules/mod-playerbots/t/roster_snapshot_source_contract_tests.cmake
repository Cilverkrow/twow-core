# twow-repo#698 (found while fixing #697): BuildRosterListEntries runs in the issuing player's command thread, not on a bot's map thread. It must not
# read a bot's strategy maps (IsTankFor/IsHealFor -> ContainsStrategy) or its Group (IsRaidGroup) there: the bot's own thread changes both. The
# bot publishes role and group state as one atomic word from its OWN thread (PublishRosterSnapshot in UpdateAIInternal); the list only loads it.
# Carries a negative probe: the old function body (direct strategy/group reads) must be rejected by the same checker.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#698 roster snapshot: missing ${description}: ${needle}")
  endif()
endfunction()

function(slice text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" b)
  string(FIND "${text}" "${end_marker}" e)
  if(b EQUAL -1 OR e EQUAL -1 OR NOT b LESS e)
    message(FATAL_ERROR "#698 roster snapshot: cannot find ${begin_marker}")
  endif()
  math(EXPR len "${e} - ${b}")
  string(SUBSTRING "${text}" ${b} ${len} piece)
  set(${out_var} "${piece}" PARENT_SCOPE)
endfunction()

# The list builder is safe when it reads neither the strategy maps nor the Group of a bot.
function(builder_reads_no_bot_containers text out_var)
  set(ok TRUE)
  foreach(banned "IsTankFor(" "IsHealFor(" "IsTank(" "IsHeal(" "ContainsStrategy(" "HasStrategy(" "->GetGroup()" "IsRaidGroup(")
    string(FIND "${text}" "${banned}" pos)
    if(NOT pos EQUAL -1)
      set(ok FALSE)
    endif()
  endforeach()
  set(${out_var} ${ok} PARENT_SCOPE)
endfunction()

# --- negative probe ---
builder_reads_no_bot_containers("e.role = PlayerbotAI::IsTankFor(bot, bot) ? Role::TANK : Role::DPS;\nif (Group* group = bot->GetGroup())\n    e.group = group->IsRaidGroup() ? RAID : GROUP;" probe_old)
if(probe_old)
  message(FATAL_ERROR "#698 roster snapshot: negative probe failed - the direct strategy/group reads were accepted")
endif()
builder_reads_no_bot_containers("Unpacked const snap = ai::roster_snapshot::Unpack(GetBotAI(bot)->GetRosterSnapshot());" probe_new)
if(NOT probe_new)
  message(FATAL_ERROR "#698 roster snapshot: positive probe failed - the snapshot read was rejected")
endif()

read_source("PlayerbotMgr.cpp" mgr)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)

slice("${mgr}" "void BuildRosterListEntries(" "std::list<std::string> PlayerbotHolder::HandleRoster(" builder)
builder_reads_no_bot_containers("${builder}" builder_ok)
if(NOT builder_ok)
  message(FATAL_ERROR "#698 roster snapshot: BuildRosterListEntries (command thread) must not read a bot's strategy maps or Group - use GetRosterSnapshot()")
endif()
require_text("${builder}" "ai::roster_snapshot::Unpack(GetBotAI(bot)->GetRosterSnapshot())" "snapshot read")

# the snapshot is written only by the bot's own thread: one store, in PublishRosterSnapshot, called from UpdateAIInternal
string(REGEX MATCHALL "rosterSnapshot\\.store\\(" stores "${ai_cpp}")
list(LENGTH stores n_stores)
if(NOT n_stores EQUAL 1)
  message(FATAL_ERROR "#698 roster snapshot: expected exactly 1 rosterSnapshot.store (PublishRosterSnapshot), found ${n_stores}")
endif()
slice("${ai_cpp}" "void PlayerbotAI::PublishRosterSnapshot()" "void PlayerbotAI::UpdateAIInternal(" publish)
require_text("${publish}" "PlayerbotAI::IsTankFor(bot, bot)" "role from the bot's own strategies, on its own thread")
require_text("${publish}" "rosterSnapshot.store(ai::roster_snapshot::Pack(role, group), std::memory_order_relaxed);" "single atomic store")
string(FIND "${ai_cpp}" "PublishRosterSnapshot();   // twow-repo#698" call_pos)
string(FIND "${ai_cpp}" "void PlayerbotAI::UpdateAIInternal(" update_pos)
if(call_pos EQUAL -1 OR update_pos EQUAL -1 OR NOT update_pos LESS call_pos)
  message(FATAL_ERROR "#698 roster snapshot: PublishRosterSnapshot must be called from UpdateAIInternal")
endif()

require_text("${ai_h}" "std::atomic<uint32> rosterSnapshot{0};" "atomic snapshot word")
require_text("${ai_h}" "uint32 GetRosterSnapshot() const { return rosterSnapshot.load(std::memory_order_relaxed); }" "reader is one atomic load")
