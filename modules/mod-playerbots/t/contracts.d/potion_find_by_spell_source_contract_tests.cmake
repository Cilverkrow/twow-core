# twow-repo#333 T24 POTFIX (OB-00 16.11.2026, bug fix): on this vanilla world data all consumables (item class 0) have subclass 0,
# so FindPotionVisitor ("healing potion" / "mana potion": potion actions, RosterConsumables.UseReal #485, ready check, BetterStacks)
# accepted nothing and InventoryParseItems fell back to a name match ("Recipe: Major Healing Potion" and Craftsman's Writs included,
# sorted first, no CanUseItem). AiPlayerbot.PotionFind.BySpell (default 0; 1 all, 2 cohort B, 3 cohort A) accepts a subclass-0
# consumable whose item spell shares the potion cooldown (category 4) and has the effect (ENERGIZE: mana), PotionFindPolicy.h, unit
# test potion_find_policy; the two lookups then skip the name fallback.
# This contract pins:
#   0. the switch: dist key and modes, member, read with > 3 = off, sorted [ConfigEcho] entry;
#   1. the switch-off path: the old subclass 1/3 loop byte for byte (with its early return), the new branch after it, behind bySpell;
#      bySpell only from PotionFindBySpellApplies (config, then cohort), the one switch read;
#   2. the fact copy from the core spell data into the policy, with the battleground-only attribute and the bot's battleground state
#      (the draughts cast only in a battleground);
#   3. the two lookups in InventoryParseItems return before the name fallback only when the switch applies; the fallback unchanged;
#   4. the policy header: std only, its rule lines, constants equal to the core enums and to IsPotionLike's category 4;
#   5. the unit test registered with its DB fixtures; the other FindPotionVisitor user unchanged;
#   6. negative probes: every mutation must make the contract fail.

cmake_policy(VERSION 3.16)

set(TAG "#333 T24 PotionFind.BySpell")

if(NOT PB_SOURCE_DIR OR NOT PB_MODULE_DIR OR NOT CORE_SOURCE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR, PB_MODULE_DIR and CORE_SOURCE_DIR are required")
endif()

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/strategy/ItemVisitors.h" visitors)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/PlayerbotFactory.cpp" factory)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseConsumableAction.cpp" use_cpp)
read_source("${PB_SOURCE_DIR}/PotionFindPolicy.h" policy)
read_source("${PB_MODULE_DIR}/t/potion_find_policy_tests.cpp" unit)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)
read_source("${CORE_SOURCE_DIR}/src/game/Objects/ItemPrototype.h" core_item)
read_source("${CORE_SOURCE_DIR}/src/game/Spells/SpellDefines.h" core_spell)
read_source("${CORE_SOURCE_DIR}/src/game/SharedDefines.h" core_shared)

# ---- helpers: each one appends to `errors` in the caller's scope -----------------------------------------------------
function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}" PARENT_SCOPE)
  endif()
endfunction()

function(order text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}" PARENT_SCOPE)
  endif()
endfunction()

function(count text needle expected what)
  string(LENGTH "${needle}" needle_len)
  set(n 0)
  string(FIND "${text}" "${needle}" at)
  while(NOT at EQUAL -1)
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${needle_len}")
    string(SUBSTRING "${text}" ${at} -1 text)
    string(FIND "${text}" "${needle}" at)
  endwhile()
  if(NOT n EQUAL expected)
    set(errors "${errors};${what}: expected ${expected}, found ${n}" PARENT_SCOPE)
  endif()
endfunction()

# The text from `first` up to the next `last` (empty when `first` is missing).
function(region text first last out_var)
  set(r "")
  string(FIND "${text}" "${first}" a)
  if(NOT a EQUAL -1)
    string(SUBSTRING "${text}" ${a} -1 rest)
    string(FIND "${rest}" "${last}" b)
    if(NOT b EQUAL -1)
      string(SUBSTRING "${rest}" 0 ${b} r)
    endif()
  endif()
  set(${out_var} "${r}" PARENT_SCOPE)
endfunction()

