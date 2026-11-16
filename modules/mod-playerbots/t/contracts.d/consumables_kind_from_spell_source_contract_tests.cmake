# twow-repo#333 T24 CLSFIX (OB-00 16.11.2026, bug fix): on this vanilla world data all 1587 consumables (item class 0) have
# subclass 0, so the potion / elixir / flask / scroll branches of UseConsumableAction::ClassifyConsumable never ran ("use
# consumable" knew only buff food and one alcohol). AiPlayerbot.Consumables.KindFromSpell (default 0; 1 all, 2 cohort B, 3 cohort A)
# derives the subclass of a subclass-0 consumable from its item spells (ConsumableKindPolicy.h, unit test
# consumable_kind_policy: Flask of the Titans -> flask, Elixir of Fortitude -> elixir, Scroll of Strength II -> scroll, healing
# potions -> potion) and runs the unchanged branches with it.
# This contract pins:
#   0. the switch: dist key, member, read with > 3 = off, sorted [ConfigEcho] entry; read in the action only by KindFromSpellFor;
#   1. ClassifyConsumable: every subclass test reads the derived subclass, nothing else of the function changed;
#   2. the off path: switch (config, then cohort) first, then subclass 0, else proto->SubClass unchanged; every copied spell fact;
#   3. knownFlaskAuras unchanged (the one flask list, also for the derived flask);
#   4. the policy header stays std only and its constants match the core enums;
#   5. the keep block in ItemUsageValue.cpp does not read the switch; the unit test keeps the four contract cases;
#   6. the review guards (switch on only): Execute skips a candidate a more powerful active spell would refuse and one a quest in the
#      log asks for; ScoreSpellStats gives no score to a pool of a power the bot does not have; Execute skips a candidate whose
#      every effect is a mana effect for a bot without mana (lead 16.11.: Flask of Distilled Wisdom for warriors and rogues);
#   7. negative probes: every mutation must make the contract fail.

cmake_policy(VERSION 3.16)

set(TAG "#333 T24 Consumables.KindFromSpell")

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
read_source("${PB_SOURCE_DIR}/strategy/actions/UseConsumableAction.h" use_h)
read_source("${PB_SOURCE_DIR}/strategy/actions/UseConsumableAction.cpp" use_cpp)
read_source("${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" usage)
read_source("${PB_SOURCE_DIR}/ConsumableKindPolicy.h" policy)
read_source("${PB_MODULE_DIR}/t/consumable_kind_policy_tests.cpp" unit)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)
read_source("${CORE_SOURCE_DIR}/src/game/Objects/ItemPrototype.h" core_item)
read_source("${CORE_SOURCE_DIR}/src/game/Database/DBCEnums.h" core_dbc)
read_source("${CORE_SOURCE_DIR}/src/game/Spells/SpellDefines.h" core_spell)
read_source("${CORE_SOURCE_DIR}/src/game/Spells/SpellAuraDefines.h" core_aura)

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

# Header constant `name` = `value` and the core enum `core_name` = the same value in `core_text`.
function(mirror core_text name value core_name)
  string(FIND "${policy}" "constexpr std::uint32_t ${name} = ${value};\n" at)
  if(at EQUAL -1)
    set(errors "${errors};policy constant ${name} = ${value}" PARENT_SCOPE)
    return()
  endif()
  if(NOT core_text MATCHES "[ \t]${core_name} *= *${value},")
    set(errors "${errors};core enum ${core_name} = ${value} (mirror ${name})" PARENT_SCOPE)
  endif()
endfunction()

