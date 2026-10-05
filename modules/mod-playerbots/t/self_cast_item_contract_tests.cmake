# Hotfix 8.28: no self cast without the item it names; disenchant only items in the bags.
file(READ "${PB_SOURCE_DIR}/strategy/actions/CastCustomSpellAction.cpp" cast)
foreach (needle
    "if (items.empty() && param.find(\"Hitem:\") != std::string::npos)"
    "if (selfCommand && missingItem && !itemTarget)"
    "[CastSelf] state=no_item_target bot=%u"
    "if (ai->InventoryParseItems(chat->formatQItem(item), IterateItemsMask::ITERATE_ITEMS_IN_BAGS).empty())")
  string(FIND "${cast}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "self cast item: missing ${needle}")
  endif()
endforeach()
string(FIND "${cast}" "if (selfCommand && missingItem && !itemTarget)" guard)
string(FIND "${cast}" "const bool canCast = " cancast)
if (guard GREATER cancast)
  message(FATAL_ERROR "self cast item: the guard must come before the cast check")
endif()
# Hotfix 8.30: the disenchant filter also honours ITEM_FLAG_NO_DISENCHANT (Spell::CheckItems refuses it).
string(FIND "${cast}" "proto->Quality, proto->DisenchantID, (proto->Flags & ITEM_FLAG_NO_DISENCHANT) != 0))" noflag)
if (noflag EQUAL -1)
  message(FATAL_ERROR "self cast item: disenchant filter ignores ITEM_FLAG_NO_DISENCHANT")
endif()
# Hotfix 8.30a: NO_DISENCHANT items are no disenchant usage; a skip is logged (throttled).
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
string(FIND "${usage}" "if (proto->DisenchantID && !(proto->Flags & ITEM_FLAG_NO_DISENCHANT))" usage_flag)
string(FIND "${cast}" "[ProfessionUse] stage=disenchant state=skipped reason=no_disenchant_flag bot=%u" skip_log)
if (usage_flag EQUAL -1 OR skip_log EQUAL -1)
  message(FATAL_ERROR "self cast item: 8.30a usage flag or skip line missing")
endif()
message(STATUS "SELF_CAST_ITEM_CONTRACT=PASS")
