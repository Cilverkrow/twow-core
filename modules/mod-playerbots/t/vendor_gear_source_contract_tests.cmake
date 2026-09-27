function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/BuyAction.cpp" buy)
file(READ "${PB_SOURCE_DIR}/strategy/values/VendorValues.cpp" vendor_values)
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# twow-repo#363: roster bots on their own, up to the level cap, behind a switch.
require_text("${buy}" "IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster()" "roster bots on their own")
require_text("${buy}" "vendor_gear::InScope(VendorGearSettings(), rosterOnItsOwn, bot->GetLevel())" "scope check")
require_text("${buy}" "bool const gearPolicy = buyUseful && VendorGearInScope(ai);" "only on a vendor visit (buy vendor)")
require_text("${buy}" "bool const policyGear = gearPolicy && usage == ItemUsage::ITEM_USAGE_EQUIP;" "only EQUIP usage (class/spec/proficiency)")

# Decision through the stat score and the policy.
require_text("${buy}" "sRandomItemMgr.ItemStatWeight(bot, offered)" "stat score of the offer")
require_text("${buy}" "sRandomItemMgr.ItemStatWeight(bot, oldItem)" "stat score of the equipped item")
require_text("${buy}" "vendor_gear::Decide(offer, gearAllowance, gearSpent)" "policy decision")

# Budget: reserve for repairs and class training.
require_text("${buy}" "(uint32)NeedMoneyFor::repair" "repair reserve")
require_text("${buy}" "(uint32)NeedMoneyFor::spells" "training reserve")
require_text("${vendor_values}" "BuyAction::VendorGearAllowance(ai)" "trigger uses the same budget")

# Buy-loop contract: owned, bought before, slot per visit, remembered in the store.
require_text("${buy}" "offer.alreadyOwned = bot->HasItemCount(proto->ItemId, 1, false);" "owned check")
require_text("${buy}" "offer.boughtBefore = gearMemory.BoughtBefore(proto->ItemId);" "no repeat buy")
require_text("${buy}" "offer.slotBoughtThisVisit = gearMemory.SlotDone(gearSlot);" "one per slot and visit")
require_text("${buy}" "vendor_gear::Store::Instance().Put(bot->GetGUIDLow(), gearMemory);" "memory kept across visits")
forbid_text("${buy}" "\"manual time::vendor" "per-item AI context values (#351)")

# Diagnostics.
require_text("${buy}" "[VendorGear] %s reason=%s" "diagnostic line")
require_text("${buy}" "if (!buy && !sPlayerbotAIConfig.vendorGearTrace)" "skips only with trace")

# Config: default off, documented.
require_text("${config_source}" "\"AiPlayerbot.VendorGear.Enabled\", false" "default off")
require_text("${config_source}" "\"AiPlayerbot.VendorGear.MaxLevel\", 30" "level cap 30")
require_text("${config_source}" "\"AiPlayerbot.VendorGear.MaxSpendPercent\", 50" "spend limit")
foreach(line "AiPlayerbot.VendorGear.Enabled = 0" "AiPlayerbot.VendorGear.MaxLevel = 30"
    "AiPlayerbot.VendorGear.MaxSpendPercent = 50" "AiPlayerbot.VendorGear.ReserveCopper = 0"
    "AiPlayerbot.VendorGear.CooldownSeconds = 600" "AiPlayerbot.VendorGear.Trace = 0")
  require_text("${config_template}" "${line}" "documented key")
endforeach()
