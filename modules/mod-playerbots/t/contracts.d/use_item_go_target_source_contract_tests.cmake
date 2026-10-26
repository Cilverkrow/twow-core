cmake_policy(VERSION 3.16)
# twow-repo#601 cr2-C GO diagnosis (CLI-Deep 26.10.2026, OB-00 go, bug fix without a switch): UseAction::Execute ran its nearest-object
# name search after a game object link and tested std::string::find as a bool (true = NOT found), so the nearest unrelated object
# replaced the linked one. OpenLootAction's key path ("use <key item> <chest>") then cast the key item spell on that object:
# Digging Claw 5166 -> BAD_TARGETS, Silithid Mound (quest 868) never opened (9 B bots, 0 credit). Pins: a linked object wins, the
# name search is the fallback only and matches a name that contains the text.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "use item go target: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "use item go target: missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "use item go target: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "use item go target: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("strategy/actions/UseItemAction.cpp" use)
read_source("strategy/actions/LootAction.cpp" loot)

# 1. The link is parsed first; the name search only runs without one.
require_order("${use}" "std::list<ObjectGuid> gos = chat->parseGameobjects(useName);" "std::list<ObjectGuid> nearestGOs = targetGameObject ? std::list<ObjectGuid>() : AI_VALUE(std::list<ObjectGuid>, \"nearest game objects no los\");" "link first, search only as fallback")

# 2. A name matches when it contains the text (find != npos), never find() as a bool.
require_text("${use}" "if (go && std::string(go->GetName()).find(useName) != std::string::npos)" "contains, not 'not found'")
reject_text("${use}" "if (go && std::string(go->GetName()).find(useName))\n" "find() as a bool")

# 3. The caller this fixes: the key path of OpenLootAction hands the key item and the chest as links.
require_text("${loot}" "return ai->DoSpecificAction(\"use\", Event(\"do loot\", chat->formatQItem(lootObject.reqItem) + \" \" + chat->formatGameobject(go)));" "key path passes item and chest links")