# Header constant `type name = value;` and the core enum `core_name = value,` in `core_text` (padded with spaces in the core).
function(mirror core_text type name value core_name)
  string(FIND "${policy}" "constexpr ${type} ${name} = ${value};\n" at)
  if(at EQUAL -1)
    set(errors "${errors};policy constant ${name} = ${value}" PARENT_SCOPE)
    return()
  endif()
  if(NOT core_text MATCHES "[ \t]${core_name} *= *${value},")
    set(errors "${errors};core enum ${core_name} = ${value} (mirror ${name})" PARENT_SCOPE)
  endif()
endfunction()

set(dist_key "# Mode: 0 = as before (default, rollback), 1 = every bot, 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid) -\n# the second arm of a crossover unit. Any other value acts like 0.\nAiPlayerbot.PotionFind.BySpell = 0\n")
set(member "    uint32 potionFindBySpell = 0;  // twow-repo#333 T24 POTFIX:")
set(config_read "    potionFindBySpell = config.GetIntDefault(\"AiPlayerbot.PotionFind.BySpell\", 0);\n    if (potionFindBySpell > 3)\n        potionFindBySpell = 0;\n")
set(echo_entry "            {\"PotionFind.BySpell\", Value(potionFindBySpell)},\n")
set(echo_before "            {\"Perf.RouteTimeTrace\", Value(perfRouteTimeTrace)},\n")
set(echo_after "            {\"ProfessionTraining.NearRadius\", Value(professionTrainingNearRadius)},\n")
set(applies "    inline bool PotionFindBySpellApplies(Player* bot)\n    {\n        return sPlayerbotAIConfig.potionFindBySpell && bot && ai::cohort::Applies(sPlayerbotAIConfig.potionFindBySpell, bot->GetGUIDLow());\n    }\n")
set(ctor "        FindPotionVisitor(Player* bot, uint32 effectId) : FindUsableItemVisitor(bot), effectId(effectId), bySpell(PotionFindBySpellApplies(bot)) {}\n")
set(old_loop "            if (proto->Class == ITEM_CLASS_CONSUMABLE && (proto->SubClass == ITEM_SUBCLASS_POTION || proto->SubClass == ITEM_SUBCLASS_FLASK))\n            {\n                for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)\n                {\n                    const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);\n                    if (!spellInfo)\n                        return false;\n\n                    for (int i = 0 ; i < 3; i++)\n                    {\n                        if (spellInfo->Effect[i] == effectId)\n                            return true;\n                    }\n                }\n            }\n")
set(new_branch "            if (bySpell && proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_CONSUMABLE)\n                return AcceptsVanillaPotion(proto);\n\n            return false;\n        }\n")
set(fact_copy "            ai::potion_find::SlotFacts slots[MAX_ITEM_PROTO_SPELLS];\n            for (int j = 0; j < MAX_ITEM_PROTO_SPELLS; j++)\n            {\n                slots[j].itemCategory = proto->Spells[j].SpellCategory;\n                const SpellEntry* const spellInfo = sServerFacade.LookupSpellInfo(proto->Spells[j].SpellId);\n                if (!spellInfo)\n                    continue;\n\n                slots[j].known = true;\n                slots[j].spellCategory = spellInfo->Category;\n                for (int i = 0; i < 3; i++)\n                {\n                    slots[j].effect[i] = spellInfo->Effect[i];\n                    slots[j].misc[i] = spellInfo->EffectMiscValue[i];\n                }\n                slots[j].battlegroundOnly = spellInfo->HasAttribute(SPELL_ATTR_EX3_ONLY_BATTLEGROUNDS);\n            }\n            return ai::potion_find::AcceptsVanillaPotion(proto->Class, proto->SubClass, slots, MAX_ITEM_PROTO_SPELLS, effectId,\n                bot->InBattleGround());\n")
set(early_return "        if (visitor.BySpell())\n        {\n            RETURN_SORT_FOUND;\n        }\n")
set(mana_comment "        // twow-repo#333 T24 POTFIX (AiPlayerbot.PotionFind.BySpell): the visitor decides alone. No name fallback below,\n        // which also returned recipes and Craftsman's Writs (\"Recipe: Major Mana Potion\") and potions the bot cannot use.\n")
set(heal_comment "        // twow-repo#333 T24 POTFIX (AiPlayerbot.PotionFind.BySpell): as for \"mana potion\".\n")
set(mana_lookup "    else if (text == \"mana potion\")\n    {\n        FindPotionVisitor visitor(bot, SPELL_EFFECT_ENERGIZE);\n        VISIT;\n")
set(heal_lookup "    else if (text == \"healing potion\")\n    {\n        FindPotionVisitor visitor(bot, SPELL_EFFECT_HEAL);\n        VISIT;\n")
set(name_fallback "    if (found.size() == 0 && quality == MAX_ITEM_QUALITY && itemClass == MAX_ITEM_CLASS && fromSlot == EQUIPMENT_SLOT_END && outfit.empty())\n    {\n        FindNamedItemVisitor visitor(bot, text);\n        VISIT;\n    }\n")
set(rule_category "    return slot.itemCategory == kCategoryPotion || slot.spellCategory == kCategoryPotion;\n")
set(rule_effect "        if (slot.effect[i] == effectId && (effectId != kEffectEnergize || slot.misc[i] == kPowerMana))\n            return true;\n")
set(rule_class "    if (itemClass != kClassConsumable || subClass != kSubClassConsumable)\n        return false;\n")
set(rule_skip "        if (!slots[j].known || (slots[j].battlegroundOnly && !inBattleground))\n            continue;\n\n        if (InPotionCategory(slots[j]) && HasWantedEffect(slots[j], effectId))\n            return true;\n")

