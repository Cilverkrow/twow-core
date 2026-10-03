# twow-repo#485 (#471): skinners clear their own skinnable corpse, skin loot is always kept,
# no skinning target under foreign loot. Also pins the core skinning rule the policy mirrors.
# Sources come through PB_SOURCE_DIR and CORE_SOURCE_DIR (= PB_MODULE_DIR/../..), never
# CMAKE_SOURCE_DIR: twow-repo builds the core under /src/core (mem_stores, core#226).
if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED CORE_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR and CORE_SOURCE_DIR are required")
endif()

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

function(require_before text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from begin_marker up to the first end_marker after it.
function(section text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" begin_at)
  if(begin_at EQUAL -1)
    message(FATAL_ERROR "Missing section start: ${begin_marker}")
  endif()
  string(SUBSTRING "${text}" ${begin_at} -1 tail)
  string(FIND "${tail}" "${end_marker}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "Missing section end after ${begin_marker}: ${end_marker}")
  endif()
  string(SUBSTRING "${tail}" 0 ${end_at} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/LootAction.cpp" loot_action)
file(READ "${PB_SOURCE_DIR}/LootObjectStack.cpp" stack)
file(READ "${PB_SOURCE_DIR}/SkinLootPolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)
file(READ "${CORE_SOURCE_DIR}/src/game/Spells/Spell.cpp" spell)
file(READ "${CORE_SOURCE_DIR}/src/game/Spells/SpellEffects.cpp" spell_effects)
file(READ "${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player)
file(READ "${CORE_SOURCE_DIR}/src/game/Handlers/LootHandler.cpp" loot_handler)

# Pure policy, included only by the .cpp files that use it (not through the botpch.h chain).
reject_text("${policy}" "#include \"" "game include in the pure policy")
reject_text("${ai_header}" "SkinLootPolicy.h" "policy include in PlayerbotAI.h (precompiled header chain)")
reject_text("${config_header}" "SkinLootPolicy.h" "policy include in PlayerbotAIConfig.h (precompiled header chain)")
require_text("${loot_action}" "#include \"playerbot/SkinLootPolicy.h\"" "policy include in LootAction.cpp")
require_text("${stack}" "#include \"playerbot/SkinLootPolicy.h\"" "policy include in LootObjectStack.cpp")

# The mirrored rule equals the core's (Spell::CheckCast, SPELL_EFFECT_SKINNING): a change in
# Spell.cpp fails here until RequiredSkinningSkill follows it.
section("${spell}" "case SPELL_EFFECT_SKINNING:" "case SPELL_EFFECT_OPEN_LOCK_ITEM:" spell_skinning)
require_text("${spell_skinning}" "int32 skillValue = ((Player*)m_caster)->GetSkillValue(SKILL_SKINNING);" "core skinning skill (GetSkillValue, bonuses included)")
require_text("${spell_skinning}" "int32 TargetLevel = m_targets.getUnitTarget()->GetLevel();" "core corpse level")
require_text("${spell_skinning}" "int32 ReqValue = (skillValue < 100 ? (TargetLevel - 10) * 10 : TargetLevel * 5);" "core skinning formula")
require_before("${spell_skinning}" "if (ReqValue > skillValue)" "return SPELL_FAILED_LOW_CASTLEVEL;" "core skill check")
require_text("${policy}" "return skill < 100 ? (creatureLevel - 10) * 10 : creatureLevel * 5;" "policy mirror of the core formula")
# The core rules this PR works with: foreign corpses only after their loot, no skinning while loot is left.
require_text("${spell_skinning}" "if (!creature->IsSkinnableBy(m_caster->ToPlayer()))" "core rule for foreign corpses")
section("${spell_skinning}" "(creature->lootForSkin || !creature->loot.isLooted())" "chance for fail at orange skinning attempt" spell_not_looted)
require_text("${spell_not_looted}" "return SPELL_FAILED_TARGET_NOT_LOOTED;" "core: no skinning while loot is left")

# Skin loot reaches the client as LOOT_PICKPOCKETING; the Loot keeps LOOT_SKINNING and the
# skinned corpse keeps lootForSkin (the core's own skin-loot test when items are stored).
section("${player}" "loot->loot_type = loot_type;" "WorldPacket data(SMSG_LOOT_RESPONSE" send_loot)
require_before("${send_loot}" "case LOOT_SKINNING:" "loot_type = LOOT_PICKPOCKETING;" "core skin loot sent as pickpocket loot")
require_text("${player}" "creature->lootForSkin = true;" "core marks a skinned corpse")
require_text("${loot_handler}" "if (pCreature->lootForSkin)" "core skin-loot test when items are stored")
# Spell::EffectOpenLock opens herbs, ore, locked chests and lockboxes as LOOT_SKINNING too:
# skin loot is creature loot only, gathered and chest loot keeps the normal loot rules.
require_text("${spell_effects}" "SendLoot(guid, LOOT_SKINNING, LockType(m_spellInfo->EffectMiscValue[eff_idx]));" "core opens herbs, ore and locked chests as LOOT_SKINNING too")

# StoreLootAction: skin loot is always kept, a roster skinner clears a corpse it can skin.
section("${loot_action}" "bool StoreLootAction::Execute(Event& event)" "bool StoreLootAction::IsLootAllowed(" store_loot)
require_text("${store_loot}" "Creature* const lootCreature = guid.IsCreature() ? ai->GetCreature(guid) : nullptr;" "loot creature only for creature loot")
require_before("${store_loot}" "if (!loot)" "bool const skinLoot = lootCreature && (loot_type == LOOT_SKINNING || loot->loot_type == LOOT_SKINNING ||" "skin loot only from creatures, read after the null check")
require_text("${store_loot}" "lootCreature->lootForSkin);" "skin loot opened again")
require_text("${store_loot}" "bool const clearForSkin = lootCreature && sPlayerbotAIConfig.professionUseClearCorpseForSkinning &&" "nothing extra while the switch is off")
require_text("${store_loot}" "skin_loot::ShouldClearCorpseForSkinning(sPlayerbotAIConfig.professionUseClearCorpseForSkinning, IsRosterBotOnItsOwn(ai)," "clear decided by the policy, roster bots on their own")
require_text("${store_loot}" "loot->loot_type == LOOT_CORPSE, lootCreature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE), ai->HasSkill(SKILL_SKINNING)," "corpse loot, skinnable corpse, skinning")
require_text("${store_loot}" "bot->HasItemCount(7005, 1), int32(bot->GetSkillValue(SKILL_SKINNING)), int32(lootCreature->GetLevel()));" "knife, skill, corpse level")
require_text("${store_loot}" "if (!skinLoot && !clearForSkin && !IsLootAllowed(itemQualifier, ai))" "money rule skipped for skin loot and cleared corpses")
reject_text("${loot_action}" "loot_type != LOOT_SKINNING && !IsLootAllowed(" "packet loot type check (never matched)")
require_before("${store_loot}" "if (!skinLoot && !clearForSkin && !IsLootAllowed(itemQualifier, ai))" "if (AI_VALUE2(uint32, \"stack space for item\", itemid) < itemcount)" "stack space still checked")
# The knife check is the one LootObject::IsLootPossible uses (same item, no second constant).
require_text("${stack}" "if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1))" "knife check in LootObject::IsLootPossible")

# Critic B3.1: skins visible per bot, [ProfessionUse] stage=skin state=looted detail=<item>.
require_text("${loot_action}" "#include \"playerbot/strategy/triggers/ProfessionUseTriggers.h\"" "throttled [ProfessionUse] trace")
require_before("${store_loot}" "HandleAutostoreLootItemOpcode(packet);" "TraceProfessionUse(ai, \"skin\", \"looted\", \"skin_loot\", itemid);" "skin trace after the store")
require_text("${store_loot}" "if (skinLoot && lootItem->is_looted)" "skin trace only for stored skin loot")
# state=cleared reason=junk_taken only when the switch stored junk (an item IsLootAllowed
# refuses), detail = that count; not for every emptied corpse (review core#279).
require_text("${store_loot}" "bool const junkItem = clearForSkin && !skinLoot && !IsLootAllowed(itemQualifier, ai);" "junk: refused by IsLootAllowed, asked only on a corpse being cleared")
require_before("${store_loot}" "bool const junkItem = clearForSkin && !skinLoot && !IsLootAllowed(itemQualifier, ai);" "HandleAutostoreLootItemOpcode(packet);" "junk decided before the store")
require_before("${store_loot}" "HandleAutostoreLootItemOpcode(packet);" "if (junkItem && lootItem->is_looted)" "junk counted after the store")
require_before("${store_loot}" "if (junkItem && lootItem->is_looted)" "++junkTaken;" "junk counted only when stored")
require_before("${store_loot}" "skin_loot::ClearTrace const clearTrace = skin_loot::TraceAfterClear(loot->isLooted(), itemsTaken, junkTaken);" "HandleLootReleaseOpcode(packet);" "cleared state read before the release")
require_text("${store_loot}" "TraceProfessionUse(ai, \"skin\", clearTrace.state, clearTrace.reason, clearTrace.detail);" "clear trace from the policy")
reject_text("${loot_action}" "\"junk_taken\"" "junk_taken decided outside the policy")
require_before("${policy}" "if (junkTaken > 0)" "return { \"cleared\", \"junk_taken\", junkTaken };" "junk_taken only with junk stored, detail = junk count")

# LootObject::Refresh: own loot first (hotfix 8.6), loot left means no skinning target.
section("${stack}" "void LootObject::Refresh(" "WorldObject* LootObject::GetWorldObject(" refresh)
require_text("${refresh}" "bool const lootable = creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);" "lootable flag read once")
require_before("${refresh}" "if (creature->IsTappedBy(bot))" "(TARGET_NOT_LOOTED). Normal loot first" "hotfix 8.6 own loot first")
require_before("${refresh}" "UNIT_DYNFLAG_LOOTABLE" "#485: loot left on the corpse - not a skinning target" "lootable branch before the #485 rule")
require_before("${refresh}" "#485: loot left on the corpse - not a skinning target" "UNIT_FLAG_SKINNABLE" "#485 rule before the skinning branch")
require_text("${refresh}" "if (skin_loot::IsSkinTarget(lootable, creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE)))" "skinning target only without loot")
reject_text("${refresh}" "if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))" "skinning branch that ignores loot left on the corpse")

# LootObject::IsLootPossible: the tool before the skill shortcut (level-10 corpses need skill 0).
section("${stack}" "bool LootObject::IsLootPossible(Player* bot)" "bool LootObjectStack::Add(" possible)
require_before("${possible}" "if (skillId == SKILL_MINING && !bot->HasItemCount(2901, 1))" "if (!reqSkillValue)" "mining pick before the skill shortcut")
require_before("${possible}" "if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1))" "if (!reqSkillValue)" "skinning knife before the skill shortcut")

# Switch: off by default, documented.
require_text("${config_header}" "bool professionUseClearCorpseForSkinning = false;" "switch member, off by default")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.ClearCorpseForSkinning\", false" "switch default off")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.ClearCorpseForSkinning = 0" "documented switch")

message(STATUS "SKIN_LOOT_CONTRACT=PASS")