set(kind_for "bool UseConsumableAction::KindFromSpellFor() const\n{\n    return sPlayerbotAIConfig.consumablesKindFromSpell &&\n        ai::cohort::Applies(sPlayerbotAIConfig.consumablesKindFromSpell, bot->GetGUIDLow());\n}\n")
set(off_path "uint32 UseConsumableAction::ConsumableSubClass(ItemPrototype const* proto) const\n{\n    if (!KindFromSpellFor() || proto->SubClass != ITEM_SUBCLASS_CONSUMABLE)\n        return proto->SubClass;\n")
set(facts_glue "        facts.known = true;\n        facts.family = spellInfo->SpellFamilyName;\n        facts.category = spellInfo->Category;\n        facts.durationIndex = spellInfo->DurationIndex;\n        facts.targetA0 = spellInfo->EffectImplicitTargetA[0];\n        for (int j = 0; j < 3; ++j)\n        {\n            facts.effect[j] = spellInfo->Effect[j];\n            facts.aura[j] = spellInfo->EffectApplyAuraName[j];\n        }\n")
set(slot_glue "        uint32 const spellId = proto->Spells[i].SpellId;\n        if (!spellId)\n            continue;\n\n        ai::consumable_kind::ItemSpell& slot = spells[n++];\n        slot.spellId = spellId;\n        slot.itemCategory = proto->Spells[i].SpellCategory;\n        SpellEntry const* spellInfo = fill(spellId, slot.spell);\n        if (!spellInfo)\n            continue;\n")
set(trigger_glue "        for (int j = 0; j < 3; ++j)\n        {\n            uint32 const trigger = spellInfo->EffectTriggerSpell[j];\n            if (trigger && trigger != spellId)\n                fill(trigger, slot.trigger[j]);\n        }\n")
set(guard_line "        if (kindFromSpell && (IsCandidateOutranked(c) || IsQuestDeliveryItem(c.item->GetProto()->ItemId) ||\n            IsManaOnlyWithoutManaPool(c.spellId)))\n            continue;\n")
set(mana_pool_test "    if (bot->GetMaxPower(POWER_MANA))\n        return false;\n")
set(mana_effect "        bool const manaEffect = (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_ENERGY ||\n            spellInfo->Effect[i] == SPELL_EFFECT_ENERGIZE) && spellInfo->EffectMiscValue[i] == int32(POWER_MANA);\n        if (!manaEffect)\n            return false;\n")
set(mana_amount "        if (spellInfo->EffectBasePoints[i] > 0)\n            manaAmount = true;\n    }\n\n    return manaAmount;")
set(mana_guard "        if (kindFromSpell && spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_ENERGY &&\n            (spellInfo->EffectMiscValue[i] < 0 || spellInfo->EffectMiscValue[i] >= MAX_POWERS ||\n             !bot->GetMaxPower(Powers(spellInfo->EffectMiscValue[i]))))\n            continue;\n")
set(null_check "    if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE)\n        return false;\n")
set(subclass_line "    uint32 const subClass = ConsumableSubClass(proto);\n")
set(echo_entry "            {\"Consumables.KindFromSpell\", Value(consumablesKindFromSpell)},\n")
set(echo_before "            {\"CombatRule.TargetRule\", Value(combatRuleTargetRule)},\n")
set(echo_after "            {\"Crafting.Consumables\", Value(craftingConsumables)},\n")
set(flask_list "const std::vector<uint32> UseConsumableAction::knownFlaskAuras = {\n    17626,  // Flask of the Titans\n    17627,  // Flask of Distilled Wisdom\n    17628,  // Flask of Supreme Power\n    17629,  // Flask of Chromatic Resistance\n    17624,  // Flask of Petrification\n};\n")

