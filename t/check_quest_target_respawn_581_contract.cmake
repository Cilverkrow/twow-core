if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#581 QL-3 (owner 14.10.2026): DynamicRespawn.QuestTarget.* (default 0). Few-spawn quest targets respawn
# faster the more nearby players need them; nobody interested or switch off = respawn exactly as before.

function(read_core path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#581 QL-3: missing ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#581 QL-3: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text first second out_var)
  string(FIND "${text}" "${first}" a)
  if (a EQUAL -1)
    message(FATAL_ERROR "#581 QL-3: region not found: ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if (b EQUAL -1)
    message(FATAL_ERROR "#581 QL-3: region end not found: ${second}")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} out)
  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

read_core("src/game/World.cpp" world)
read_core("src/mangosd/mangosd.conf.dist.in" config)
read_core("src/game/Objects/Creature.cpp" creature)
read_core("src/game/ObjectMgr.cpp" object_mgr)
read_core("src/game/Objects/QuestTargetRespawnPolicy.h" policy)

# Switch default off, documented.
require_text("${world}" "setConfig(CONFIG_BOOL_DYN_RESPAWN_QUEST_TARGET, \"DynamicRespawn.QuestTarget.Enable\", false);" "switch default off")
require_text("${config}" "DynamicRespawn.QuestTarget.Enable = 0" "documented default 0")
require_text("${config}" "DynamicRespawn.QuestTarget.MaxSpawns = 3" "max spawns documented")

# Hook: after the existing dynamic respawn, never for accelerated rares.
region("${creature}" "void Creature::SetDeathState(DeathState s)" "m_corpseDecayTimer" death)
require_order("${death}" "if (!acceleratedRare)" "ApplyQuestTargetRespawnDelay(respawnDelay, data);" "only without an accelerated rare")
require_order("${death}" "ApplyDynamicRespawnDelay(respawnDelay, data);" "ApplyQuestTargetRespawnDelay(respawnDelay, data);" "after DynamicRespawn")

# The delay function: switch first, continents, normal/elite only, candidates only.
region("${creature}" "void Creature::ApplyQuestTargetRespawnDelay(uint32& delay, CreatureData const* data)" "\n}\n" apply)
require_order("${apply}" "if (!sWorld.getConfig(CONFIG_BOOL_DYN_RESPAWN_QUEST_TARGET)" "Cell::VisitWorldObjects(this, searcher" "switch before any search")
require_text("${apply}" "GetMapId() > 1" "continents only")
require_text("${apply}" "if (GetCreatureInfo()->rank > CREATURE_ELITE_ELITE)" "rares and bosses excluded")
require_order("${apply}" "if (!target)\n        return;" "Cell::VisitWorldObjects(this, searcher" "non-candidates skip the search")
require_text("${apply}" "quest_target_respawn::ScaledDelay(delay, check.GetCount()" "policy decides the delay")

# Interest: incomplete kill objective or a quest drop this player still needs.
region("${creature}" "class QuestTargetRespawnChecker" "};" checker)
require_text("${checker}" "player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE" "kill objective still open")
require_text("${checker}" "LootTemplates_Creature.HaveQuestLootForPlayer(_lootId, player)" "quest drop still needed")

# Index: built once, thread-safe, by spawn count and quest relation.
region("${object_mgr}" "ObjectMgr::QuestRespawnTarget const* ObjectMgr::GetQuestRespawnTarget(uint32 entry) const" "\n}\n" index)
require_text("${index}" "std::call_once(m_questRespawnTargetsOnce" "built once (map threads)")
require_text("${index}" "quest_target_respawn::IsCandidate(count, maxSpawns" "few-spawn rule from the policy")
require_text("${index}" "LootTemplates_Creature.HaveQuestLootFor(info->loot_id)" "quest drops count")

# Policy: never longer, never below the minimum, nobody interested = unchanged.
require_text("${policy}" "if (!interested || perPlayer <= 0.0f || delay <= minSeconds)\n            return delay;" "unchanged without interest")
require_text("${policy}" "return std::min(result, delay);" "never longer than before")

message(STATUS "quest_target_respawn_581 contract passed")
