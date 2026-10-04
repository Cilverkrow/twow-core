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

# Both texts present and the first one ahead of the second (string(FIND) offsets).
function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order broken (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AddLootAction.cpp" add_loot)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/ProfessionUseTriggers.cpp" triggers)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ProfessionUseActions.cpp" actions)
file(READ "${PB_SOURCE_DIR}/strategy/generic/LootNonCombatStrategy.cpp" strategy)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/TriggerContext.h" trigger_context)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ActionContext.h" action_context)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #333 gathering: roster-only wider radius, the hostile check stays.
require_text("${add_loot}" "profession_use::GatherDistance(IsRosterBotOnItsOwn(ai)" "roster-only gather radius")
require_text("${add_loot}" "strongHostiles.size() > 1" "hostile check kept")
foreach(reason not_lootable too_far hostiles bag_full node_in_range)
  require_text("${add_loot}" "\"${reason}\"" "gather diagnostic ${reason}")
endforeach()

# Crafting: roster bots on their own, interval, bag space, no spell focus.
require_text("${triggers}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster()" "roster bots on their own only")
require_text("${triggers}" "professionUseCraftIntervalSeconds" "craft interval")
require_text("${triggers}" "spell->RequiresSpellFocus" "focus recipes excluded")
require_text("${triggers}" "ShouldCraftSpellValue::SpellGivesSkillUp" "skill-up recipes only")
require_text("${triggers}" "[ProfessionUse] stage=%s state=%s reason=%s" "visible diagnostics")
require_text("${actions}" "DoSpecificAction(\"craft random item\"" "existing craft action reused")
require_text("${strategy}" "\"profession craft\"" "craft trigger in the gather strategy")
require_text("${trigger_context}" "creators[\"profession craft\"]" "trigger registered")
require_text("${action_context}" "creators[\"profession craft\"]" "action registered")

# Defaults: behaviour unchanged unless configured.
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.GatherDistance\", 0.0f" "gather radius default off")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.Craft\", false" "craft default off")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.Craft = 0" "documented craft switch")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.Trace = 0" "documented trace switch")

# twow-repo#485: crafting with real reagents. Under the item cheat every recipe
# looked craftable (4,791 starts, 0 "no_materials"); now the bags decide.
file(READ "${PB_SOURCE_DIR}/ProfessionUsePolicy.h" policy)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ProfessionUseActions.h" actions_header)
file(READ "${PB_SOURCE_DIR}/strategy/values/ItemUsageValue.cpp" item_usage)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_header)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.h" ai_header)

require_text("${triggers}" "professionUseRealReagents" "real-reagent switch in the trigger")
require_text("${triggers}" "bot->GetItemCount(uint32(spell->Reagent[i])) / spell->ReagentCount[i]" "reagents counted in the bags")
require_text("${triggers}" "spell->Totem[i] && !bot->HasItemCount(spell->Totem[i], 1)" "tools checked in the bags")
require_text("${triggers}" "profession_use::Pick(recipes, lastSkill)" "deterministic pick")
require_text("${triggers}" "AI_VALUE2(bool, \"can craft spell\", spellId)" "legacy check kept without the switch")
# Critic B4.1: Pick is -1 when every craftable recipe is backed off.
require_text("${triggers}" "\"backed_off\"" "all-backed-off diagnostic")
require_order("${triggers}" "if (pick < 0)" "recipes[std::size_t(pick)]" "pick checked before it is used")
# Critic B4.2: no cast while moving, fighting, casting or mounted; a backoff
# only after a lasting refusal (reagent, tool, spell focus, no room).
require_text("${triggers}" "if (!IsReadyToCraft(ai))" "trigger waits while the bot is busy")
require_order("${triggers}" "if (!IsReadyToCraft(ai))" "for (uint32 spellId : AI_VALUE(std::vector<uint32>, \"craft spells\"))" "idle check before the recipe scan")
require_text("${triggers}" "sServerFacade.isMoving(bot) || bot->IsTaxiFlying(), sServerFacade.IsInCombat(bot)" "moving and combat checked")
require_text("${triggers}" "bot->IsNonMeleeSpellCasted(true), !bot->IsStandState(), bot->IsMounted()" "casting, sitting and mounted checked")
require_text("${actions_header}" "bool isUseful() override;" "craft action checks usefulness")
require_text("${actions}" "|| IsReadyToCraft(ai)" "action waits while the bot is busy")
require_text("${actions}" "SET_AI_VALUE2(int, \"manual int\", \"profession craft spell\", 0);" "pick used once")
require_order("${actions}" "ai->CanCastSpell(spellId, bot, 0, true, nullptr, false, false, false, &check)" "ai->CastSpell(spellId, bot)" "pre-check before the cast")
# Review: trade skills carry SPELL_ATTR_NOT_SHAPESHIFT; the form goes first.
require_order("${actions}" "ai->RemoveShapeshift();" "ai->CanCastSpell(spellId, bot, 0, true, nullptr, false, false, false, &check)" "form dropped before the pre-check")
foreach(code SPELL_FAILED_ITEM_NOT_READY SPELL_FAILED_ITEM_GONE SPELL_FAILED_REQUIRES_SPELL_FOCUS)
  require_text("${actions}" "case ${code}:" "lasting refusal ${code}")