# Every check, on the variables of the caller's scope (the real sources, or one mutated by a probe).
function(check out_var)
  set(errors "")

  # 0. The switch.
  need("${dist}" "${dist_key}" "(0) dist key, default 0, modes 0/1/2/3")
  order("${dist}" "\n#AiPlayerbot.UsePotionChance = 1.0\n" "${dist_key}" "(0) dist key next to UsePotionChance")
  count("${dist}" "\nAiPlayerbot.PotionFind.BySpell = " 1 "(0) dist key once")
  need("${config_h}" "${member}" "(0) member, default 0")
  need("${config_cpp}" "${config_read}" "(0) config read, > 3 = off")
  count("${config_cpp}" "${echo_entry}" 1 "(0) [ConfigEcho] entry")
  order("${config_cpp}" "${echo_before}" "${echo_entry}" "(0) [ConfigEcho] after Perf.RouteTimeTrace")
  order("${config_cpp}" "${echo_entry}" "${echo_after}" "(0) [ConfigEcho] before ProfessionTraining.NearRadius")

  # 1. The off path: the switch is read once (PotionFindBySpellApplies, config first), the old loop unchanged, the new branch after
  #    it and behind bySpell.
  need("${visitors}" "#include \"playerbot/CohortPolicy.h\"\n#include \"playerbot/PotionFindPolicy.h\"\n" "(1) includes")
  need("${visitors}" "${applies}" "(1) PotionFindBySpellApplies: config, then cohort")
  count("${visitors}" "sPlayerbotAIConfig.potionFindBySpell" 2 "(1) switch reads (only PotionFindBySpellApplies)")
  count("${visitors}" "PotionFindBySpellApplies(" 2 "(1) PotionFindBySpellApplies: definition and the constructor")
  need("${visitors}" "${ctor}" "(1) bySpell set once in the constructor")
  need("${visitors}" "        bool BySpell() const { return bySpell; }\n" "(1) BySpell()")
  region("${visitors}" "    class FindPotionVisitor : public FindUsableItemVisitor" "\n    };\n" potion)
  need("${potion}" "${old_loop}" "(1) the old subclass 1/3 loop byte for byte (early return included)")
  need("${potion}" "${new_branch}" "(1) the new branch: bySpell, class 0, subclass 0, then the policy")
  order("${potion}" "${old_loop}" "${new_branch}" "(1) the old loop before the new branch")
  count("${potion}" "bySpell &&" 1 "(1) one gate on bySpell")
  count("${potion}" "sLog" 0 "(1) no log line in the visitor (it runs per bag item)")
  need("${potion}" "        uint32 effectId;\n        bool bySpell;" "(1) members, in init order")

  # 2. The fact copy.
  need("${potion}" "        bool AcceptsVanillaPotion(const ItemPrototype* proto) const\n        {\n${fact_copy}        }\n" "(2) every slot fact copied from the core spell data, a slot without a spell skipped")

  # 3. The lookups: the early return only when the switch applies, after the visit; the name fallback unchanged.
  count("${ai_cpp}" "${early_return}" 2 "(3) the two early returns")
  need("${ai_cpp}" "${mana_lookup}${mana_comment}${early_return}" "(3) mana potion: visit, then return when the switch applies")
  need("${ai_cpp}" "${heal_lookup}${heal_comment}${early_return}" "(3) healing potion: visit, then return when the switch applies")
  need("${ai_cpp}" "${name_fallback}" "(3) the name fallback unchanged")
  count("${ai_cpp}" "potionFindBySpell" 0 "(3) no switch read in PlayerbotAI.cpp (BySpell only)")
  count("${ai_cpp}" "visitor.BySpell()" 2 "(3) BySpell only in the two lookups")

  # 4. The policy header.
  foreach(bad "#include \"" "sLog" "SpellEntry" "sServerFacade" "ItemPrototype const" "ItemPrototype*" "sPlayerbotAIConfig" "Player*")
    count("${policy}" "${bad}" 0 "(4) policy header std only: ${bad}")
  endforeach()
  need("${policy}" "${rule_category}" "(4) rule: potion cooldown category on the slot or the spell")
  need("${policy}" "${rule_effect}" "(4) rule: the effect, ENERGIZE only for mana")
  need("${policy}" "${rule_class}" "(4) rule: class 0, subclass 0 only")
  need("${policy}" "${rule_skip}" "(4) rule: unknown slots skipped, category and effect on the same slot")
  mirror("${core_item}" "std::uint32_t" kClassConsumable 0 ITEM_CLASS_CONSUMABLE)
  mirror("${core_item}" "std::uint32_t" kSubClassConsumable 0 ITEM_SUBCLASS_CONSUMABLE)
  mirror("${core_spell}" "std::uint32_t" kEffectHeal 10 SPELL_EFFECT_HEAL)
  mirror("${core_spell}" "std::uint32_t" kEffectEnergize 30 SPELL_EFFECT_ENERGIZE)
  mirror("${core_shared}" "std::int32_t" kPowerMana 0 POWER_MANA)
  need("${policy}" "constexpr std::uint32_t kCategoryPotion = 4;\n" "(4) potion category 4")
  need("${use_cpp}" "        if (proto->Spells[i].SpellCategory == 4)\n            return true;\n" "(4) IsPotionLike: slot category 4")
  need("${use_cpp}" "        if (spellInfo && spellInfo->Category == 4)\n            return true;\n" "(4) IsPotionLike: spell category 4")

  # 5. The unit test and the other visitor user.
  need("${tests}" "add_executable(potion_find_policy_tests\n  \"\${PB_MODULE_DIR}/t/potion_find_policy_tests.cpp\")\n" "(5) unit test built")
  need("${tests}" "add_test(NAME potion_find_policy\n  COMMAND potion_find_policy_tests\n" "(5) unit test registered")
  foreach(case "Require(Heals(minorHealing) && Heals(healing) && Heals(majorHealing), \"118 / 929 / 13446 are healing potions\");"
      "Require(Mana(minorMana) && Mana(majorMana), \"2455 / 13444 are mana potions\");"
      "Require(!Heals(bandage) && !Heals(bread) && !Heals(water) && !Heals(elixirOfFortitude) && !Heals(flaskOfTheTitans),"
      "Require(!Heals(Slot(1153, 30, kHeal)), \"healthstone: own action, own cooldown\");"
      "Require(!Mana(Slot(4, 4, kEnergize, 0, 0, 1)), \"Rage Potion: rage, not mana\");"
      "Require(AcceptsVanillaPotion(0, 0, slots, 5, kHeal, false), \"empty slot 1, potion in slot 2\");"
      "Require(!AcceptsVanillaPotion(0, 0, heal, 5, kHeal, false), \"17348 draught outside a battleground: no potion\");"
      "Require(AcceptsVanillaPotion(0, 0, heal, 5, kHeal, true), \"17348 draught in a battleground: a healing potion\");")
    need("${unit}" "${case}" "(5) unit test case")
  endforeach()
  need("${factory}" "        FindPotionVisitor visitor(bot, effect);\n        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);\n" "(5) InitPotions unchanged")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check(errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "${TAG}:${errs}")