# Every check, on the variables of the caller's scope (the real sources, or one mutated by a probe).
function(check out_var)
  set(errors "")

  # 0. The switch.
  need("${dist}" "#   0 = off (default), 1 = every bot, 2 = only cohort B (odd low guid), 3 = only cohort A (even low guid); other values act like 0.\nAiPlayerbot.Consumables.KindFromSpell = 0\n" "(0) dist key, default 0, modes")
  order("${dist}" "\nAiPlayerbot.Crafting.Consumables = 0\n" "\nAiPlayerbot.Consumables.KindFromSpell = 0\n" "(0) dist key after Crafting.Consumables")
  need("${dist}" "#   [Crafting] kind=consumables event=use type=flask|elixir|scroll|food_buff|alcohol item= bot= level= cohort=\n" "(0) the use line stays the proof")
  need("${config_h}" "    uint32 consumablesKindFromSpell = 0;\n" "(0) member, default 0")
  need("${config_cpp}" "    consumablesKindFromSpell = config.GetIntDefault(\"AiPlayerbot.Consumables.KindFromSpell\", 0);\n    if (consumablesKindFromSpell > 3)\n    {\n        sLog.outError(\"[Consumables] config key=KindFromSpell value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", consumablesKindFromSpell);\n        consumablesKindFromSpell = 0;\n    }\n" "(0) config read, > 3 = off")
  count("${config_cpp}" "${echo_entry}" 1 "(0) [ConfigEcho] entry")
  order("${config_cpp}" "${echo_before}" "${echo_entry}" "(0) [ConfigEcho] after CombatRule.TargetRule")
  order("${config_cpp}" "${echo_entry}" "${echo_after}" "(0) [ConfigEcho] before Crafting.Consumables")
  need("${use_cpp}" "${kind_for}" "(0) KindFromSpellFor: config, then cohort")
  count("${use_cpp}" "sPlayerbotAIConfig.consumablesKindFromSpell" 2 "(0) switch reads in the action (only KindFromSpellFor)")
  count("${use_cpp}" "KindFromSpellFor()" 4 "(0) KindFromSpellFor: definition, ConsumableSubClass, ScoreSpellStats, Execute")
  need("${use_h}" "        bool KindFromSpellFor() const;\n" "(0) KindFromSpellFor declared")

  # 1. ClassifyConsumable: the derived subclass once, after the null / class check; seven tests read it, none reads proto->SubClass.
  region("${use_cpp}" "bool UseConsumableAction::ClassifyConsumable(" "\n}\n" classify)
  need("${classify}" "${null_check}" "(1) null / class check")
  count("${classify}" "${subclass_line}" 1 "(1) the derived subclass, once")
  order("${classify}" "${null_check}" "${subclass_line}" "(1) after the null / class check")
  order("${classify}" "uint32 const subClass = ConsumableSubClass(proto);" "subClass ==" "(1) before the first subclass test")
  count("${classify}" "proto->SubClass ==" 0 "(1) subclass tests on proto->SubClass")
  count("${classify}" "subClass ==" 7 "(1) subclass tests on the derived subclass")
  foreach(test "    if (subClass == ITEM_SUBCLASS_POTION)\n        return false;\n" "    if (subClass == ITEM_SUBCLASS_FLASK)\n    {\n" "    if (subClass == ITEM_SUBCLASS_ELIXIR)\n    {\n" "    if (subClass == ITEM_SUBCLASS_FOOD ||\n        subClass == ITEM_SUBCLASS_CONSUMABLE ||\n        subClass == ITEM_SUBCLASS_CONSUMABLE_OTHER)\n    {\n" "    if (subClass == ITEM_SUBCLASS_SCROLL)\n    {\n")
    need("${classify}" "${test}" "(1) branch test")
  endforeach()
  count("${classify}" "sLog" 0 "(1) no log line in the classifier (it runs per bag item and tick)")

  # 2. ConsumableSubClass: the off path first, the policy with the facts of the core spell data and the one flask list.
  count("${use_cpp}" "ConsumableSubClass(" 2 "(2) definition and the one call")
  need("${use_cpp}" "${off_path}" "(2) off path first: switch, subclass 0, else proto->SubClass")
  region("${use_cpp}" "uint32 UseConsumableAction::ConsumableSubClass(" "\n}\n" derive)
  need("${derive}" "\n    return ai::consumable_kind::DeriveSubClass(spells, n);" "(2) the policy decides")
  need("${derive}" "        for (uint32 flaskAura : UseConsumableAction::knownFlaskAuras)\n        {\n            if (flaskAura == spellId)\n                facts.flaskAura = true;\n        }\n" "(2) flask aura from knownFlaskAuras")
  need("${derive}" "${facts_glue}" "(2) every spell fact copied from the core spell data")
  need("${derive}" "${slot_glue}" "(2) one slot per item spell with its id and slot category")
  need("${derive}" "${trigger_glue}" "(2) trigger facts as CollectPossibleAuraIds (not 0, not the spell itself)")
  count("${derive}" "sLog" 0 "(2) no log line")
  need("${use_cpp}" "#include \"playerbot/ConsumableKindPolicy.h\"\n" "(2) policy include")
  need("${use_h}" "        uint32 ConsumableSubClass(ItemPrototype const* proto) const;\n" "(2) declared")

  # 3. The flask list.
  need("${use_cpp}" "${flask_list}" "(3) knownFlaskAuras unchanged")

  # 4. The policy header: std only, constants equal to the core enums, variant B by default.
  foreach(bad "#include \"" "sLog" "SpellEntry" "sSpellMgr" "sServerFacade" "ItemPrototype const" "sPlayerbotAIConfig")
    count("${policy}" "${bad}" 0 "(4) policy header std only: ${bad}")
  endforeach()
  need("${policy}" "constexpr bool kBuffAuraFilter = true;\n" "(4) variant B by default")
  foreach(pair "kSubClassConsumable|0|ITEM_SUBCLASS_CONSUMABLE" "kSubClassPotion|1|ITEM_SUBCLASS_POTION"
      "kSubClassElixir|2|ITEM_SUBCLASS_ELIXIR" "kSubClassFlask|3|ITEM_SUBCLASS_FLASK" "kSubClassScroll|4|ITEM_SUBCLASS_SCROLL")
    string(REPLACE "|" ";" p "${pair}")
    list(GET p 0 name)
    list(GET p 1 value)
    list(GET p 2 core_name)
    mirror("${core_item}" ${name} ${value} ${core_name})
  endforeach()
  mirror("${core_dbc}" kFamilyGeneric 0 SPELLFAMILY_GENERIC)
  mirror("${core_dbc}" kFamilyPotion 13 SPELLFAMILY_POTION)
  mirror("${core_spell}" kTargetUnitFriend 21 TARGET_UNIT_FRIEND)
  mirror("${core_spell}" kEffectApplyAura 6 SPELL_EFFECT_APPLY_AURA)
  foreach(pair "kAuraModDamageDone|13|SPELL_AURA_MOD_DAMAGE_DONE" "kAuraModResistance|22|SPELL_AURA_MOD_RESISTANCE"
      "kAuraModStat|29|SPELL_AURA_MOD_STAT" "kAuraModIncreaseHealth|34|SPELL_AURA_MOD_INCREASE_HEALTH"
      "kAuraModIncreaseEnergy|35|SPELL_AURA_MOD_INCREASE_ENERGY" "kAuraModDodgePercent|49|SPELL_AURA_MOD_DODGE_PERCENT"
      "kAuraModCritPercent|52|SPELL_AURA_MOD_CRIT_PERCENT" "kAuraModHitChance|54|SPELL_AURA_MOD_HIT_CHANCE"
      "kAuraModSpellCritChanceSchool|71|SPELL_AURA_MOD_SPELL_CRIT_CHANCE_SCHOOL" "kAuraModPowerRegen|85|SPELL_AURA_MOD_POWER_REGEN"
      "kAuraModAttackPower|99|SPELL_AURA_MOD_ATTACK_POWER" "kAuraModHealingDone|135|SPELL_AURA_MOD_HEALING_DONE")
    string(REPLACE "|" ";" p "${pair}")
    list(GET p 0 name)
    list(GET p 1 value)
    list(GET p 2 core_name)
    mirror("${core_aura}" ${name} ${value} ${core_name})
  endforeach()

  # 5. Neighbours: the keep block does not read the switch (it keeps what the classifier accepts); the unit test is registered and
  #    keeps the four contract cases of the design.
  count("${usage}" "consumablesKindFromSpell" 0 "(5) no switch read in ItemUsageValue.cpp")
  need("${tests}" "add_executable(consumable_kind_policy_tests\n  \"\${PB_MODULE_DIR}/t/consumable_kind_policy_tests.cpp\")\n" "(5) unit test built")
  need("${tests}" "add_test(NAME consumable_kind_policy\n  COMMAND consumable_kind_policy_tests\n" "(5) unit test registered")
  foreach(case "Require(DeriveSubClass(&titans, 1) == kSubClassFlask, \"Flask of the Titans -> flask\");"
      "Require(DeriveSubClass(&fortitude, 1) == kSubClassElixir, \"Elixir of Fortitude -> elixir\");"
      "Require(DeriveSubClass(&strength, 1) == kSubClassScroll, \"Scroll of Strength II -> scroll\");"
      "Require(DeriveSubClass(&majorHealing, 1) == kSubClassPotion, \"Major Healing Potion -> potion\");")
    need("${unit}" "${case}" "(5) unit test contract case")
  endforeach()

  # 6. The review guards, all behind KindFromSpellFor.
  region("${use_cpp}" "bool UseConsumableAction::Execute(" "\n}\n" execute)
  need("${execute}" "    bool const kindFromSpell = KindFromSpellFor();\n" "(6) Execute reads the switch once")
  need("${execute}" "${guard_line}" "(6) Execute skips an outranked candidate and a quest delivery item, switch on only")
  order("${execute}" "if (HasAnyCandidateAura(c))" "${guard_line}" "(6) the guard after the aura check")
  order("${execute}" "${guard_line}" "switch (c.type)" "(6) the guard before the choice")
  region("${use_cpp}" "bool UseConsumableAction::IsCandidateOutranked(" "\n}\n" outranked)
  need("${outranked}" "    CollectPossibleAuraIds(candidate.spellId, auraIds);\n" "(6) outranked: the candidate's aura ids")
  need("${outranked}" "                CollectPossibleAuraIds(proto->Spells[i].SpellId, auraIds);\n" "(6) outranked: every item spell's aura ids")
  need("${outranked}" "        if (spellInfo && bot->HasMorePowerfulSpellActive(spellInfo))\n            return true;\n" "(6) outranked: the core bounce check")
  region("${use_cpp}" "bool UseConsumableAction::IsQuestDeliveryItem(" "\n}\n" questitem)
  need("${questitem}" "    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)\n" "(6) quest item: every quest log slot")
  need("${questitem}" "            if (quest->ReqItemCount[i] && quest->ReqItemId[i] == itemId)\n                return true;\n" "(6) quest item: a required item, any status in the log")
  count("${questitem}" "m_status" 0 "(6) quest item: no status filter (a complete quest still needs the item)")
  region("${use_cpp}" "float UseConsumableAction::ScoreSpellStats(" "\n}\n" score)
  need("${score}" "    bool const kindFromSpell = KindFromSpellFor();\n" "(6) ScoreSpellStats reads the switch once")
  need("${score}" "${mana_guard}" "(6) no score for a pool of a power the bot does not have, switch on only")
  order("${score}" "if (amount <= 0)" "${mana_guard}" "(6) the power guard after the amount check")
  order("${score}" "${mana_guard}" "switch (spellInfo->EffectApplyAuraName[i])" "(6) the power guard before the aura switch")
  foreach(decl "        bool IsCandidateOutranked(const ConsumableCandidate& candidate) const;\n" "        bool IsQuestDeliveryItem(uint32 itemId) const;\n")
    need("${use_h}" "${decl}" "(6) guard declared")
  endforeach()
  need("${use_h}" "        bool IsManaOnlyWithoutManaPool(uint32 spellId) const;\n" "(6) mana-only guard declared")
  count("${use_cpp}" "IsManaOnlyWithoutManaPool(" 2 "(6) mana-only: definition and the one call in Execute")
  region("${use_cpp}" "bool UseConsumableAction::IsManaOnlyWithoutManaPool(" "\n}\n" manaonly)
  need("${manaonly}" "${mana_pool_test}" "(6) mana-only: only a bot without a mana pool")
  order("${manaonly}" "${mana_pool_test}" "LookupSpellInfo(spellId)" "(6) mana-only: the pool test first")
  need("${manaonly}" "        if (!spellInfo->Effect[i])\n            continue;\n" "(6) mana-only: every present effect")
  need("${manaonly}" "${mana_effect}" "(6) mana-only: any other effect keeps the candidate")
  need("${manaonly}" "${mana_amount}" "(6) mana-only: at least one positive mana amount")
  count("${manaonly}" "sPlayerbotAIConfig" 0 "(6) mana-only: no switch read (Execute gates it)")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check(errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "${TAG}:${errs}")
