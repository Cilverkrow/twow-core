if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#485 (owner decision 9): roster bots on their own use bandages, healing and mana potions
# from their bags - no item cheat consumption, a small stock kept instead of sold, optional vendor
# purchase, a cloth reserve for Tailoring. RosterConsumables.UseReal = 0 keeps the legacy path.

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(reject_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Text from start_marker up to end_marker (both must exist, in that order).
function(text_between text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "Missing section start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(FIND "${rest}" "${end_marker}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "Missing section end after ${start_marker}: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end_at} section)
  set(${out} "${section}" PARENT_SCOPE)
endfunction()

# The cheat-free gate of a section: the cheat test must be ORed with the roster switch, so a roster
# bot under UseReal needs the item. Returns "" when the gate is missing.
function(find_real_item_gate section out)
  string(FIND "${section}" "(!ai->HasCheat(BotCheatMask::item) || UsesRealConsumable(ai, proto)) && !bot->HasItemCount(itemId, 1)" at)
  if(at EQUAL -1)
    set(${out} "" PARENT_SCOPE)
  else()
    set(${out} "found" PARENT_SCOPE)
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/ConsumablesPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/UseItemAction.h" use_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/UseItemAction.cpp" use_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" triggers)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# 1. Pure policy: std only, included only by the .cpp files that use it.
require_text("${policy}" "namespace ai::consumables" "policy namespace")
reject_text("${policy}" "#include \"" "engine include in the pure policy")
reject_text("${use_h}" "#include \"playerbot/ConsumablesPolicy.h\"" "policy include in a header")
require_text("${use_cpp}" "#include \"playerbot/ConsumablesPolicy.h\"" "policy include in UseItemAction.cpp")
require_text("${usage}" "#include \"playerbot/ConsumablesPolicy.h\"" "policy include in ItemUsageValue.cpp")
require_text("${triggers}" "#include \"playerbot/ConsumablesPolicy.h\"" "policy include in ProfessionUseTriggers.cpp")

# 2. Switch: roster bot on its own only, config check first (the default costs nothing).
require_text("${use_cpp}" "return sPlayerbotAIConfig.rosterConsumablesUseReal && IsRosterBotOnItsOwn(ai);" "UsesRealConsumables gate")
require_text("${use_cpp}" "return proto && sPlayerbotAIConfig.rosterConsumablesUseReal &&" "UsesRealConsumable config check first")

# 3. No cheat consumption: the item is required and consumed, a missing one makes the action impossible.
text_between("${use_cpp}" "bool RequiresItemToUse(" "// Exception items" requires_section)
require_order("${requires_section}" "if (!ai->HasCheat(BotCheatMask::item))" "if (UsesRealConsumable(ai, itemProto))" "item required for real consumables under the cheat")
text_between("${use_cpp}" "bool UseItemIdAction::isPossible()" "uint32 spellCount = 0;" possible_section)
find_real_item_gate("${possible_section}" gate_found)
if(gate_found STREQUAL "")
  message(FATAL_ERROR "Missing cheat-free item gate in UseItemIdAction::isPossible")
endif()
text_between("${use_h}" "class UsePotionAction" "class UseHealingPotionAction" potion_section)
require_order("${potion_section}" "if (UsesRealConsumables(ai))" "return sRandomItemMgr.GetRandomPotion(bot->GetLevel(), effect);" "no random cheat potion under UseReal")
text_between("${use_h}" "class UseBandageAction" "class ThrowGrenadeAction" bandage_section)
require_order("${bandage_section}" "return BestBandageInBags(ai);" "int firstAidSkillValue = bot->GetSkillValue(129);" "bandage from the bags before the legacy id")
require_text("${bandage_section}" "if (bot->HasAura(11196))" "Recently Bandaged respected")
require_text("${bandage_section}" "AI_VALUE(uint8, \"my attacker count\") > 0" "no bandage under attack")

# 4. Thresholds and trace.
text_between("${use_cpp}" "bool UsePotionAction::isUseful()" "bool UseSpellItemAction::isUseful()" useful_section)
require_text("${useful_section}" "AI_VALUE2(bool, \"combat\", \"self target\")" "potions stay in combat")
require_text("${useful_section}" "sPlayerbotAIConfig.rosterConsumablesHealingPotionPct" "healing potion threshold")
require_text("${useful_section}" "sPlayerbotAIConfig.rosterConsumablesManaPotionPct" "mana potion threshold")
require_text("${use_cpp}" "[Consumable] used=%s item=%u hp_pct=%u" "[Consumable] trace")
require_text("${use_cpp}" "if (!sPlayerbotAIConfig.rosterConsumablesTrace)" "trace switch")
require_text("${use_cpp}" "consumables::TraceDue(now, AI_VALUE2(time_t, \"manual time\", timeKey), sPlayerbotAIConfig.rosterConsumablesTraceCooldownSeconds)" "trace throttle")
require_text("${use_cpp}" "if (successCasts > 0 && itemUsed && UsesRealConsumable(ai, proto))" "trace only for a real item")

# 5. Stock: kept instead of sold, bought only with Buy; before the legacy (non-cheat) block.
require_order("${usage}" "sPlayerbotAIConfig.rosterConsumablesUseReal && IsRosterBotOnItsOwn(ai)" "if (proto->Class == ITEM_CLASS_CONSUMABLE && !ai->HasCheat(BotCheatMask::item))" "roster stock before the legacy consumable block")
require_text("${usage}" "sPlayerbotAIConfig.rosterConsumablesKeepStacks, sPlayerbotAIConfig.rosterConsumablesBuy" "stock keys")
require_text("${policy}" "return keepStacks < 2 ? 2.0f : float(keepStacks);" "keep limit of at least two stacks (no buy/sell loop)")

# 6. Cloth reserve, inside CraftableFromBags only (not the 8.20 recipe ranking).
text_between("${triggers}" "uint32 ai::CraftableFromBags(" "return craftable;" craftable_section)
require_text("${craftable_section}" "sPlayerbotAIConfig.rosterConsumablesTailoringClothReserve" "cloth reserve key")
require_text("${craftable_section}" "bot->HasSkill(SKILL_TAILORING)" "only for tailors")
require_text("${craftable_section}" "reagent->Class == ITEM_CLASS_TRADE_GOODS" "only cloth / trade goods")
require_text("${triggers}" "itr->second->skillId == SKILL_FIRST_AID" "only First Aid recipes")

# 7. Config: neutral defaults, documented in the template.
foreach(entry
    "rosterConsumablesUseReal = config.GetBoolDefault(\"AiPlayerbot.RosterConsumables.UseReal\", false)"
    "rosterConsumablesHealingPotionPct = config.GetIntDefault(\"AiPlayerbot.RosterConsumables.HealingPotionPct\", 100)"
    "rosterConsumablesManaPotionPct = config.GetIntDefault(\"AiPlayerbot.RosterConsumables.ManaPotionPct\", 100)"
    "rosterConsumablesKeepStacks = config.GetIntDefault(\"AiPlayerbot.RosterConsumables.KeepStacks\", 2)"
    "rosterConsumablesBuy = config.GetBoolDefault(\"AiPlayerbot.RosterConsumables.Buy\", false)"
    "rosterConsumablesTailoringClothReserve = config.GetIntDefault(\"AiPlayerbot.RosterConsumables.TailoringClothReserve\", 0)"
    "rosterConsumablesTrace = config.GetBoolDefault(\"AiPlayerbot.RosterConsumables.Trace\", false)"
    "rosterConsumablesTraceCooldownSeconds = config.GetIntDefault(\"AiPlayerbot.RosterConsumables.TraceCooldownSeconds\", 300)")
  require_text("${config_source}" "${entry}" "neutral config default")
endforeach()
foreach(key UseReal HealingPotionPct ManaPotionPct KeepStacks Buy TailoringClothReserve Trace TraceCooldownSeconds)
  require_text("${config_template}" "AiPlayerbot.RosterConsumables.${key} = " "documented key ${key}")
endforeach()
require_text("${config_template}" "AiPlayerbot.RosterConsumables.UseReal = 0" "UseReal off by default")
require_text("${config_header}" "bool rosterConsumablesUseReal;" "config member")

# 8. OB-10 lane: crafting pick, crafting trip and material memory are not touched here.
string(REPLACE "${craftable_section}" "" triggers_outside "${triggers}")
reject_text("${triggers_outside}" "rosterConsumables" "consumables switch in the 8.20 recipe ranking")

# Negative probe: the gate scan must miss the legacy (cheat) gate.
find_real_item_gate("        if (!ai->HasCheat(BotCheatMask::item) && !bot->HasItemCount(itemId, 1))" probe_found)
if(NOT probe_found STREQUAL "")
  message(FATAL_ERROR "negative probe not caught: the legacy cheat gate passed as the UseReal gate - the scan is broken")
endif()
