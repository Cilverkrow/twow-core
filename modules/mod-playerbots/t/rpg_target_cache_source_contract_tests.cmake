# twow-repo#541 (deep dive R1, OB-00 go 08.10.2026): choose rpg target builds the rpg trigger list
# once per GetTargets call instead of once per candidate target (PerfMon A1600: the most expensive
# single action). Same order, same relevance rule, same reasons; no behaviour change.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseRpgTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseRpgTargetAction.h" choose_h)

# One list per GetTargets call (scope), freed at its end.
region("${choose}" "ChooseRpgTargetAction::GetTargets(Player* requester, bool debug)" "float ChooseRpgTargetAction::getMaxRelevance(" get_targets)
require_text("${get_targets}" "RpgTriggerCacheScope rpgTriggerScope(this);" "trigger list scoped to GetTargets")
require_text("${choose_h}" "~RpgTriggerCacheScope() { action->rpgTriggerCacheActive = false; action->ClearRpgTriggers(); }" "list freed at the end of the call")

# Per target only IsActive and the sub-action name: no strategy list, trigger nodes or handler arrays.
region("${choose}" "float ChooseRpgTargetAction::getMaxRelevance(GuidPosition guidP)" "bool ChooseRpgTargetAction::Execute(" max_rel)
reject_text("${max_rel}" "GetSupportedStrategies" "strategy list rebuilt per target")
reject_text("${max_rel}" "InitTriggers" "trigger nodes rebuilt per target")
reject_text("${max_rel}" "getHandlers" "handler arrays rebuilt per target")
require_text("${max_rel}" "if (!entry.trigger->IsActive())" "activity still checked per target")
require_text("${max_rel}" "if (entry.relevance < maxRelevance || entry.relevance > 2.0f)" "same relevance rule")
require_text("${max_rel}" "rgpActionReason[guidP] = entry.lastSubAction->GetRpgActionName();" "sub-action name asked per target (it may depend on the target)")

# The list is built from the same sources as before, once.
region("${choose}" "void ChooseRpgTargetAction::BuildRpgTriggers()" "void ChooseRpgTargetAction::ClearRpgTriggers()" build)
require_text("${build}" "GetSupportedStrategies(strategies);" "all strategies")
require_text("${build}" "rpgStrategy->InitTriggers(triggerNodes, BotState::BOT_STATE_NON_COMBAT);" "non-combat rpg triggers")
require_text("${build}" "if (dynamic_cast<RpgEnabled*>(action))" "rpg actions recognised as before")

message(STATUS "RPG_TARGET_CACHE_SOURCE_CONTRACT=PASS")
