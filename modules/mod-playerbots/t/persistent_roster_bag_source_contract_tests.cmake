if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" manager)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.h" manager_header)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

require_text("${manager_header}" "void ProvisionPersistentRosterBags(Player* bot);"
  "private roster bag provisioner")
require_text("${manager}" "#include \"playerbot/PersistentRosterBagPolicy.h\""
  "standalone bag policy")
require_text("${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterBags(Player* bot)"
  "roster bag provisioner implementation")
require_text("${manager}" "constexpr uint32 kPersistentRosterBagItemId = 50004;"
  "pinned bag item id")
require_text("${manager}" "proto->ContainerSlots != 36"
  "36-slot template validation")
require_text("${manager}" "proto->BagFamily != BAG_FAMILY_NONE"
  "normal bag-family validation")
require_text("${manager}" "bot->CanEquipItem(slot, destination, proto, nullptr, false, false)"
  "non-mutating no-swap equip preflight")
require_text("${manager}" "bot->EquipNewItem(destination, kPersistentRosterBagItemId, true)"
  "core-managed item creation and equip")
require_text("${manager}" "ProvisionPersistentRosterBags(bot);"
  "roster login hook")

string(FIND "${manager}" "void RandomPlayerbotMgr::OnBotLoginInternal(Player * const bot)" login_start)
string(FIND "${manager}" "void RandomPlayerbotMgr::OnPlayerLogin(Player* player)" login_end)
string(FIND "${manager}" "ProvisionPersistentRosterBags(bot);" call_offset)
if(login_start EQUAL -1 OR login_end EQUAL -1 OR call_offset EQUAL -1 OR
   call_offset LESS login_start OR call_offset GREATER login_end)
  message(FATAL_ERROR "Roster bag provisioner is not confined to the one-time login hook")
endif()

string(FIND "${manager}" "void RandomPlayerbotMgr::ProvisionPersistentRosterBags(Player* bot)" provision_start)
string(FIND "${manager}" "void RandomPlayerbotMgr::OnPlayerLogin(Player* player)" provision_end)
if(provision_start EQUAL -1 OR provision_end EQUAL -1 OR provision_end LESS provision_start)
  message(FATAL_ERROR "Could not isolate roster bag provisioner")
endif()
math(EXPR provision_length "${provision_end} - ${provision_start}")
string(SUBSTRING "${manager}" ${provision_start} ${provision_length} provision_region)
foreach(forbidden "Randomize(" "DestroyItem(" "RemoveItem(" "MoveItemFromInventory(" "StoreNewItem(" "CharacterDatabase" "LoginDatabase")
  string(FIND "${provision_region}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR "Roster bag provisioner contains forbidden ${forbidden}")
  endif()
endforeach()

message(STATUS "PERSISTENT_ROSTER_BAG_SOURCE_CONTRACT=PASS")
