cmake_policy(VERSION 3.16)
# twow-repo#601 (CLI-Deep 26.10.2026, OB-00 go): AiPlayerbot.QuestLoop.EmptyCampHold (default 0)
# - cand3-C: with InactiveReselect the emptied camp was remembered for 180 s only; cohort B went back to the same camp 561 times
#   3-30 min later and 83 % of these trips found it empty again (8.3 % of the B time). With the switch the mark lasts the
#   respawn of the spawn point (at most 900 s); the COOLDOWN back at the camp after a fruitless search stays the old one.
#   Pins the key, the policy, the hold after the old mark, the respawn source and the fallback cap.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "empty camp hold: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "empty camp hold: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "empty camp hold: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "empty camp hold: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "empty camp hold: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("QuestLoopPolicy.h" policy)
read_source("TravelMgr.cpp" travel)

# 1. Key: neutral default, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 questLoopEmptyCampHold = 0;" "member default off")
require_text("${config_cpp}" "questLoopEmptyCampHold = config.GetIntDefault(\"AiPlayerbot.QuestLoop.EmptyCampHold\", 0);" "default 0")
require_text("${config_cpp}" "if (questLoopEmptyCampHold > 2)" "unknown mode checked")
require_text("${config_cpp}" "{\"QuestLoop.EmptyCampHold\", Value(questLoopEmptyCampHold)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.QuestLoop.EmptyCampHold = 0\n" "documented")

# 2. Policy: the agreed cap, never below the base mark, the fallback never above it.
require_text("${policy}" "constexpr std::uint32_t EmptyCampHoldMaxSeconds = 900;" "cap 900 s")
require_text("${policy}" "return std::max(baseMs, std::min(respawnSeconds, EmptyCampHoldMaxSeconds) * 1000u);" "respawn within base .. cap")
require_text("${policy}" "return std::min(leftMs, baseMs);" "fallback capped at the base")

# 3. The hold: in the InactiveReselect branch after the old mark and its line, before the target ends; switch + cohort first.
between("${travel}" "else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "            if (progressRule)\n" camp)
require_order("${camp}" "\"[QuestLoop] state=inactive_reselect bot=%u level=%u quest=%u target_entry=%d mark_ms=%u\"" "if (ai::cohort::Applies(sPlayerbotAIConfig.questLoopEmptyCampHold, bot->GetGUIDLow()))" "after the old mark")
require_order("${camp}" "if (ai::cohort::Applies(sPlayerbotAIConfig.questLoopEmptyCampHold, bot->GetGUIDLow()))" "SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "before the target ends")
require_text("${camp}" "uint32 const holdMs = quest_loop::EmptyCampHoldMs(markMs, respawnSeconds);" "hold from the old mark")
require_text("${camp}" "if (holdMs > markMs)\n                        {\n                            quest_loop::MarkEmptyCamp(emptyCamps, objective->GetQuestId(), objective->GetEntry(), WorldTimer::getMSTime(), holdMs);" "re-marked only when longer")
require_text("${camp}" "[QuestLoop] state=empty_camp_hold bot=%u level=%u quest=%u target_entry=%d respawn_s=%u base_ms=%u hold_ms=%u" "greppable line")

# 4. Respawn source: the spawn point of the target (creature / positive game object spawntimesecsmin), O(1).
between("${travel}" "static uint32 SpawnRespawnSeconds(WorldPosition const* point)" "\n}\n" respawn)
require_text("${respawn}" "dynamic_cast<GuidPosition const*>(point)" "the target's spawn point")
require_text("${respawn}" "return data->spawntimesecsmin;" "creature respawn")
require_text("${respawn}" "return data->spawntimesecsmin > 0 ? uint32(data->spawntimesecsmin) : 0;" "script-only game object: no hold")

# 5. Fallback: the COOLDOWN back at the camp stays the old one.
between("${travel}" "bool TravelTarget::FallBackToInactiveCooldown()" "\n}\n" fallback)
require_order("${fallback}" "leftMs = quest_loop::FallbackCooldownMs(leftMs," "if (!leftMs)\n        return false;" "capped before the check")
require_text("${fallback}" "if (leftMs && ai::cohort::Applies(sPlayerbotAIConfig.questLoopEmptyCampHold, bot->GetGUIDLow()))" "switch + cohort")
