# twow-repo#563 phase 2 (X3a + X3c, OB-00 go 10.10.2026; switches default 0).
# X3a: every bot's value map (NamedObjectContext::created) is guarded by its own shared_mutex when
#   AiPlayerbot.X3a.ContextLock is on - and the lock covers ONLY the map access. Factories, Update/Reset,
#   the eviction predicate and delete run outside it, so no thread ever holds two context locks: there is
#   no lock order between two bots' contexts, hence no deadlock or inversion.
# X3c: value writes into another bot (talk target, last said, RTSC) are queued (AiPlayerbot.X3c.InboxWrites)
#   and applied by that bot in its own UpdateAI; dropped and counted when the target has no AI.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563 X3: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#563 X3: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#563 X3: region not found: '${first}' .. '${second}'")
  endif()
  math(EXPR len "${b} - ${a}")
  string(SUBSTRING "${text}" ${a} ${len} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("strategy/NamedObjectContext.h" named)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("strategy/actions/EmoteAction.cpp" emote)
read_source("strategy/actions/SayAction.cpp" say)
read_source("strategy/actions/RtscAction.cpp" rtsc)

# Switches: default off, documented.
require_text("${named}" "static std::atomic<bool> enabled{ false };" "lock switch default off")
require_text("${config_cpp}" "ai::context_lock::Enabled().store(config.GetBoolDefault(\"AiPlayerbot.X3a.ContextLock\", false)" "X3a config default 0")
require_text("${config_h}" "bool x3cInboxWrites = false;" "X3c member default off")
require_text("${config_cpp}" "x3cInboxWrites = config.GetBoolDefault(\"AiPlayerbot.X3c.InboxWrites\", false);" "X3c config default 0")
require_text("${conf_dist}" "AiPlayerbot.X3a.ContextLock = 0" "documented X3a")
require_text("${conf_dist}" "AiPlayerbot.X3c.InboxWrites = 0" "documented X3c")
require_text("${named}" "mutable std::shared_mutex createdMutex;" "per-context mutex")

# X3a Create: shared lookup, factory OUTSIDE any lock, unique insert, delete of a lost race outside.
between("${named}" "T* Create(std::string name, PlayerbotAI* ai)" "virtual ~NamedObjectContext()" create)
require_order("${create}" "context_lock::Shared const lock(createdMutex);" "T* fresh = NamedObjectFactory<T>::Create(name, ai);   // outside the lock" "lookup before the factory")
between("${create}" "context_lock::Shared const lock(createdMutex);" "T* fresh = NamedObjectFactory<T>::Create(name, ai);" shared_block)
string(FIND "${shared_block}" "NamedObjectFactory<T>::Create" factory_in_shared)
if(NOT factory_in_shared EQUAL -1)
  message(FATAL_ERROR "#563 X3: the factory must not run under the shared lock")
endif()
require_order("${create}" "T* fresh = NamedObjectFactory<T>::Create(name, ai);" "context_lock::Unique const lock(createdMutex);" "factory before the unique lock")
between("${create}" "context_lock::Unique const lock(createdMutex);" "if (!inserted && fresh)" unique_block)
string(FIND "${unique_block}" "delete " delete_in_unique)
string(FIND "${unique_block}" "Factory<T>::Create" factory_in_unique)
if(NOT delete_in_unique EQUAL -1 OR NOT factory_in_unique EQUAL -1)
  message(FATAL_ERROR "#563 X3: no factory and no delete under the unique lock")
endif()

# Erase / Clear: delete after the lock block.
between("${named}" "void Erase(const std::string& name)" "void Update()" erase)
require_order("${erase}" "created.erase(it);" "delete doomed;" "erase, then delete")
between("${erase}" "context_lock::Unique const lock(" "delete doomed;" erase_locked)
string(FIND "${erase_locked}" "\n            }\n" erase_block_end)
if(erase_block_end EQUAL -1)
  message(FATAL_ERROR "#563 X3: Erase must delete after the locked block")
endif()
between("${named}" "void Clear()" "void Erase(const std::string& name)" clear)
require_order("${clear}" "old.swap(created);" "delete i->second;" "swap under the lock, delete after")

# Update/Reset (value code) run on a snapshot when locked; the predicate of EraseIf runs between the locks.
require_text("${named}" "for (T* object : Snapshot())\n                object->Update();" "Update outside the lock")
require_text("${named}" "for (T* object : Snapshot())\n                object->Reset();" "Reset outside the lock")
between("${named}" "std::vector<std::pair<std::string, T*>> candidates;" "return doomed.size();" erase_if)
require_order("${erase_if}" "context_lock::Shared const lock(createdMutex);" "if (pred(candidate.second))" "candidates, then the predicate")
require_order("${erase_if}" "if (pred(candidate.second))" "context_lock::Unique const lock(createdMutex);" "predicate before the unique lock")
require_order("${erase_if}" "context_lock::Unique const lock(createdMutex);" "for (T* object : doomed)\n                delete object;" "delete after the unique lock")

# X3c inbox: applied in the drain on the owner's thread; target without AI counted.
require_text("${ai_h}" "ai::BoundedInbox<std::function<void(ai::AiObjectContext*)>> contextWriteInbox{ ai::ContextWriteInboxCapacity };" "write inbox")
between("${ai_cpp}" "void PlayerbotAI::HandleQueuedBotPackets()" "void PlayerbotAI::QueueContextWrite(" drain)
require_order("${drain}" "contextWriteInbox.Drain(writes);" "write(aiObjectContext);" "drain, then apply on the own context")
between("${ai_cpp}" "void PlayerbotAI::QueueContextWriteTo(" "void PlayerbotAI::UpdateAI(" write_to)
require_order("${write_to}" "if (!targetAi)" "ai::ContextWriteTargetGone().fetch_add(1, std::memory_order_relaxed);" "gone target counted")
require_text("${ai_cpp}" "ai::InboxDroppedByClass(ai::InboxDropWrite).fetch_add(1, std::memory_order_relaxed);" "bound drop counted")
require_text("${rnd_mgr}" "drop_write=%llu write_target_gone=%llu" "minute line fields")

# The three write sites: queued with the switch, the old direct write only in the else branch.
require_order("${emote}" "if (sPlayerbotAIConfig.x3cInboxWrites)" "PlayerbotAI::QueueContextWriteTo(player, [talker](AiObjectContext* other)" "talk target queued")
require_order("${emote}" "PlayerbotAI::QueueContextWriteTo(player, [talker]" "else\n                GetBotAI(player)->GetAiObjectContext()->GetValue<ObjectGuid>(\"talk target\")->Set(bot->GetObjectGuid());" "old talk target in else")
require_order("${say}" "if (sPlayerbotAIConfig.x3cInboxWrites)" "memberAi->QueueContextWrite([lastSaidQualifier, until](AiObjectContext* other)" "last said queued")
require_text("${say}" "else\n                memberAi->GetAiObjectContext()->GetValue<time_t>(\"last said\", qualifier)->Set(until);" "old last said in else")
require_order("${rtsc}" "if (sPlayerbotAIConfig.x3cInboxWrites)" "PlayerbotAI::QueueContextWriteTo(player, [locationName, p](AiObjectContext* other)" "RTSC queued")
require_text("${rtsc}" "else\n\t\t\t\t\t\tSET_PAI_VALUE2(WorldPosition, \"RTSC saved location\", tokens[1], p);" "old RTSC in else")

message(STATUS "x3_context_lock source contract passed")
