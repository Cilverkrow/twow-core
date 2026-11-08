# twow-repo#333 M2 Crafting.Consumables (owner list 07.11.2026, item 1; M2-ENTWURF §7; inventory A3-AKTIONS-VERDRAHTUNG §4:
# the "consumables" strategy was in no default list, oil and stone USELESS without one in the bags). AiPlayerbot.Crafting.Consumables
# (code default 0; 1 all, 2 cohort B, 3 cohort A): a roster bot on its own gets the non-combat strategy "consumables" and its item
# usage keeps the buff consumables of "use consumable" and the stone / oil for its main hand up to RosterConsumables.KeepStacks.
# No effect under the item cheat. Proof lines "[Crafting] kind=consumables event=use|imbue".
# This contract pins the switch (dist key, member, read with > 3 = off, sorted [ConfigEcho] entry), the one helper, the strategy
# place, the keep block's place and conditions, the proof lines and the stone / oil id read before the use.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Consumables: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 M2 Crafting.Consumables: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Consumables: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#333 M2 Crafting.Consumables: region end not found after '${first}': '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#333 M2 Crafting.Consumables: ${description}: expected ${expected} of '${needle}', found ${count}")
  endif()
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/AiFactory.cpp" factory)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.h" pu_h)
read_source("${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" pu_cpp)
read_source("${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseConsumableAction.h" use_h)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseConsumableAction.cpp" use_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/ImbueAction.cpp" imbue)

# 0. The switch: default 0, modes 0..3, unknown = 0, sorted [ConfigEcho] entry, documented (cheat, lines).
require_text("${dist}" "\nAiPlayerbot.Crafting.Consumables = 0\n" "dist key, default 0")
require_text("${dist}" "No effect under the item cheat (RndBotCheats item)" "dist: no effect under the item cheat")
require_text("${dist}" "#   [Crafting] kind=consumables event=use type=flask|elixir|scroll|food_buff|alcohol item= bot= level= cohort=" "dist: use line")
require_text("${dist}" "#   [Crafting] kind=consumables event=imbue type=stone|oil slot=mainhand|offhand item= bot= level= cohort=" "dist: imbue line")
require_text("${config_h}" "uint32 craftingConsumables = 0;" "config member, default 0")
require_text("${config_cpp}" "craftingConsumables = config.GetIntDefault(\"AiPlayerbot.Crafting.Consumables\", 0);\n    if (craftingConsumables > 3)\n    {\n        sLog.outError(\"[Crafting] config key=Consumables value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", craftingConsumables);\n        craftingConsumables = 0;\n    }" "config read, > 3 = off")
# Later Crafting.* keys (Cooking, Survival, Fishing) sort between this entry and CustomRaceStartZones; CombatRule.* (A3 step 5)
# sorts between CheatItem.Trace and this entry. Both neighbours are order checks.
require_text("${config_cpp}" "            {\"Crafting.Consumables\", Value(craftingConsumables)},\n" "parsed mode in [ConfigEcho]")
require_order("${config_cpp}" "            {\"CheatItem.Trace\", Value(cheatItemTrace)},\n" "            {\"Crafting.Consumables\", Value(craftingConsumables)},\n" "[ConfigEcho]: CheatItem.Trace before Crafting.Consumables")
require_order("${config_cpp}" "{\"Crafting.Consumables\", Value(craftingConsumables)}," "{\"CustomRaceStartZones\"" "parsed mode in [ConfigEcho], sorted before CustomRaceStartZones")

# 1. One helper: config first (the default costs nothing), roster bot on its own, cohort.
require_text("${pu_h}" "bool CraftingConsumablesFor(PlayerbotAI* ai);" "(1) helper declared")
between("${pu_cpp}" "bool ai::CraftingConsumablesFor(PlayerbotAI* ai)" "\n}\n" helper)
require_text("${helper}" "    if (!sPlayerbotAIConfig.craftingConsumables)\n        return false;" "(1) config check first")
require_text("${helper}" "return bot && IsRosterBotOnItsOwn(ai) && ai::cohort::Applies(sPlayerbotAIConfig.craftingConsumables, bot->GetGUIDLow());" "(1) roster bot on its own, cohort")

# 2. The strategy: non-combat only, not in a battleground, after the ad-hoc group block and before the battleground switch.
set(add "    if (!player->InBattleGround() && ai::CraftingConsumablesFor(facade))\n        nonCombatEngine->addStrategy(\"consumables\");")
require_text("${factory}" "${add}" "(2) consumables strategy for the switch's bots")
require_order("${factory}" "nonCombatEngine->addStrategy(\"adhoc group\");" "${add}" "(2) after the ad-hoc group block")
require_order("${factory}" "${add}" "nonCombatEngine->addStrategies(\"racials\", \"nc\", \"default\", \"buff\", \"food\"" "(2) before the non-combat battleground switch")
count_text("${factory}" "addStrategy(\"consumables\")" 1 "(2) added in one place only")

