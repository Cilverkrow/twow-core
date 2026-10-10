# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A15): PartyMemberValue::FindPartyMember classified every nearby player
# (IsHeal/IsTank) and scanned four lists for the "party member without water|food|item" values
# although their predicate (PlayerWithoutItemPredicate) rejects every unit that is not a Player in
# the bot's own group, so for a bot without group nobody can be the answer. With
# AiPlayerbot.Perf.GiveItemGroupOnlyScan = 1 that classification and list scan are skipped for a
# bot without group and a predicate that opts in through FindPlayerPredicate::OnlyBotGroupMembers().
# Every value Get of the bot (distance, mount speed, last movement, nearest friendly players, rpg
# target) stays in place and in order. Default 0 = the original code path.
# Disclosed side effects (OB-00 09.10.2026): the skipped scan no longer calls Unit::GetPet() on
# foreign players (no SetPet(nullptr) cross-thread write, no error log) and no longer fills
# RandomPlayerbotMgr::eventCache through IsHeal/IsTank.
# Helpers are inline (same shape as t/chase_hazard_skip_source_contract_tests.cmake).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "CORE_SOURCE_DIR is required")
endif()

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "#541 A15: missing source file: ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_source path out_var)
  read_file("${PB_SOURCE_DIR}/${path}" text)
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A15: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A15: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A15: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" count)
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A15: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A15: region start not found: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 tail)
  string(LENGTH "${start_marker}" start_len)
  string(SUBSTRING "${tail}" ${start_len} -1 after)
  string(FIND "${after}" "${end_marker}" end_offset)
  if(end_offset EQUAL -1)
    message(FATAL_ERROR "#541 A15: region end not found after ${start_marker}: ${end_marker}")
  endif()
  math(EXPR len "${start_len} + ${end_offset}")
  string(SUBSTRING "${tail}" 0 ${len} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" config_dist)
read_source("PartyScanPolicy.h" policy_h)
read_source("strategy/values/PartyMemberValue.h" pmv_h)
read_source("strategy/values/PartyMemberValue.cpp" pmv_cpp)
read_source("strategy/values/PartyMemberWithoutItemValue.cpp" item_cpp)
read_source("strategy/triggers/GenericTriggers.h" triggers_h)
read_source("strategy/actions/GiveItemAction.cpp" give_action_cpp)
read_file("${PB_SOURCE_DIR}/../../mod-playerbots.cmake" module_cmake)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)

# --- 1. Switch: own member, default 0, read with default false, documented as 0 ---
require_text("${config_h}" "    bool perfGiveItemGroupOnlyScan = false;  // twow-repo#541 (audit A15): "
  "A15 member with default false")
require_order("${config_h}" "bool chaseSkipHazardPathWhenNoHazards = false;" "bool perfGiveItemGroupOnlyScan = false;"
  "A15 member after the earlier #541 audit switches")
require_count("${config_h}" "perfGiveItemGroupOnlyScan" 1 "A15 member declared once")
require_text("${config_cpp}"
  "    perfGiveItemGroupOnlyScan = config.GetBoolDefault(\"AiPlayerbot.Perf.GiveItemGroupOnlyScan\", false);"
  "A15 switch read with default false")
require_count("${config_cpp}" "AiPlayerbot.Perf.GiveItemGroupOnlyScan" 1 "A15 switch read once")
require_order("${config_cpp}" "chaseSkipHazardPathWhenNoHazards = config." "perfGiveItemGroupOnlyScan = config."
  "A15 switch read next to the earlier #541 audit switches")
require_text("${config_dist}" "\nAiPlayerbot.Perf.GiveItemGroupOnlyScan = 0\n" "A15 conf.dist entry with value 0")
require_count("${config_dist}" "AiPlayerbot.Perf.GiveItemGroupOnlyScan =" 1 "A15 conf.dist entry once")
forbid_text("${config_dist}" "AiPlayerbot.Perf.GiveItemGroupOnlyScan = 1" "A15 conf.dist enabled by default")
require_text("${config_dist}" "# twow-repo#541 (audit A15): 1 = a bot without group skips the healer/tank classification"
  "A15 conf.dist documentation")
