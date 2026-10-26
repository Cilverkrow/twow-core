cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026, OB-00 go, bug fix without a switch): StoreLootAction::IsLootAllowed turned a
# quest item down as soon as ONE quest in the log had enough of it - Blood Shard 5075 (889 needs 10, 5052 needs 1) was never looted
# again once the bot held one shard with 5052 in its log. Pins: every quest that requires the item adds its need inside the loop (no
# early return on a satisfied count), the item is turned down after the loop only when none needs more, the sync rule is unchanged.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "quest loot shared item: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "quest loot shared item: missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "quest loot shared item: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "quest loot shared item: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "quest loot shared item: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "quest loot shared item: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("strategy/actions/LootAction.cpp" loot)
read_source("LootSlotPolicy.h" policy)

# 1. The rule: satisfied only when at least one quest requires the item and none needs more.
require_text("${policy}" "need.required = true;\n    if (have < required)\n        need.needsMore = true;" "every quest adds its need")
require_text("${policy}" "return need.required && !need.needsMore;" "satisfied only when none needs more")

# 2. IsLootAllowed: the quest loop collects, the decision follows the loop, before the loot strategy.
between("${loot}" "bool StoreLootAction::IsLootAllowed(ItemQualifier& itemQualifier, PlayerbotAI *ai)" "\n}\n" allowed)
require_text("${allowed}" "ai::loot_slot::AddQuestNeed(questNeed, AI_VALUE2(uint32, \"item count\", proto->Name1), quest->ReqItemCount[i]);" "need added per quest")
reject_text("${allowed}" "if (AI_VALUE2(uint32, \"item count\", proto->Name1) >= quest->ReqItemCount[i])\n                    return false;" "no early return on the first satisfied quest")
require_text("${allowed}" "if (ai->GetMaster() && sPlayerbotAIConfig.syncQuestWithPlayer)\n                    return false; //Quest is autocomplete for the bot so no item needed." "sync rule unchanged")
require_order("${allowed}" "ai::loot_slot::QuestItemNeed questNeed;" "for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)" "need starts before the loop")
require_order("${allowed}" "for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)" "if (ai::loot_slot::QuestItemSatisfied(questNeed))\n        return false;" "decision after the loop")
require_order("${allowed}" "if (ai::loot_slot::QuestItemSatisfied(questNeed))\n        return false;" "bool canLoot = LootStrategyValue::CanLoot(itemQualifier, ai);" "before the loot strategy")