endforeach()
# Review follow-up: DONT_REPORT is no lasting refusal. Spell::CheckCast also
# returns it before CheckItems (ended battleground, banish), so no backoff.
forbid_text("${actions}" "case SPELL_FAILED_DONT_REPORT:" "backoff on the transient DONT_REPORT")
require_text("${actions}" "if (result == SPELL_FAILED_DONT_REPORT)" "DONT_REPORT recognised")
require_text("${actions}" "return \"dont_report\";" "DONT_REPORT traced without backoff")
require_text("${actions}" "backoff ? std::string(backoff) : TransientReason(check)" "transient refusals traced")
require_order("${actions}" "if (backoff)" "\"profession craft failed until\"" "backoff only after a lasting refusal")
require_text("${triggers}" "\"profession craft failed until\"" "backoff honoured by the trigger")
# Review: a recipe on its own or category cooldown (transmutes 24-48 h) would
# fail with NOT_READY every interval and, ranked first, block all crafting.
require_text("${triggers}" "bot->HasSpellCooldown(spellId)" "recipe on cooldown skipped")
require_text("${triggers}" "bot->HasSpellCategoryCooldown(spell->Category)" "recipe on category cooldown skipped")
# Review follow-up: so would a recipe whose product the bags cannot take (unique
# item carried); the trigger checks the room like Spell::CheckItems.
require_text("${triggers}" "bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, spell->EffectItemType[0], 1) == EQUIP_ERR_OK" "room for the product checked like Spell::CheckItems")
require_text("${triggers}" "recipe.craftable > 0 && !HasRoomForProduct(spell, bot)" "recipe without room for its product skipped")
# Critic B4.3: a pending pick is reused for a minute instead of a scan every 10 s.
require_text("${triggers}" "AI_VALUE2(time_t, \"manual time\", \"profession craft scan\"), 60)" "scan at most once a minute")
# Direct self cast, trace with the spell id, bot event for cast starts (critic B4.4).
require_text("${actions}" "ai->CastSpell(spellId, bot)" "direct self cast")
require_text("${actions}" "ok ? \"cast_started\" : \"failed\", ok ? \"real_reagents\" : \"cast_failed\", spellId" "trace carries the spell id")
require_text("${actions}" "\"CraftCastStarted\"" "bot event counts cast starts")
forbid_text("${actions}" "\"CraftAction\"" "event name that reads like a finished item")
# Hotfix 8.13: the legacy path traces "started" only once CastCustomSpellAction began the cast.
require_text("${actions}" "TraceProfessionUse(ai, \"craft\", \"failed\", \"cast_not_started\", uint32(bot->GetLevel()));" "legacy path traces only a request failure (8.13)")
forbid_text("${actions}" "started ? \"started\"" "legacy started trace before the cast")
# Review (merge with 8.13): the real-reagent cast returns before the legacy
# request, so no cast is traced twice (cast_started here, started in
# CastCustomSpellAction); other refusals carry the cast result like 8.13, under
# their own prefix (8.13 uses cast_result_<n> for "rpg craft" and the legacy path).
require_order("${actions}" "return ok;" "DoSpecificAction(\"craft random item\"" "real-reagent path returns before the legacy request")
require_text("${actions}" "return \"precheck_result_\" + std::to_string(uint32(result));" "transient refusal traced with its cast result")
forbid_text("${actions}" "\"cast_result_\"" "reason that mixes with the 8.13 legacy and rpg craft failures")
forbid_text("${actions}" "\"not_castable\"" "reason without the cast result")
forbid_text("${actions}" "RandomBotSayWithoutMaster" "self-cast errors said instead of logged")

