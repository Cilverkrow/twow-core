# twow-repo#541 (audit A04+A05, cards 22 and 21): behind AiPlayerbot.Perf.GatherLootFastPath (default 0)
# "add gathering loot" rejects SKILL_NONE objects before the LOS raycast (A04) and reads the
# "nearest corpses" list only for a bot with skinning (A05). Same nodes and corpses added, less work.
# Switch off: the inherited AddAllLootAction::Execute and the original LOS -> SKILL_NONE order, unchanged.
# The A05 premise (a corpse is a gathering target only via SKILL_SKINNING) is pinned against
# LootObject::Refresh and core Creature.h, so drift there fails this contract.

if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR and CORE_SOURCE_DIR are required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(read_core path out_var)
  file(READ "${CORE_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A04/A05: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A04/A05: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A04/A05: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A04/A05: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A04/A05: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#541 A04/A05: ${description}: '${needle}' found ${n}x, expected ${expected}")
  endif()
endfunction()

read_source("strategy/actions/AddLootAction.cpp" add_loot)
read_source("strategy/actions/AddLootAction.h" add_loot_h)
read_source("LootObjectStack.cpp" loot_stack)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" random_mgr)
read_core("src/game/Objects/Creature.h" creature_h)

set(sw "sPlayerbotAIConfig.perfGatherLootFastPath")

# 1. Switch: default off, documented, loaded once.
require_text("${config_h}" "bool perfGatherLootFastPath = false;" "member default off")
require_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool perfGatherLootFastPath = false;" "after the #541 switches")
require_text("${config_cpp}" "perfGatherLootFastPath = config.GetBoolDefault(\"AiPlayerbot.Perf.GatherLootFastPath\", false);" "config default 0")
count_text("${config_cpp}" "perfGatherLootFastPath =" 1 "single config load")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.GatherLootFastPath = 0\n" "documented key, value 0")
count_text("${conf_dist}" "AiPlayerbot.Perf.GatherLootFastPath =" 1 "single documented key")

