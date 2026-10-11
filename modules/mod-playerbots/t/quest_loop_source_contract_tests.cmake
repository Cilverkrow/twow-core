# twow-repo#581 (QUEST-LOOP-01, owner 11.10.2026): QL-1 AiPlayerbot.QuestLoop.MaxFailures (failure memory per bot x quest)
# and QL-2 AiPlayerbot.QuestLoop.InactiveCooldownSeconds (longer cooldown after an emptied camp), both default 0.

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
    message(FATAL_ERROR "#581 quest loop: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#581 quest loop: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#581 quest loop: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#581 quest loop: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)
read_source("TravelMgr.h" travel_h)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
read_source("QuestLoopPolicy.h" policy)

require_text("${config_h}" "uint32 questLoopMaxFailures = 0;" "QL-1 default off")
require_text("${config_h}" "uint32 questLoopInactiveCooldownSeconds = 0;" "QL-2 default off")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.QuestLoop.MaxFailures\", 0)" "QL-1 config default 0")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.QuestLoop.InactiveCooldownSeconds\", 0)" "QL-2 config default 0")
require_text("${conf_dist}" "AiPlayerbot.QuestLoop.MaxFailures = 0" "QL-1 documented")
require_text("${conf_dist}" "AiPlayerbot.QuestLoop.InactiveCooldownSeconds = 0" "QL-2 documented")

# The inactive drop: objectives only, QL-2 behind its switch, QL-1 counted, after the cooldown status is set.
between("${travel}" "TraceQuestCommit(tDestination, \"drop\", destinationInactive ? \"destination_inactive\" : \"conditions_inactive\");" "return;" inactive_path)
require_order("${inactive_path}" "SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);" "if (destinationInactive && dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination))" "after the cooldown starts")
require_order("${inactive_path}" "if (sPlayerbotAIConfig.questLoopInactiveCooldownSeconds)" "SetExpireIn(quest_loop::InactiveCooldownMs(tDestination->GetCooldownDelay(), sPlayerbotAIConfig.questLoopInactiveCooldownSeconds));" "QL-2 behind its switch")
require_text("${inactive_path}" "NoteQuestObjectiveFailure();" "QL-1 counts the inactive drop")

# Work timeouts of objectives count too; turn-ins keep their own rule (return before).
between("${travel}" "void TravelTarget::OnWorkTimeout()" "\n}\n" work_fn)
require_order("${work_fn}" "NoteTurnInFailure(static_cast<QuestTravelDestination*>(tDestination)->GetQuestId(), \"work_timeout\");\n        return;" "NoteQuestObjectiveFailure();" "turn-ins are not counted")

# Counting: behind the switch, per quest, bounded, visible.
between("${travel}" "void TravelTarget::NoteQuestObjectiveFailure()" "\n}\n" note_fn)
require_text("${note_fn}" "if (!sPlayerbotAIConfig.questLoopMaxFailures || !objective" "QL-1 behind its switch, objectives only")
require_text("${note_fn}" "uint32 const questId = objective->GetQuestId();" "keyed by quest, not by square")
require_text("${note_fn}" "if (questLoops.size() > 32 && questLoops.find(questId) == questLoops.end())" "bounded")
require_text("${note_fn}" "[QuestLoop] state=suppressed" "visible")
require_text("${travel_h}" "std::map<uint32, quest_loop::Record> questLoops;" "per quest id")

# The chooser skips suppressed objectives, and the refresh does not bring them back.
require_order("${choose}" "persistentTarget->IsDestinationDeathSuppressed(destination)" "if (persistentTarget && persistentTarget->IsQuestLoopSuppressed(destination))" "chooser skip next to the death rule")
require_text("${choose}" "target->IsQuestLoopSuppressed(oldDestination))   // twow-repo#581 (QL-1)" "refresh does not undo it")
between("${travel}" "bool TravelTarget::IsQuestLoopSuppressed(" "\n}\n" sup_fn)
require_text("${sup_fn}" "dynamic_cast<QuestObjectiveTravelDestination const*>(destination)" "objectives only (givers and turn-ins untouched)")

# Policy: window, cooldown, never shorter than the destination's own.
require_text("${policy}" "if (!maxFailures)\n            return false;" "0 = off")
require_text("${policy}" "nowMs - record.windowStart >= windowMs" "failures outside the window do not add up")
require_text("${policy}" "return std::max(destinationCooldownMs, minSeconds * 1000u);" "QL-2 only lengthens")

message(STATUS "quest_loop source contract passed")
