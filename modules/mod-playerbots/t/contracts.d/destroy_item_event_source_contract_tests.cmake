cmake_policy(VERSION 3.16)

# twow-repo#333 (OB-00 26.10.2026, P1-s smoke): every destroyed item stack writes one "DestroyItemAction" bot_events line
# (name, count, reason; the item id last, as SellAction). Before, a destroy left no event and no character_destroyed_items
# row, so the smoke's destroy counter was 0 by construction. No switch: it is telemetry only, written only when
# bot_events.csv is enabled.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 destroy event: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 destroy event: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle count description)
  string(REPLACE "${needle}" "" rest "${text}")
  string(LENGTH "${text}" a)
  string(LENGTH "${rest}" b)
  string(LENGTH "${needle}" n)
  math(EXPR found "(${a} - ${b}) / ${n}")
  if(NOT found EQUAL count)
    message(FATAL_ERROR "#333 destroy event: ${description}: '${needle}' found ${found} times, expected ${count}")
  endif()
endfunction()

read_source("strategy/actions/DestroyItemAction.cpp" cpp)
read_source("strategy/actions/DestroyItemAction.h" h)

# The event: only with bot_events.csv, name + count + reason, the item id as the last field.
require_text("${cpp}" "if (!sPlayerbotAIConfig.hasLog(\"bot_events.csv\"))\n        return;" "nothing built without bot_events.csv")
require_text("${cpp}" "sPlayerbotAIConfig.logEvent(ai, \"DestroyItemAction\",\n        std::string(proto->Name1) + \" count=\" + std::to_string(item->GetCount()) + \" reason=\" + reason,\n        std::to_string(proto->ItemId));" "name, count, reason, item id last")

# Every destroy path logs before the item is gone: the shared helper (command / smart / smart_gray) and both loops of
# DestroyAllGrayItemsAction.
require_text("${h}" "void DestroyItem(FindItemVisitor* visitor, Player* requester, char const* reason = \"command\");" "the reason, the chat command by default")
require_order("${cpp}" "        LogDestroyEvent(ai, item, reason);\n        bot->DestroyItem(item->GetBagSlot(),item->GetSlot(), true);" "bool SmartDestroyItemAction::Execute" "the shared helper logs before it destroys")
require_text("${cpp}" "DestroyItem(&visitor, requester, \"smart_gray\");" "smart destroy, grey only")
require_text("${cpp}" "DestroyItem(&visitor, requester, \"smart\");" "smart destroy")
require_count("${cpp}" "LogDestroyEvent(ai, pItem, \"all_gray\");\n" 2 "both loops of DestroyAllGrayItemsAction")
require_count("${cpp}" "bot->DestroyItem(" 3 "the destroy calls this contract covers (a new one needs its event)")

message(STATUS "#333 destroy event source contract passed")
