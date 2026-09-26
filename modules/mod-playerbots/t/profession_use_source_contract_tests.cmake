function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
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
