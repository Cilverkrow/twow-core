function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)

# #405: a work phase that runs out counts against the quest objective; after Max
# the objective is suppressed like a death-suppressed destination.
require_text("${travel_mgr}" "OnWorkTimeout();" "work timeout counted before the target expires")
require_text("${travel_mgr}" "void TravelTarget::OnWorkTimeout()" "work timeout rule")
require_text("${travel_mgr}" "dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination)" "quest objectives only")
require_text("${travel_mgr}" "TraceQuestCommit(tDestination, \"abandon\", \"work_suppressed\");" "visible suppression")
require_text("${travel_mgr}" "SuppressCurrentDestination(cooldownMs);" "same suppression as the death rules")
require_text("${choose}" "persistentTarget->IsDestinationDeathSuppressed(destination)" "selection skips suppressed destinations")
require_text("${config_source}" "\"AiPlayerbot.QuestWorkTimeouts.Max\", 0" "off by default")
require_text("${config_template}" "AiPlayerbot.QuestWorkTimeouts.Max = 0" "documented key")

# #405 (b): quest-only loot is resolved per player by the Core (quest slots come
# after the normal items), not by the GetLootItemInSlot shim that returned null.
file(READ "${PB_SOURCE_DIR}/strategy/actions/LootAction.cpp" loot_action)
require_text("${loot_action}" "LootItem* lootItem = loot->LootItemInSlot(itemindex, bot->GetGUIDLow());" "per-player loot slot")
require_text("${loot_action}" "reason=no_slot" "visible skip when a slot has no item")
