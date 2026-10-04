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
message(STATUS "SELF_CAST_ITEM_CONTRACT=PASS")
