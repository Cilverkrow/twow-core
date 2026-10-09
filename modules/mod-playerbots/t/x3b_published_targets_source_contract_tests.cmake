# twow-repo#563 X3b variant 2 (published snapshot, OB-00 go 10.10.2026), site 1: AttackersValue::AddTargetsOf.
# With AiPlayerbot.X3b.PublishedTargets (default 0) a bot never computes "possible targets" or reads
# "current/old target" on another bot's context: it reads that bot's published snapshot. Each bot publishes
# its own targets right after reading them on its own context. Off: the old code path, unchanged.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563 X3b: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#563 X3b: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#563 X3b: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#563 X3b: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#563 X3b: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("X3bSnapshotPolicy.h" policy)
read_source("strategy/values/AttackersValue.cpp" attackers)

# Switch default off, documented.
require_text("${config_h}" "bool x3bPublishedTargets = false;" "member default off")
require_text("${config_cpp}" "x3bPublishedTargets = config.GetBoolDefault(\"AiPlayerbot.X3b.PublishedTargets\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.X3b.PublishedTargets = 0" "documented")

# The snapshot is immutable and swapped under a mutex; the old one is released outside it.
require_text("${ai_h}" "std::shared_ptr<PublishedTargets const> publishedTargets;" "const snapshot pointer")
require_text("${ai_h}" "mutable std::mutex publishedTargetsMutex;" "pointer mutex")
between("${ai_cpp}" "void PlayerbotAI::PublishTargets(" "std::shared_ptr<PlayerbotAI::PublishedTargets const> PlayerbotAI::GetPublishedTargets() const" publish)
require_order("${publish}" "std::scoped_lock lock(publishedTargetsMutex);" "previous.swap(publishedTargets);" "swap under the lock")
require_order("${publish}" "publishedTargets = std::move(snapshot);\n    }" "// previous is released here" "old snapshot released after the lock")
between("${ai_cpp}" "std::shared_ptr<PlayerbotAI::PublishedTargets const> PlayerbotAI::GetPublishedTargets() const" "\n}\n" get_snapshot)
require_order("${get_snapshot}" "std::scoped_lock lock(publishedTargetsMutex);" "return publishedTargets;" "copy under the lock")
require_text("${policy}" "return uint32_t(nowMs - publishedMs) <= MaxAgeMs;" "wrap-safe age check")

# AddTargetsOf(Player*): another bot's targets only from its snapshot.
between("${attackers}" "void AttackersValue::AddTargetsOf(Player* player," "// Get the current attackers of the player" add_targets)
require_text("${add_targets}" "bool const otherBot = playerBot && playerBot != ai;" "other bot detection")
between("${add_targets}" "if (otherBot && sPlayerbotAIConfig.x3bPublishedTargets)" "else if (playerBot)" read_branch)
require_text("${read_branch}" "playerBot->GetPublishedTargets();" "snapshot read")
foreach(forbidden "PAI_VALUE" "GetAiObjectContext" "AI_VALUE")
  forbid_text("${read_branch}" "${forbidden}" "the snapshot branch must not touch a value context")
endforeach()
require_order("${read_branch}" "if (!snapshot)" "x3b::SnapshotCount(x3b::SnapshotMissing)" "missing counted")
require_order("${read_branch}" "!x3b::Fresh(WorldTimer::getMSTime(), snapshot->publishedMs)" "x3b::SnapshotCount(x3b::SnapshotStale)" "stale counted")
require_order("${add_targets}" "if (otherBot && sPlayerbotAIConfig.x3bPublishedTargets)" "PAI_VALUE2(std::list<ObjectGuid>, \"possible targets\"" "snapshot branch before the old path")
between("${add_targets}" "else if (playerBot)" "// Add the pull and attack targets" old_branch)
require_order("${old_branch}" "PAI_VALUE2(std::list<ObjectGuid>, \"possible targets\"" "if (!otherBot && sPlayerbotAIConfig.x3bPublishedTargets)" "publish after the own read")
require_order("${old_branch}" "if (!otherBot && sPlayerbotAIConfig.x3bPublishedTargets)" "ai->PublishTargets(std::move(snapshot));" "publish only our own targets")

# Minute line, only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.x3bPublishedTargets)" "[X3bSnapshot] hit=%llu stale=%llu missing=%llu max_age_ms=%u" "minute line behind the switch")

message(STATUS "x3b_published_targets source contract passed")
