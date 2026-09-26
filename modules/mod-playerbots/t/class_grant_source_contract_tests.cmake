function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AutoLearnSpellAction.cpp" learn)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #356: roster bots only, behind a switch; idempotent totem grant.
require_text("${learn}" "sPlayerbotAIConfig.classGrantEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())" "roster bots behind the switch")
require_text("${learn}" "if (IsClassGrantBot() && bot->getClass() == CLASS_SHAMAN)" "totems for shamans")
require_text("${learn}" "if (!bot->HasItemCount(grant.item, 1, true))" "totem only when not in bags or bank")
require_text("${learn}" "[ClassGrant] state=granted source=totem" "totem diagnostics")
require_text("${learn}" "[ClassGrant] state=granted source=quest" "quest spell diagnostics")
require_text("${config_source}" "\"AiPlayerbot.ClassGrant.Enabled\", false" "default off")
require_text("${config_template}" "AiPlayerbot.ClassGrant.Enabled = 0" "documented switch")
