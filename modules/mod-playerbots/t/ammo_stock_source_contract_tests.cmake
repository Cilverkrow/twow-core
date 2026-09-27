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

file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
file(READ "${PB_SOURCE_DIR}/strategy/actions/BuyAction.cpp" buy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# OB-10 train 6 (142 Crude Throwing Axes) and owner decision 2026-09-27:
# the ammo need is a count from the policy, the cheat no longer overrides it.
require_text("${usage}" "float(AmmoTargetCount(ai, proto))" "count-based ammo need")
require_text("${usage}" "ammo_stock::TargetCount(settings, facts)" "policy decides the target")
require_text("${usage}" "facts.quiverCapacity +=" "quiver capacity")
forbid_text("${usage}" "(bot->getClass() == CLASS_HUNTER) ? 8 : 2" "uncapped 8/2 stacks rule")
forbid_text("${usage}" "needAmmo = 1;" "item cheat overriding the cap")

# Missing ammo is bought in one transaction, not one item per call.
require_text("${buy}" "ammo_stock::PurchaseUnits(ItemUsageValue::AmmoTargetCount(ai, proto)" "bundled purchase")
require_text("${buy}" "BuyItemFromVendor(vendorguid, itemId, uint8(units)" "units passed to the vendor")
require_text("${buy}" "RESET_AI_VALUE2(std::list<Item*>, \"inventory items\", \"ammo\")" "fresh ammo list after a purchase")

# Neutral dist defaults; the owner's values belong in the server profile.
require_text("${config_source}" "\"AiPlayerbot.HunterAmmoTiers\", \"\"" "tiers default empty")
require_text("${config_source}" "\"AiPlayerbot.HunterAmmoFillQuiver\", false" "quiver fill default off")
require_text("${config_source}" "\"AiPlayerbot.ThrownMaxCount\", 0" "thrown cap default 0")
require_text("${config_template}" "AiPlayerbot.HunterAmmoTiers =\n" "documented tiers")
require_text("${config_template}" "AiPlayerbot.HunterAmmoFillQuiver = 0" "documented quiver fill")
require_text("${config_template}" "AiPlayerbot.ThrownMaxCount = 0" "documented thrown cap")
forbid_text("${config_template}" "AiPlayerbot.AmmoMaxStacks" "obsolete stack cap")
forbid_text("${config_template}" "AiPlayerbot.ThrownMaxStacks" "obsolete stack cap")