require_text("${config_dist}" "# Disclosed side effects that go away (OB-00, 09.10.2026: shipped as neutral with this disclosure)"
  "A15 conf.dist side-effect disclosure")
require_text("${config_dist}" "skipped scan no longer calls GetPet() on nearby foreign players" "A15 conf.dist GetPet disclosure")
require_text("${config_dist}" "(eventCache map insert, first-miss DB load)" "A15 conf.dist eventCache disclosure")
require_order("${config_dist}" "\nAiPlayerbot.Movement.ChaseSkipHazardPathWhenNoHazards = 0\n"
  "\nAiPlayerbot.Perf.GiveItemGroupOnlyScan = 0\n" "A15 conf.dist entry next to the #541 performance switches")

# --- 2. Pure policy, no state ---
require_text("${policy_h}"
  "inline bool SharesGroup(void const* memberGroup, void const* botGroup)\n{\n    return memberGroup != nullptr && botGroup != nullptr && memberGroup == botGroup;\n}\n"
  "policy SharesGroup models Player::IsInGroup")
require_text("${policy_h}"
  "inline bool SkipUngroupedScan(bool switchOn, bool predicateOnlyBotGroup, bool botHasGroup)\n{\n    return switchOn && predicateOnlyBotGroup && !botHasGroup;\n}\n"
  "policy SkipUngroupedScan: switch, opt-in predicate and no bot group")
forbid_text("${policy_h}" "static" "static state in the policy")
forbid_text("${policy_h}" "mutex" "lock in the policy")

# --- 3. Hook: generic default false, only the item predicate opts in (CMaNGOS branch) ---
require_text("${pmv_h}" "        virtual bool OnlyBotGroupMembers() const { return false; }\n" "generic hook default false")
forbid_text("${pmv_h}" "OnlyBotGroupMembers() const { return true; }" "generic hook default true")
require_count("${pmv_h}" "OnlyBotGroupMembers" 1 "hook declared once in PartyMemberValue.h")
require_text("${module_cmake}" "target_compile_definitions(\${PB_TARGET} PUBLIC CMANGOS MANGOSBOT_ZERO ENABLE_PLAYERBOTS)"
  "module compiles the CMANGOS branch")

region("${item_cpp}" "class PlayerWithoutItemPredicate : public FindPlayerPredicate, public PlayerbotAIAware"
  "Unit* PartyMemberWithoutItemValue::Calculate()" item_pred)
region("${item_pred}" "    virtual bool Check(Unit* unit) override\n" "        PlayerbotAI *botAi = GetBotAI(member);" item_gate)
# Every exit before the bot-group gate rejects; the gate itself is the CMaNGOS IsInGroup line.
require_order("${item_pred}" "        Pet* pet = dynamic_cast<Pet*>(unit);"
  "        Player* member = dynamic_cast<Player*>(unit);\n        if (!member)\n            return false;\n"
  "predicate rejects pets before the Player cast")
require_order("${item_pred}" "        Player* member = dynamic_cast<Player*>(unit);"
  "#ifdef CMANGOS\n        if (!member->IsInGroup(ai->GetBot()))\n#endif\n            return false;\n"
  "predicate: Player cast before the bot-group gate")
require_count("${item_gate}" "return " 4 "exits before GetBotAI (pet, alive, cast, group)")
require_count("${item_gate}" "return false;" 4 "every exit before GetBotAI rejects")
require_order("${item_pred}" "if (!member->IsInGroup(ai->GetBot()))" "PlayerbotAI *botAi = GetBotAI(member);"
  "bot-group gate before the other bot's AI")
require_order("${item_pred}" "PlayerbotAI *botAi = GetBotAI(member);"
  "#ifdef CMANGOS\n    // twow-repo#541 (audit A15)" "opt-in after Check()")
require_order("${item_pred}" "#ifdef CMANGOS\n    // twow-repo#541 (audit A15)"
  "    virtual bool OnlyBotGroupMembers() const override { return true; }\n#endif\n\nprivate:"
  "opt-in inside #ifdef CMANGOS, before private:")