# Own materials are kept; a vendor reagent counts only with the reagents no
# vendor sells.
require_text("${item_usage}" "sPlayerbotAIConfig.professionUseKeepCraftMaterials && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())" "roster-only keep switch")
require_text("${item_usage}" "(!ai->HasCheat(BotCheatMask::item) || keepCraft) && IsItemNeededForUsefullCraft(proto," "materials needed despite the item cheat")
require_text("${item_usage}" "profession_use::IsVendorReagent(sPlayerbotAIConfig.professionUseVendorReagents, proto->ItemId)" "vendor reagents from the config")
# Review follow-up: two vendor reagents of one recipe (thread and bleach or dye)
# waited for each other and were never bought.
require_text("${item_usage}" "bool const vendorReagent = keepCraft && profession_use::IsVendorReagent(" "vendor reagent only under KeepCraftMaterials")
require_text("${item_usage}" "lowBagSpace || vendorReagent, vendorReagent);" "vendor reagent walk only for a vendor reagent")
require_order("${item_usage}" "if (!profession_use::OtherReagentRequired(sPlayerbotAIConfig.professionUseVendorReagents, vendorReagent, reqProto->ItemId))" "AI_VALUE2(uint32, \"item count\", reqProto->Name1)" "other vendor reagents skipped before the count")
require_text("${policy}" "return !forVendorReagent || !IsVendorReagent(vendorReagents, otherReagentId);" "only the reagents no vendor sells hold a vendor reagent back")
require_text("${item_usage}" "keepCraft ? profession_use::KeepCraftStacks(stacks, sPlayerbotAIConfig.professionUseReagentKeepStacks) : stacks == 1" "kept below ReagentKeepStacks + 1 stacks")
require_text("${policy}" "return stacks < float(keepStacks) + 1.0f;" "keep limit ReagentKeepStacks + 1, like the reagent branch")
# Review: KeepCraftMaterials lets item-cheat roster bots reach the reagent walk;
# the null check holds for every bot (no switch).
require_order("${item_usage}" "if (!reqProto)" "AI_VALUE2(uint32, \"item count\", reqProto->Name1)" "reagent prototype checked before use")
# Critic B4.6: the reagent ids are configuration, not code.
require_text("${policy}" "inline bool IsVendorReagent(std::set<std::uint32_t> const& vendorReagents, std::uint32_t itemId)" "vendor reagent set from the config")
forbid_text("${policy}" "3371, 3372" "vendor reagent ids baked into the policy")
# The policy stays out of the botpch.h chain.
forbid_text("${config_header}" "ProfessionUsePolicy.h" "policy included from PlayerbotAIConfig.h")
forbid_text("${ai_header}" "ProfessionUsePolicy.h" "policy included from PlayerbotAI.h")

# Neutral defaults; the owner's values belong in the server profile.
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.RealReagents\", false" "real reagents default off")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.CraftFailBackoffSeconds\", 1800" "backoff default")
require_text("${config_source}" "std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.ProfessionUse.CraftFailBackoffSeconds\"" "negative backoff clamped to 0")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.KeepCraftMaterials\", false" "keep materials default off")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.ReagentKeepStacks\", 1" "keep stacks default")
require_text("${config_source}" "\"AiPlayerbot.ProfessionUse.VendorReagents\", \"\"" "vendor reagents default empty")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.RealReagents = 0" "documented real reagents switch")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.CraftFailBackoffSeconds = 1800" "documented backoff")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.KeepCraftMaterials = 0" "documented keep switch")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.ReagentKeepStacks = 1" "documented keep stacks")
require_text("${config_template}" "AiPlayerbot.ProfessionUse.VendorReagents =\n" "documented empty vendor reagent list")

message(STATUS "PROFESSION_USE_SOURCE_CONTRACT=PASS")
