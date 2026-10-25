# twow-repo#601 root cause of the cand-v466r standing (CLI-Deep 25.10.2026, OB-00 go): AiPlayerbot.QuestLoop.InactiveAfterProgress
# (default 0) - an objective that goes inactive (no living target in sight) or whose work phase runs out after the bot got credit on
# it is progress, not an emptied camp: no QL-1, no QL-2 / #405 count, a short cooldown after an inactive drop.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "inactive after progress: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "inactive after progress: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "inactive after progress: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "inactive after progress: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)
read_source("TravelMgr.h" travel_h)
read_source("QuestLoopPolicy.h" policy)

# 1. Key: neutral default, an unknown mode is off, documented.
require_text("${config_h}" "uint32 questLoopInactiveAfterProgress = 0;" "member default off")
require_text("${config_cpp}" "questLoopInactiveAfterProgress = config.GetIntDefault(\"AiPlayerbot.QuestLoop.InactiveAfterProgress\", 0);" "default 0")
require_text("${config_cpp}" "if (questLoopInactiveAfterProgress > 2)" "unknown mode checked")
require_text("${conf_dist}" "\nAiPlayerbot.QuestLoop.InactiveAfterProgress = 0\n" "documented")

# 2. The count: taken when a new destination is set (a refresh keeps it), quest items for loot objectives, credit otherwise.
between("${travel}" "void TravelTarget::SetTarget(" "\n}\n" set_target)
require_text("${set_target}" "if (sPlayerbotAIConfig.questLoopInactiveAfterProgress && tDestination1 != tDestination)\n        objectiveCountAtStart = ObjectiveCountOf(bot, tDestination1);" "count at a new destination")
require_order("${set_target}" "objectiveCountAtStart = ObjectiveCountOf(bot, tDestination1);" "tDestination = tDestination1;" "taken before the destination changes")
require_text("${travel}" "return quest->ReqItemCount[index] > 0 ? int32(it->second.m_itemcount[index]) : int32(it->second.m_creatureOrGOcount[index]);" "items or credit")
require_text("${travel_h}" "int32 objectiveCountAtStart = -1;" "no count by default")
require_text("${policy}" "return countAtStart >= 0 && countNow > countAtStart;" "only a grown count is progress")
require_text("${policy}" "constexpr std::uint32_t ProgressCooldownMs = 1000;" "short cooldown")

# 3. The inactive drop: roster bot on its own, cohort mode; progress -> short cooldown, else the #581 rule unchanged; count retaken.
between("${travel}" "TraceQuestCommit(tDestination, \"drop\", destinationInactive ? \"destination_inactive\" : \"conditions_inactive\");" "            return;\n        }\n    }\n}" inactive)
require_text("${inactive}" "ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveAfterProgress, bot->GetGUIDLow()) &&\n                sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();" "roster bots on their own, cohort mode")
require_text("${inactive}" "if (progressRule && quest_loop::InactiveAfterProgress(objectiveCountAtStart, objectiveCountNow))" "progress decides")
require_text("${inactive}" "\"[QuestLoop] state=inactive_after_progress bot=%u level=%u quest=%u target_entry=%d count_from=%d count_to=%d cooldown_ms=%u\"" "greppable line")
require_order("${inactive}" "SetExpireIn(quest_loop::ProgressCooldownMs);" "else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "the emptied-camp rule only without progress")
require_order("${inactive}" "else if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "NoteQuestObjectiveFailure();" "QL-1 stays in the emptied-camp branch")
require_text("${inactive}" "if (progressRule)\n                objectiveCountAtStart = objectiveCountNow;" "count retaken: a second drop without credit is an emptied camp")

# 4. The work timeout: progress counts for neither QL-1 nor #405; turn-ins keep their own path first.
between("${travel}" "void TravelTarget::OnWorkTimeout()" "\n}\n" work)
require_order("${work}" "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");\n        return;" "ai::cohort::Applies(sPlayerbotAIConfig.questLoopInactiveAfterProgress, bot->GetGUIDLow())" "turn-ins first")
require_order("${work}" "if (progressed)\n            return;" "NoteQuestObjectiveFailure();" "before the QL-1 count")
require_order("${work}" "if (progressed)\n            return;" "destination_death::RecordDeath(workTimeouts[tDestination]" "before the #405 count")
require_text("${work}" "\"[QuestLoop] state=work_after_progress bot=%u level=%u quest=%u target_entry=%d count_from=%d count_to=%d\"" "greppable line")

message(STATUS "inactive_after_progress source contract passed")