require_count("${item_cpp}" "OnlyBotGroupMembers() const override { return true; }" 1 "one opt-in in the item predicates")
require_count("${item_cpp}" "OnlyBotGroupMembers" 1 "subclasses inherit the opt-in, no own override")
# The food and water predicates inherit the opt-in, so their Check() must call the base Check() first.
require_count("${item_cpp}" ": public PlayerWithoutItemPredicate" 2 "two subclasses of the item predicate")
require_count("${item_cpp}" "if (!PlayerWithoutItemPredicate::Check(unit))" 2 "food and water call the base Check()")
region("${item_cpp}" "class PlayerWithoutFoodPredicate : public PlayerWithoutItemPredicate" "};" food_pred)
region("${item_cpp}" "class PlayerWithoutWaterPredicate : public PlayerWithoutItemPredicate" "};" water_pred)
foreach(sub IN ITEMS food_pred water_pred)
  require_text("${${sub}}"
    "    virtual bool Check(Unit* unit) override\n    {\n        if (!PlayerWithoutItemPredicate::Check(unit))\n            return false;\n"
    "${sub}: base Check() is the first statement")
  require_count("${${sub}}" "virtual bool Check(" 1 "${sub}: one Check() override")
endforeach()

# No other predicate opts in. The six known users of FindPlayerPredicate, and the whole tree.
foreach(other IN ITEMS
    "strategy/values/PartyMemberWithoutAuraValue.cpp"
    "strategy/values/PartyMemberToDispel.cpp"
    "strategy/values/PartyMemberToResurrect.cpp"
    "strategy/values/PartyMemberToSoulstone.cpp"
    "strategy/druid/DruidValues.cpp"
    "strategy/paladin/PaladinTriggers.cpp")
  read_source("${other}" other_text)
  forbid_text("${other_text}" "OnlyBotGroupMembers" "opt-in in ${other} (needs its own neutrality proof)")
endforeach()
file(GLOB_RECURSE all_sources "${PB_SOURCE_DIR}/*.h" "${PB_SOURCE_DIR}/*.cpp")
set(override_total 0)
set(hook_files "")
foreach(src IN LISTS all_sources)
  file(READ "${src}" src_text)
  string(FIND "${src_text}" "OnlyBotGroupMembers" hook_offset)
  if(NOT hook_offset EQUAL -1)
    file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${src}")
    list(APPEND hook_files "${rel}")
    count_text("${src_text}" "OnlyBotGroupMembers() const override" n)
    math(EXPR override_total "${override_total} + ${n}")
  endif()
endforeach()
if(NOT override_total EQUAL 1)
  message(FATAL_ERROR "#541 A15: OnlyBotGroupMembers overrides in the tree: ${override_total}, expected 1 (${hook_files})")
endif()
list(SORT hook_files)
set(expected_hook_files
  "strategy/values/PartyMemberValue.cpp"
  "strategy/values/PartyMemberValue.h"
  "strategy/values/PartyMemberWithoutItemValue.cpp")
if(NOT hook_files STREQUAL expected_hook_files)
  message(FATAL_ERROR "#541 A15: OnlyBotGroupMembers used in '${hook_files}', expected '${expected_hook_files}'")
endif()

# --- 4. Callers unchanged: full FindPartyMember call, trigger operand order ---
require_text("${item_cpp}" "    Unit *unit = FindPartyMember(*predicate);\n" "item value calls FindPartyMember with the defaults")
forbid_text("${item_cpp}" "FindPartyMember(*predicate, true" "item value ignores out-of-group players (drops the gating Gets)")
require_text("${triggers_h}"
  "return AI_VALUE2(Unit*, \"party member without item\", item) && AI_VALUE2(uint32, \"item count\", item);"
  "give item trigger operand order")
require_text("${triggers_h}"
  "return AI_VALUE(Unit*, \"party member without food\") && AI_VALUE2(uint32, \"item count\", item);"
  "give food trigger operand order")
require_text("${triggers_h}"
  "return AI_VALUE(Unit*, \"party member without water\") && AI_VALUE2(uint32, \"item count\", item);"
  "give water trigger operand order")