# 3. The keep block: after the RosterConsumables.UseReal stock, before the legacy consumable block; config first, no cheat, the
#    helper, level and KeepStacks; the main-hand stone / oil and the classification of use consumable; KEEP only (no buying).
set(keep_head "    if (sPlayerbotAIConfig.craftingConsumables && !ai->HasCheat(BotCheatMask::item) && CraftingConsumablesFor(ai) &&\n        proto->RequiredLevel <= bot->GetLevel() && CurrentStacks(ai, proto) <= sPlayerbotAIConfig.rosterConsumablesKeepStacks)")
require_text("${usage}" "${keep_head}" "(3) keep block conditions")
require_order("${usage}" "sPlayerbotAIConfig.rosterConsumablesUseReal && IsRosterBotOnItsOwn(ai)" "${keep_head}" "(3) after the UseReal stock")
require_order("${usage}" "${keep_head}" "if (proto->Class == ITEM_CLASS_CONSUMABLE && !ai->HasCheat(BotCheatMask::item))" "(3) before the legacy consumable block")
between("${usage}" "${keep_head}" "if (proto->Class == ITEM_CLASS_CONSUMABLE && !ai->HasCheat(BotCheatMask::item))" keep)
require_text("${keep}" "if (mainhand && ai->IsImbueItemFor(mainhand, proto))\n            return ItemUsage::ITEM_USAGE_KEEP;" "(3) stone / oil for the main hand")
require_text("${keep}" "dynamic_cast<UseConsumableAction*>(ai->GetAiObjectContext()->GetAction(\"use consumable\"))" "(3) the action's own classification")
require_text("${keep}" "if (use && use->IsBuffConsumable(proto))\n                return ItemUsage::ITEM_USAGE_KEEP;" "(3) buff consumable kept")
count_text("${keep}" "ITEM_USAGE_USE" 0 "(3) no buying from the keep block")
count_text("${usage}" "craftingConsumables" 1 "(3) one switch read in ItemUsageValue.cpp")

# 4. use consumable: still useless under the item cheat; IsBuffConsumable = the action's classification; the use line after a use.
require_text("${use_cpp}" "    if (ai->HasCheat(BotCheatMask::item))\n        return false;" "(4) useless under the item cheat")
require_text("${use_h}" "bool IsBuffConsumable(ItemPrototype const* proto) const;" "(4) public classification")
between("${use_cpp}" "bool UseConsumableAction::IsBuffConsumable(ItemPrototype const* proto) const" "\n}\n" isbuff)
require_text("${isbuff}" "return proto && bot->CanUseItem(proto) == EQUIP_ERR_OK && ClassifyConsumable(proto, type, spellId);" "(4) same classification, usable")
require_text("${use_cpp}" "    bool const used = UseItemIdAction::Execute(event);\n" "(4) the use first")
require_text("${use_cpp}" "    if (used && CraftingConsumablesFor(ai))\n        sLog.outBasic(\"[Crafting] kind=consumables event=use type=%s item=%u bot=%u level=%u cohort=%c\", ConsumableTypeName(selectedType),\n            selectedItemId, bot->GetGUIDLow(), bot->GetLevel(), ai::cohort::Letter(bot->GetGUIDLow()));\n    return used;" "(4) use line, switch's bots only")
require_text("${use_cpp}" "            selectedType = c.type;\n            break;" "(4) the type of the chosen item")

# 5. Stone and oil: one line per started use on the three use sites, the item id read before the use (the last one may be gone).
require_text("${imbue}" "sLog.outBasic(\"[Crafting] kind=consumables event=imbue type=%s slot=%s item=%u bot=%u level=%u cohort=%c\"" "(5) imbue line")
require_text("${imbue}" "if (used && CraftingConsumablesFor(ai))" "(5) switch's bots only")
count_text("${imbue}" "return NoteCraftingImbue(ai, UseItem(requester, " 3 "(5) three use sites")
count_text("${imbue}" "->GetEntry(), " 0 "(5) no entry read inside the use call")
require_text("${imbue}" "uint32 const stoneId = stone->GetEntry();" "(5) stone id before the use")
require_text("${imbue}" "uint32 const oilId = oil->GetEntry();" "(5) oil id before the use")

# 6. IsImbueItemFor: the same display id lists and checks as FindConsumable / FindStoneFor / FindOilFor, without the bags.
require_text("${ai_h}" "bool IsImbueItemFor(Item* weapon, ItemPrototype const* proto) const;" "(6) declared")
between("${ai_cpp}" "bool PlayerbotAI::IsImbueItemFor(Item* weapon, ItemPrototype const* proto) const" "\n}\n" imbue_for)
require_text("${imbue_for}" "(proto->Class != ITEM_CLASS_CONSUMABLE && proto->Class != ITEM_CLASS_TRADE_GOODS) ||\n        bot->CanUseItem(proto) != EQUIP_ERR_OK" "(6) FindConsumable's class and use checks")
require_text("${imbue_for}" "uint32 const displayId = proto->DisplayInfoID;" "(6) display ids, as the lists")
foreach(list uPriorizedSharpStoneIds uPriorizedWeightStoneIds uPriorizedWizardOilIds uPriorizedManaOilIds)
  require_text("${imbue_for}" "listed(${list}, countof(${list}))" "(6) list ${list}")
endforeach()
count_text("${imbue_for}" "FindConsumable(" 0 "(6) no bag search")
