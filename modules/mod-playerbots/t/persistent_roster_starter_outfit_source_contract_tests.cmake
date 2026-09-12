if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" manager)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.h" manager_header)
file(READ "${PB_SOURCE_DIR}/PersistentRosterStarterOutfitPolicy.h" policy)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

require_text("${manager_header}" "void ProvisionPersistentRosterStarterOutfit(Player* bot);"
  "private starter outfit provisioner")
require_text("${manager}" "#include \"playerbot/PersistentRosterStarterOutfitPolicy.h\""
  "standalone starter outfit policy")
require_text("${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterStarterOutfit(Player* bot)"
  "starter outfit provisioner implementation")
require_text("${manager}" "IsPersistentRosterMember(bot->GetGUIDLow())"
  "persistent roster admission gate")
require_text("${manager}" "ShouldProvision(IsPersistentRosterMember(bot->GetGUIDLow()), bot->GetLevel())"
  "level-one-only policy gate")
require_text("${manager}" "sObjectMgr.GetPlayerInfo(bot->GetRace(), bot->GetClass())"
  "canonical race/class PlayerCreateInfo lookup")
require_text("${manager}" "for (PlayerCreateInfoItem const& item : info->item)"
  "canonical PlayerCreateInfo item iteration")
require_text("${manager}" "bot->GetItemCount(itemId)"
  "inventory completion marker")
require_text("${manager}" "bot->StoreNewItemInBestSlots(itemId, missing)"
  "core-managed missing item provision")
require_text("${manager}" "for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)"
  "bounded canonical second-pass slot range")
require_text("${manager}" "required.find(item->GetEntry()) == required.end()"
  "canonical starter-item second-pass gate")
require_text("${manager}" "bot->CanEquipItem(NULL_SLOT, destination, item, false)"
  "non-replacing canonical equipment check")
require_text("${manager}" "SelectSecondPassAction(true, false, canEquip, canUseAmmo)"
  "policy-governed canonical second pass")
require_text("${manager}" "bot->EquipItem(destination, item, true)"
  "canonical main-hand or offhand equip")
require_text("${manager}" "bot->CanUseAmmo(item->GetEntry()) == EQUIP_ERR_OK"
  "canonical ammunition eligibility check")
require_text("${manager}" "bot->SetAmmo(item->GetEntry())"
  "canonical ammunition selection")
require_text("${manager}" "retrying on next roster login"
  "partial failure retry diagnostic")
require_text("${manager}" "ProvisionPersistentRosterStarterOutfit(bot);"
  "roster login hook")

string(FIND "${manager}" "void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)" login_start)
string(FIND "${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterBags(Player* bot)" login_end)
string(FIND "${manager}" "ProvisionPersistentRosterStarterOutfit(bot);" call_offset)
if(login_start EQUAL -1 OR login_end EQUAL -1 OR call_offset EQUAL -1 OR call_offset LESS login_start OR call_offset GREATER login_end)
  message(FATAL_ERROR "Starter outfit provisioner is not confined to the roster login hook")
endif()
math(EXPR login_length "${login_end} - ${login_start}")
string(SUBSTRING "${manager}" ${login_start} ${login_length} login_region)
string(FIND "${login_region}" "if (IsPersistentRosterMember(bot->GetGUIDLow()))" roster_gate_offset)
string(FIND "${login_region}" "ProvisionPersistentRosterStarterOutfit(bot);" roster_call_offset)
if(roster_gate_offset EQUAL -1 OR roster_call_offset EQUAL -1 OR roster_call_offset LESS roster_gate_offset)
  message(FATAL_ERROR "Starter outfit provisioner is not protected by the persistent roster gate")
endif()

string(FIND "${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterStarterOutfit(Player* bot)" provision_start)
string(FIND "${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterBags(Player* bot)" provision_end)
if(provision_start EQUAL -1 OR provision_end EQUAL -1 OR provision_end LESS provision_start)
  message(FATAL_ERROR "Could not isolate starter outfit provisioner")
endif()
math(EXPR provision_length "${provision_end} - ${provision_start}")
string(SUBSTRING "${manager}" ${provision_start} ${provision_length} provision_region)
foreach(forbidden "Randomize(" "DestroyItem(" "MoveItemFromInventory(" "SwapItem(" "StoreNewItem(" "CanStoreItem(" "StoreItem(" "CharacterDatabase" "LoginDatabase" "SetEventValue(")
  string(FIND "${provision_region}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR "Starter outfit provisioner contains forbidden ${forbidden}")
  endif()
endforeach()
require_text("${provision_region}" "bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true)"
  "bounded main-backpack-only equip transfer")
require_text("${provision_region}" "bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)"
  "main-backpack-only second-pass lookup")
foreach(bag_marker "INVENTORY_SLOT_BAG_START" "50004")
  string(FIND "${provision_region}" "${bag_marker}" bag_marker_offset)
  if(NOT bag_marker_offset EQUAL -1)
    message(FATAL_ERROR "Starter outfit provisioner must not touch persistent-roster bags (${bag_marker})")
  endif()
endforeach()

message(STATUS "PERSISTENT_ROSTER_STARTER_OUTFIT_SOURCE_CONTRACT=PASS")
