function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/LootRollAction.cpp" roll)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #341: roster bots on their own only, behind a switch.
require_text("${roll}" "sPlayerbotAIConfig.lootRollRoleAware &&" "switch")
require_text("${roll}" "IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster()" "roster bots on their own")
require_text("${roll}" "loot_roll::ForRecipe(facts)" "recipe rule")
require_text("${roll}" "loot_roll::ForGear(usage == ItemUsage::ITEM_USAGE_EQUIP, usage == ItemUsage::ITEM_USAGE_BAD_EQUIP)" "gear rule")
require_text("${roll}" "[LootRoll] vote=%s reason=%s" "visible diagnostics")
# A player who needs still wins over a bot (etiquette unchanged).
require_text("${roll}" "if (humanNeeds && vote == ROLL_NEED)" "player etiquette kept")
require_text("${config_source}" "\"AiPlayerbot.LootRoll.RoleAware\", false" "default off")
require_text("${config_template}" "AiPlayerbot.LootRoll.RoleAware = 0" "documented switch")
require_text("${config_template}" "AiPlayerbot.LootRoll.Trace = 0" "documented trace")
