# twow-repo#333 Berufe P1-s (CLI-BERUFE 26.10.2026, OB-00 go): AiPlayerbot.ThrownUsageFix, modes 0 / 1 / 2, default 0 (rollback),
# ConfigEcho. A thrown weapon sits in the ranged slot, never in the ammo slot; the ammo rule of ItemUsageValue compared it
# with PLAYER_AMMO_ID (always 0) and called every thrown weapon in the bags or at a vendor "equip" - bags full of Crude
# Throwing Axes, never sold or destroyed, bought again, the same axe re-equipped 286 times (cr2-C, 5 rogues).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 ThrownUsageFix: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#333 ThrownUsageFix: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 ThrownUsageFix: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("AmmoStockPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/ItemUsageValue.cpp" usage)

# Switch: a mode, default 0, unknown -> 0, documented, echoed.
require_text("${config_h}" "uint32 thrownUsageFix = 0;" "default off, a mode")
require_text("${config_cpp}" "thrownUsageFix = config.GetIntDefault(\"AiPlayerbot.ThrownUsageFix\", 0);" "config read")
require_text("${config_cpp}" "if (thrownUsageFix > 2)\n        thrownUsageFix = 0;" "unknown mode is off")
require_text("${config_cpp}" "{\"ThrownUsageFix\", Value(thrownUsageFix)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.ThrownUsageFix = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "\nAiPlayerbot.ThrownUsageFix = 1" "default 1 in the dist")
forbid_text("${conf_dist}" "\nAiPlayerbot.ThrownUsageFix = 2" "default 2 in the dist")

# Pure rules: a single thrown weapon leaves the ammo rule; a stacking one compares with the ranged slot.
forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "return thrownFix && stackSize <= 1;" "single thrown weapon leaves the ammo rule")
require_text("${policy}" "return thrownFix ? rangedEntry : ammoSlotEntry;" "the ranged slot for thrown weapons")

# ItemUsageValue: only thrown weapons (ammoClass weapon), the cohort first, the skip in the ammo condition, the ranged
# item as the current ammo, before both EQUIP returns of the ammo rule.
require_text("${usage}" "#include \"playerbot/CohortPolicy.h\"" "cohort include")
require_text("${usage}" "bool const thrownFix = ammoClass == ITEM_CLASS_WEAPON &&\n                ai::cohort::Applies(sPlayerbotAIConfig.thrownUsageFix, bot->GetGUIDLow());" "thrown weapons only, mode and cohort")
require_text("${usage}" "if (proto->Class == ammoClass && proto->SubClass == subClass &&\n                !ammo_stock::ThrownSkipsAmmoRule(thrownFix, proto->GetMaxStackSize()))" "the skip in the ammo condition")
require_text("${usage}" "uint32 currentAmmoId = ammo_stock::CurrentAmmoEntry(thrownFix, bot->GetUInt32Value(PLAYER_AMMO_ID), pItem->GetEntry());" "the ranged item as the current ammo")
require_order("${usage}" "uint32 currentAmmoId = ammo_stock::CurrentAmmoEntry(" "// fallback: equip any ammo if no ammo equipped" "the current ammo before the EQUIP fallback")
require_order("${usage}" "ItemUsage equip = QueryItemUsageForEquip(itemQualifier, bot);\n    if (equip != ItemUsage::ITEM_USAGE_NONE)\n        return equip;" "bool const thrownFix = ammoClass == ITEM_CLASS_WEAPON" "upgrades stay with the equip rule, before the ammo rule")
forbid_text("${usage}" "uint32 currentAmmoId = bot->GetUInt32Value(PLAYER_AMMO_ID);" "the ammo slot read for every ammo class")

message(STATUS "#333 ThrownUsageFix source contract passed")