forbid_text("${triggers_h}" "AI_VALUE2(uint32, \"item count\", item) && AI_VALUE(Unit*, \"party member without"
  "swapped give food/water trigger operands")
forbid_text("${triggers_h}" "AI_VALUE2(uint32, \"item count\", item) && AI_VALUE2(Unit*, \"party member without item\""
  "swapped give item trigger operands")
require_text("${give_action_cpp}" "    return AI_VALUE2(Unit*, \"party member without item\", item);"
  "give item action target value")

# --- 5. FindPartyMember: gate after every value Get, skips only the classification and list scan ---
region("${pmv_cpp}" "Unit* PartyMemberValue::FindPartyMember(FindPlayerPredicate &predicate, bool ignoreOutOfGroup, bool ignoreTanks)"
  "bool PartyMemberValue::Check(Unit* player)" find_body)
region("${pmv_cpp}" "Unit* PartyMemberValue::FindPartyMember(std::list<Player*>* party, FindPlayerPredicate &predicate, bool ignoreTanks)"
  "Unit* PartyMemberValue::FindPartyMember(FindPlayerPredicate &predicate" scan_body)
require_text("${pmv_cpp}" "#include \"playerbot/PartyScanPolicy.h\"\n" "policy include")

set(gate
  "    bool const skipGroupScan = ai::party_scan::SkipUngroupedScan(\n        sPlayerbotAIConfig.perfGiveItemGroupOnlyScan, predicate.OnlyBotGroupMembers(), group != nullptr);\n")
require_text("${find_body}" "${gate}" "A15 gate: switch, predicate opt-in and the bot's group snapshot")
# Value Gets, gate and the original scan, in this order (strings hold ";", so no CMake list).
set(step_0 "    Player* master = GetMaster();\n")
set(step_1 "    Group* group = bot->GetGroup();\n")
set(step_2 "    if (allowBufOutOfGroupPlayers && !ai->AllowActivity(OUT_OF_PARTY_ACTIVITY))\n        allowBufOutOfGroupPlayers = false;\n")
set(step_3 "    if (allowBufOutOfGroupPlayers && AI_VALUE2(float, \"distance\", \"master target\") > sPlayerbotAIConfig.sightDistance)\n        allowBufOutOfGroupPlayers = false;\n")
set(step_4 "    if (allowBufOutOfGroupPlayers && AI_VALUE2(uint32, \"current mount speed\", \"self target\"))\n        allowBufOutOfGroupPlayers = false;\n")
set(step_5 "    if (allowBufOutOfGroupPlayers && !AI_VALUE(LastMovement&, \"last movement\").lastPath.empty()")
set(step_6 "        if (ai->AllowActivity(OUT_OF_PARTY_ACTIVITY))\n            nearestOutOfGroupPlayers = AI_VALUE(std::list<ObjectGuid>, \"nearest friendly players\");\n")
set(step_7 "        if (nearestOutOfGroupPlayers.size() < 100)\n            nearestPlayers.insert(nearestPlayers.end(), nearestOutOfGroupPlayers.begin(), nearestOutOfGroupPlayers.end());\n    }\n")
set(step_8 "${gate}")
set(step_9 "    std::list<Player*> healers, tanks, others, masters;\n    if (master && !skipGroupScan) masters.push_back(master);\n    for (std::list<ObjectGuid>::iterator i = nearestPlayers.begin(); !skipGroupScan && i != nearestPlayers.end(); ++i)\n    {\n")
set(step_10 "        Player* player = dynamic_cast<Player*>(ai->GetUnit(*i));\n")
set(step_11 "        if (ai->IsHeal(player))\n        {\n            healers.push_back(player);\n        }\n        else if (ai->IsTank(player) && !ignoreTanks)\n        {\n            tanks.push_back(player);\n        }\n        else if (player != master)\n        {\n            others.push_back(player);\n        }\n")
set(step_12 "    lists.push_back(&healers);\n    lists.push_back(&tanks);\n    lists.push_back(&masters);\n    lists.push_back(&others);\n")
set(step_13 "        Unit* target = FindPartyMember(party, predicate, ignoreTanks);\n        if (target)\n            return target;\n")
set(step_14 "    if (allowBufOutOfGroupPlayers)\n    {\n        if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, \"rpg target\"))\n")
set(step_15 "            if (target && sServerFacade.IsFriendlyTo(bot, target) && predicate.Check(target) && CanFreeMoveValue::CanFreeMoveTo(ai, target))\n                return target;\n")
set(step_16 "    return NULL;\n}")
foreach(idx RANGE 0 15)
  math(EXPR nxt "${idx} + 1")
  require_text("${find_body}" "${step_${idx}}" "FindPartyMember step ${idx}")
  require_order("${find_body}" "${step_${idx}}" "${step_${nxt}}" "FindPartyMember step ${idx} -> ${nxt}")