# 2. The shared base loop ("add all loot", chat links) stays untouched.
region("${add_loot}" "bool AddAllLootAction::Execute(Event& event)" "bool AddLootAction::isUseful()" base_exec)
set(base_loops "        std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid>>(\"nearest game objects no los\")->Get();
        for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
            added |= AddLoot(requester, *i);

        std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid>>(\"nearest corpses\")->Get();
        for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
            added |= AddLoot(requester, *i);
    }

    return added;")
require_text("${base_exec}" "${base_loops}" "original game object + corpse loops in AddAllLootAction::Execute")
forbid_text("${base_exec}" "perfGatherLootFastPath" "switch in the shared base loop")
forbid_text("${base_exec}" "HasSkill" "skill gate in the shared base loop")

# 3. A05: Execute override in AddGatheringLootAction, no isUseful (the trace stays FAILED, never USELESS).
region("${add_loot_h}" "class AddGatheringLootAction : public AddAllLootAction" "#ifdef GenerateBotHelp" gather_h)
require_text("${gather_h}" "bool Execute(Event& event) override;" "Execute override declared")
forbid_text("${gather_h}" "isUseful" "isUseful override (would turn the FAILED trace into USELESS)")
forbid_text("${add_loot}" "AddGatheringLootAction::isUseful" "isUseful override")
require_text("${add_loot_h}" "virtual bool Execute(Event& event) override;" "base Execute stays virtual")

region("${add_loot}" "bool AddGatheringLootAction::Execute(Event& event)" "bool AddGatheringLootAction::AddLoot(Player* requester, ObjectGuid guid)" gather_exec)
set(off_gate "    if (!${sw} || !event.getParam().empty())\n        return AddAllLootAction::Execute(event);")
set(go_loop "    std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid>>(\"nearest game objects no los\")->Get();
    for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
        added |= AddLoot(requester, *i);")
set(corpse_block "    if (ai->HasSkill(SKILL_SKINNING))
    {
        std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid>>(\"nearest corpses\")->Get();
        for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
            added |= AddLoot(requester, *i);
    }

    return added;
}")
require_text("${gather_exec}" "${off_gate}" "switch-off / chat-link fallback to the inherited loop")
require_text("${gather_exec}" "Player* requester = event.getOwner() ? event.getOwner() : GetMaster();" "same requester as the base")
require_text("${gather_exec}" "${go_loop}" "game object loop unchanged")
require_text("${gather_exec}" "${corpse_block}" "corpse fetch and loop inside the skinning block")
require_order("${gather_exec}" "${off_gate}" "${go_loop}" "fallback first")
require_order("${gather_exec}" "${go_loop}" "${corpse_block}" "game objects before corpses (original order)")
count_text("${gather_exec}" "(\"nearest corpses\")->Get();" 1 "one corpse fetch")
count_text("${gather_exec}" "added |= AddLoot(requester, *i);" 2 "two loops")
count_text("${gather_exec}" "return " 2 "fallback return and return added")
forbid_text("${gather_exec}" "HasItemCount" "extra gate beyond the proven premise")

# 4. A04: the skill reject before the LOS raycast; the switch-off order stays verbatim behind it.
region("${add_loot}" "bool AddGatheringLootAction::AddLoot(Player* requester, ObjectGuid guid)" "bool const added = AddAllLootAction::AddLoot(requester, guid);" gather_add)
set(head "    LootObject loot(bot, guid);\n\n    WorldObject *wo = loot.GetWorldObject(bot);\n    if (loot.IsEmpty() || !wo)\n        return false;\n")
set(fast "    if (${sw} && loot.skillId == SKILL_NONE)\n        return false;\n")
set(off_path "\n    if (!sServerFacade.IsWithinLOSInMap(bot, wo))\n        return false;\n\n    if (loot.skillId == SKILL_NONE)\n        return false;\n\n    if (!loot.IsLootPossible(bot))")
require_text("${gather_add}" "${head}" "LootObject and WorldObject resolved once")
require_text("${gather_add}" "${fast}" "fast skill reject")
require_text("${gather_add}" "${off_path}" "original LOS -> SKILL_NONE -> IsLootPossible sequence")
require_order("${gather_add}" "${head}" "${fast}" "fast reject after the empty check")
require_order("${gather_add}" "${fast}" "${off_path}" "fast reject before the LOS raycast")
require_order("${gather_add}" "if (!loot.IsLootPossible(bot))" "GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE)" "IsLootPossible before the hostile search")
count_text("${gather_add}" "LootObject loot(bot, guid);" 1 "one LootObject")
count_text("${gather_add}" "GetWorldObject(" 1 "no extra Refresh")
count_text("${gather_add}" "IsWithinLOSInMap(" 1 "one LOS call")
count_text("${gather_add}" "perfGatherLootFastPath" 1 "one switch read in AddLoot")

# 5. A05 neutrality premises, pinned to the source they rely on.
region("${creature_h}" "uint32 GetRequiredLootSkill() const {" "}" required_skill)
require_text("${required_skill}" "if (skinning_loot_id) return SKILL_SKINNING;" "corpse loot skill is skinning")
require_text("${required_skill}" "return 0;" "else no skill")
count_text("${required_skill}" "return " 2 "GetRequiredLootSkill returns only SKILL_SKINNING or 0")

region("${loot_stack}" "void LootObject::Refresh(Player* bot, ObjectGuid guid, bool debug)" "GameObject* go = ai->GetGameObject(guid);" refresh_creature)
count_text("${refresh_creature}" "this->guid = guid;" 2 "a corpse gets a guid only in the tapped and the skinning branch")
region("${refresh_creature}" "if (creature->IsTappedBy(bot))" "skin_loot::IsSkinTarget(" tapped)
require_text("${tapped}" "this->guid = guid;" "tapped corpse guid")
forbid_text("${tapped}" "skillId =" "a lootable tapped corpse must stay SKILL_NONE")
region("${refresh_creature}" "skin_loot::IsSkinTarget(" "return;" skin)
require_order("${skin}" "skillId = creature->GetCreatureInfo()->GetRequiredLootSkill();" "if (ai->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)" "skin skill from GetRequiredLootSkill")
require_order("${skin}" "if (ai->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)" "this->guid = guid;" "skin guid only with the skill")

# Every other reader of "nearest corpses" must use Get() (recompute when stale), never LazyGet():
# skipping the gather fetch may then only hand them an equally fresh or fresher list.
file(GLOB_RECURSE pb_sources LIST_DIRECTORIES false "${PB_SOURCE_DIR}/*.h" "${PB_SOURCE_DIR}/*.cpp")
foreach(f IN LISTS pb_sources)
  file(READ "${f}" t)
  string(FIND "${t}" "\"nearest corpses\"" has_corpses)
  if(NOT has_corpses EQUAL -1)
    string(REGEX MATCH "\"nearest corpses\"[^\n]*LazyGet" lazy "${t}")
    string(REGEX MATCH "_LAZY\\([^\n]*\"nearest corpses\"" lazy_macro "${t}")
    if(lazy OR lazy_macro)
      file(RELATIVE_PATH rel "${PB_SOURCE_DIR}" "${f}")
      message(FATAL_ERROR "#541 A04/A05: ${rel} reads \"nearest corpses\" lazily; A05's cache-phase argument assumes Get()")
    endif()
  endif()
endforeach()

# 6. PerfMon guard: nothing of this fix in the bot manager update.
forbid_text("${random_mgr}" "perfGatherLootFastPath" "switch in RandomPlayerbotMgr")

message(STATUS "GATHER_LOOT_FAST_PATH_SOURCE_CONTRACT=PASS")
