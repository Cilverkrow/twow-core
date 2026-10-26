cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026, OB-00 go, A/B candidate): AiPlayerbot.QuestLoop.GameObjectPresence (default 0)
# - QuestObjectiveTravelDestination::IsActive checked "a living target in sight" for creatures only, so a game object objective never
# went inactive and the bot stood at a looted, despawned chest until the work timeout. With the switch the objective the bot is working
# at counts only while a spawned object of its entry that is lootable or being opened is in sight. Pins the key, the place (after the
# creature check), the gate, the scope (current target in WORK only) and the presence rule.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "go objective presence: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "go objective presence: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "go objective presence: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "go objective presence: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "go objective presence: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 questLoopGameObjectPresence = 0;" "member default off")
require_text("${config_cpp}" "questLoopGameObjectPresence = config.GetIntDefault(\"AiPlayerbot.QuestLoop.GameObjectPresence\", 0);" "default 0")
require_text("${config_cpp}" "if (questLoopGameObjectPresence > 2)" "unknown mode checked")
require_text("${config_cpp}" "{\"QuestLoop.GameObjectPresence\", Value(questLoopGameObjectPresence)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestLoop.GameObjectPresence = 0\n" "documented")

# 2. Place: in the objective's IsActive, after the creature presence check, before its final "true".
between("${travel}" "bool QuestObjectiveTravelDestination::IsActive(Player* bot, const PlayerTravelInfo& info) const {" "\n}\n" active)
require_order("${active}" "if (target.GetEntry() == GetEntry() && target.IsCreature() && ai->GetCreature(target) && ai->GetCreature(target)->IsAlive())" "if (GetEntry() < 0 && ai::cohort::Applies(sPlayerbotAIConfig.questLoopGameObjectPresence, bot->GetGUIDLow()) &&" "after the creature check")

# 3. Gate and scope: game objects, switch + cohort first (no distance work with the switch off), roster bot on its own, at the spot;
#    only the current target in WORK.
require_text("${active}" "if (GetEntry() < 0 && ai::cohort::Applies(sPlayerbotAIConfig.questLoopGameObjectPresence, bot->GetGUIDLow()) &&\n        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() && !IsOut(botPos))" "gate")
require_text("${active}" "if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK && target->GetEntry() == GetEntry())\n        {\n            std::list<ObjectGuid> gameObjects = AI_VALUE(std::list<ObjectGuid>, \"nearest game objects no los\");" "current target in WORK, one list")

# 4. Presence: an object of the entry, spawned, lootable or being opened; none -> inactive.
require_text("${active}" "if (guid.GetEntry() != uint32(-GetEntry()))\n                    continue;" "the objective's entry")
require_text("${active}" "if (!go || !go->IsSpawned())\n                    continue;" "spawned only")
require_text("${active}" "bool const restocking = go->GetGoType() == GAMEOBJECT_TYPE_CHEST && !go->GetGOInfo()->chest.consumable &&\n                    go->GetGOInfo()->chest.chestRestockTime;" "a restocking chest stays present")
require_text("${active}" "if (go->GetLootState() != GO_NOT_READY || restocking)\n                    return true;\n            }\n            return false;\n        }\n    }\n\n    return true;" "lootable / being opened / restocking; none in sight: inactive, right before the final true")