endif()

# ---- 6. negative probes ----------------------------------------------------------------------------------------------
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "${TAG} probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check(perrs)
  if(NOT perrs)
    message(FATAL_ERROR "${TAG} probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()

probe("default on" dist "\nAiPlayerbot.PotionFind.BySpell = 0\n" "\nAiPlayerbot.PotionFind.BySpell = 1\n")
probe("member default on" config_h "    uint32 potionFindBySpell = 0;" "    uint32 potionFindBySpell = 1;")
probe("read default on" config_cpp "\"AiPlayerbot.PotionFind.BySpell\", 0);" "\"AiPlayerbot.PotionFind.BySpell\", 1);")
probe("no clamp" config_cpp "    if (potionFindBySpell > 3)\n" "    if (potionFindBySpell > 4)\n")
probe("echo missing" config_cpp "${echo_entry}" "")
probe("echo out of order" config_cpp "${echo_before}${echo_entry}" "${echo_entry}${echo_before}")
probe("switch without cohort" visitors "return sPlayerbotAIConfig.potionFindBySpell && bot && ai::cohort::Applies(sPlayerbotAIConfig.potionFindBySpell, bot->GetGUIDLow());" "return sPlayerbotAIConfig.potionFindBySpell;")
probe("new branch without the switch" visitors "            if (bySpell && proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_CONSUMABLE)\n" "            if (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_CONSUMABLE)\n")
probe("new branch for every subclass" visitors " && proto->SubClass == ITEM_SUBCLASS_CONSUMABLE)\n                return AcceptsVanillaPotion(proto);" ")\n                return AcceptsVanillaPotion(proto);")
probe("old early return changed" visitors "                    if (!spellInfo)\n                        return false;\n" "                    if (!spellInfo)\n                        continue;\n")
probe("old loop widened to subclass 0" visitors "(proto->SubClass == ITEM_SUBCLASS_POTION || proto->SubClass == ITEM_SUBCLASS_FLASK)" "(proto->SubClass == ITEM_SUBCLASS_POTION || proto->SubClass == ITEM_SUBCLASS_FLASK || proto->SubClass == ITEM_SUBCLASS_CONSUMABLE)")
probe("misc fact dropped" visitors "                    slots[j].misc[i] = spellInfo->EffectMiscValue[i];\n" "")
probe("slot category 0" visitors "                slots[j].itemCategory = proto->Spells[j].SpellCategory;\n" "                slots[j].itemCategory = 0;\n")
probe("spell category dropped" visitors "                slots[j].spellCategory = spellInfo->Category;\n" "")
probe("fallback for every bot" ai_cpp "${heal_lookup}${heal_comment}${early_return}" "${heal_lookup}        {\n            RETURN_SORT_FOUND;\n        }\n")
probe("mana early return dropped" ai_cpp "${mana_lookup}${mana_comment}${early_return}" "${mana_lookup}${mana_comment}")
probe("name fallback changed" ai_cpp "        FindNamedItemVisitor visitor(bot, text);\n" "        FindNamedItemVisitor visitor(bot, text + \" \");\n")
probe("rage counts as mana" policy "(effectId != kEffectEnergize || slot.misc[i] == kPowerMana)" "(true)")
probe("any category" policy "${rule_category}" "    return true;\n")
probe("unknown slot stops" policy "        if (!slots[j].known || (slots[j].battlegroundOnly && !inBattleground))\n            continue;\n" "        if (!slots[j].known)\n            return false;\n")
probe("battleground check dropped" policy " || (slots[j].battlegroundOnly && !inBattleground))\n" ")\n")
probe("battleground fact dropped" visitors "                slots[j].battlegroundOnly = spellInfo->HasAttribute(SPELL_ATTR_EX3_ONLY_BATTLEGROUNDS);\n" "")
probe("battleground state constant" visitors "                bot->InBattleGround());\n" "                true);\n")
probe("game include in the policy" policy "#include <cstdint>\n" "#include <cstdint>\n#include \"playerbot/ServerFacade.h\"\n")
probe("wrong mirror" policy "constexpr std::uint32_t kEffectEnergize = 30;\n" "constexpr std::uint32_t kEffectEnergize = 31;\n")
probe("unit test case dropped" unit "Require(!Heals(Slot(1153, 30, kHeal)), \"healthstone: own action, own cooldown\");" "")

message(STATUS "${TAG} source contract passed (${probes} negative probes caught)")
