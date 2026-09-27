function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Unexpected ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AdhocGroupAction.cpp" action)
file(READ "${PB_SOURCE_DIR}/AiFactory.cpp" factory)
file(READ "${PB_SOURCE_DIR}/strategy/StrategyContext.h" strategies)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ActionContext.h" actions)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# Only roster bots, only while BotGroups.Enabled (#365 step 2, design 3.1).
require_text("${factory}" "if (sPlayerbotAIConfig.botGroupsEnabled && !player->InBattleGround() &&" "strategy gated by BotGroups.Enabled")
require_text("${strategies}" "creators[\"adhoc group\"]" "strategy registered")
require_text("${actions}" "creators[\"ad-hoc group\"]" "action registered")

# Candidates: same objective key, radius, level window, pair cooldown, no world scan (#351).
require_text("${action}" "AI_VALUE(std::list<ObjectGuid>, \"nearest friendly players\")" "neighbours from the existing value")
require_text("${action}" "CurrentObjective(other) != key" "same objective key")
require_text("${action}" "adhoc_group::PairCooldowns().IsBlocked(" "pair cooldown")
require_text("${action}" "adhoc_group::LevelWindowOk(" "level window")
require_text("${action}" "return;  // at most one invite per scan" "one invite per scan")
require_text("${action}" "inviter->GetSession()->HandleGroupInviteOpcode(p);" "existing group invite path")
forbid_text("${action}" "GetPlayers()" "a world scan")

# Group: registered in memory, free-for-all loot, leave via the Core path.
require_text("${action}" "group->SetLootMethod(FREE_FOR_ALL);" "free-for-all loot")
require_text("${action}" "adhoc_group::Groups().Register(" "registry")
require_text("${action}" "ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);" "Core leave path (#301)")
require_text("${action}" "[BotGroup] kind=adhoc event=leave" "visible leave with reason")
forbid_text("${action}" "AddQuest" "quest log changes")
forbid_text("${action}" "RemoveQuest" "quest log changes")

# Keys, defaults.
require_text("${config_source}" "\"AiPlayerbot.BotGroups.AdHoc.ScanIntervalSeconds\", 10" "scan interval default")
require_text("${config_source}" "\"AiPlayerbot.BotGroups.AdHoc.Radius\", 50.0f" "radius default")
require_text("${config_source}" "\"AiPlayerbot.BotGroups.AdHoc.PairCooldownSeconds\", 600" "pair cooldown default")
require_text("${config_template}" "AiPlayerbot.BotGroups.AdHoc.Radius = 50" "documented radius")