endforeach()
require_order("${find_body}" "nearestPlayers.insert(nearestPlayers.end(), nearestOutOfGroupPlayers.begin(), nearestOutOfGroupPlayers.end());"
  "skipGroupScan" "A15 flag first defined after the last shared value Get")
require_count("${find_body}" "skipGroupScan" 3 "A15 flag uses (definition, master push, loop condition)")
require_count("${pmv_cpp}" "skipGroupScan" 3 "A15 flag only in FindPartyMember")
require_count("${pmv_cpp}" "sPlayerbotAIConfig.perfGiveItemGroupOnlyScan" 1 "A15 switch read once")
forbid_text("${find_body}" "if (skipGroupScan)" "early exit on the A15 flag")
forbid_text("${find_body}" "skipGroupScan) return" "early return on the A15 flag")
forbid_text("${find_body}" "    if (master) masters.push_back(master);" "ungated master push next to the gate")
forbid_text("${find_body}" "nearestPlayers.begin(); i != nearestPlayers.end(); ++i)" "ungated classification loop")
require_count("${find_body}" "return " 3 "FindPartyMember exits (list hit, rpg target, NULL)")
require_count("${find_body}" "AI_VALUE" 6 "FindPartyMember value Gets kept (distance, mount speed, last movement x2, nearest friendly players, rpg target)")
require_count("${find_body}" "ai->AllowActivity(OUT_OF_PARTY_ACTIVITY)" 2 "AllowActivity timer calls kept")
forbid_text("${find_body}" "static " "static state in FindPartyMember")
forbid_text("${pmv_cpp}" "shared_mutex" "shared_mutex in PartyMemberValue.cpp")
forbid_text("${pmv_cpp}" "static std::" "static container in PartyMemberValue.cpp")

# --- 6. List scan unchanged: no value Get of the bot while it has no group ---
require_text("${scan_body}" "        if (!player->FindMap() || player->FindMap() != bot->FindMap())\n            continue;\n"
  "scan: same map first")
require_text("${scan_body}" "        if (ignoreTanks && ai->IsTank(player))\n            continue;\n" "scan: IsTank only with ignoreTanks")
require_text("${scan_body}"
  "        if (bot->GetGroup() && !player->IsInGroup(bot) && !CanFreeMoveValue::CanFreeMoveTo(ai, player))\n            continue;\n"
  "scan: CanFreeMoveTo only with a bot group")
require_text("${scan_body}" "        if (Check(player) && predicate.Check(player))\n            return player;\n" "scan: predicate on the player")
require_text("${scan_body}" "        Pet* pet = player->GetPet();\n        if (pet && Check(pet) && predicate.Check(pet))\n            return pet;\n"
  "scan: pet branch (GetPet, the disclosed side effect, stays on the OFF path)")
forbid_text("${scan_body}" "skipGroupScan" "A15 flag in the list scan")
forbid_text("${scan_body}" "AI_VALUE" "value Get in the list scan")

# --- 7. Core semantics the neutrality rests on ---
require_text("${player_h}"
  "bool IsInGroup(Player const* other, bool /*sub*/ = false) const { return other && GetGroup() && GetGroup() == other->GetGroup(); }"
  "core Player::IsInGroup needs one shared non-null group")

message(STATUS "give_item_group_scan source contract passed")