endif()

# ---- 7. negative probes ----------------------------------------------------------------------------------------------
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

probe("default on" dist "\nAiPlayerbot.Consumables.KindFromSpell = 0\n" "\nAiPlayerbot.Consumables.KindFromSpell = 1\n")
probe("member default on" config_h "    uint32 consumablesKindFromSpell = 0;\n" "    uint32 consumablesKindFromSpell = 1;\n")
probe("no clamp" config_cpp "    if (consumablesKindFromSpell > 3)\n" "    if (consumablesKindFromSpell > 4)\n")
probe("echo missing" config_cpp "${echo_entry}" "")
probe("echo out of order" config_cpp "${echo_before}${echo_entry}" "${echo_entry}${echo_before}")
probe("a test on proto->SubClass" use_cpp "    if (subClass == ITEM_SUBCLASS_ELIXIR)\n" "    if (proto->SubClass == ITEM_SUBCLASS_ELIXIR)\n")
probe("null check dropped" use_cpp "${null_check}\n" "")
# Swap the two lines: only the order check can catch this one (the null check and the derived line are both still there).
string(FIND "${use_cpp}" "${null_check}" swap_a)
string(FIND "${use_cpp}" "${subclass_line}" swap_b)
math(EXPR swap_len "${swap_b} - ${swap_a}")
string(LENGTH "${subclass_line}" subclass_len)
math(EXPR swap_len "${swap_len} + ${subclass_len}")
string(SUBSTRING "${use_cpp}" ${swap_a} ${swap_len} swap_from)
probe("derived before the null check" use_cpp "${swap_from}" "${subclass_line}${null_check}")
probe("off path without the switch" use_cpp "    if (!KindFromSpellFor() || proto->SubClass" "    if (proto->SubClass")
probe("derived for every subclass" use_cpp " || proto->SubClass != ITEM_SUBCLASS_CONSUMABLE)\n        return proto->SubClass;" ")\n        return proto->SubClass;")
probe("switch without cohort" use_cpp "${kind_for}" "bool UseConsumableAction::KindFromSpellFor() const\n{\n    return sPlayerbotAIConfig.consumablesKindFromSpell;\n}\n")
probe("second switch read" use_cpp "${subclass_line}" "${subclass_line}    if (sPlayerbotAIConfig.consumablesKindFromSpell == 1)\n        return false;\n")
probe("trigger facts dropped" use_cpp "                fill(trigger, slot.trigger[j]);\n" "                (void)trigger;\n")
probe("duration fact dropped" use_cpp "        facts.durationIndex = spellInfo->DurationIndex;\n" "")
probe("aura facts dropped" use_cpp "            facts.aura[j] = spellInfo->EffectApplyAuraName[j];\n" "")
probe("slot category 0" use_cpp "        slot.itemCategory = proto->Spells[i].SpellCategory;\n" "        slot.itemCategory = 0;\n")
probe("guard for every bot" use_cpp "        if (kindFromSpell && (IsCandidateOutranked(c)" "        if ((IsCandidateOutranked(c)")
probe("guard dropped" use_cpp "${guard_line}" "")
probe("bounce check dropped" use_cpp "        if (spellInfo && bot->HasMorePowerfulSpellActive(spellInfo))\n" "        if (spellInfo && false)\n")
probe("quest item check dropped" use_cpp "            if (quest->ReqItemCount[i] && quest->ReqItemId[i] == itemId)\n" "            if (false)\n")
probe("mana-only skip dropped" use_cpp " ||\n            IsManaOnlyWithoutManaPool(c.spellId)))\n" "))\n")
probe("mana-only for bots with mana" use_cpp "${mana_pool_test}" "")
probe("mana-only with other effects" use_cpp "        if (!manaEffect)\n            return false;\n" "")
probe("power guard for every bot" use_cpp "        if (kindFromSpell && spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_ENERGY" "        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_INCREASE_ENERGY")
probe("flask list changed" use_cpp "    17624,  // Flask of Petrification\n" "")
probe("log in the classifier" use_cpp "    uint32 const subClass = ConsumableSubClass(proto);\n" "    uint32 const subClass = ConsumableSubClass(proto);\n    sLog.outBasic(\"x\");\n")
probe("game header in the policy" policy "#include <cstdint>\n" "#include <cstdint>\n#include \"SpellMgr.h\"\n")
probe("mirror drift" policy "constexpr std::uint32_t kTargetUnitFriend = 21;\n" "constexpr std::uint32_t kTargetUnitFriend = 22;\n")
probe("variant A by default" policy "constexpr bool kBuffAuraFilter = true;\n" "constexpr bool kBuffAuraFilter = false;\n")
probe("keep block reads the switch" usage "sPlayerbotAIConfig.craftingConsumables" "sPlayerbotAIConfig.consumablesKindFromSpell && sPlayerbotAIConfig.craftingConsumables")
probe("contract case dropped" unit "Require(DeriveSubClass(&titans, 1) == kSubClassFlask" "Require(DeriveSubClass(&titans, 1) != kSubClassFlask")

message(STATUS "consumables_kind_from_spell_source_contract: passed (${probes} negative probes caught)")
