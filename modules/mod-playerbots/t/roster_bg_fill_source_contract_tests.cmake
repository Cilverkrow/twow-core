# twow-repo#517 (owner 04.10.2026, train 9 -> 9.x hotfix): roster bots fill a battleground (and the
# 3v3 Blood Ring) when a real player queues, behind AiPlayerbot.RosterBgFill (default 0).
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
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

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_offset)
  string(FIND "${text}" "${second}" second_offset)
  if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR NOT first_offset LESS second_offset)
    message(FATAL_ERROR "Order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from start_marker up to end_marker.
function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "Missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "Missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.h" mgr_h)
file(READ "${PB_SOURCE_DIR}/strategy/actions/BattleGroundJoinAction.cpp" join)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_dist)

# Switch, default off.
require_text("${config_h}" "bool rosterBgFill = false;" "RosterBgFill member")
require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.RosterBgFill\", false)" "RosterBgFill default off")
require_text("${config_dist}" "AiPlayerbot.RosterBgFill = 0" "RosterBgFill documented off")

# The roster path runs CheckBgQueue (switch), before its return; no CheckLfgQueue (dungeons are #548).
region("${mgr}" "if (sPlayerbotAIConfig.persistentActiveRosterEnabled)\n    {\n        if (!persistentRosterInitialized)" "    ScaleBotActivity();\n    if (sPlayerbotAIConfig.asyncBotLogin)" roster_branch)
require_text("${roster_branch}" "if (sPlayerbotAIConfig.rosterBgFill && sPlayerbotAIConfig.randomBotJoinBG)\n            CheckBgQueue();" "roster path checks the BG queue with the switch")
require_order("${roster_branch}" "CheckBgQueue();" "PlayerbotHolder::UpdateAIInternal(elapsed, minimal);\n        return;" "BG queue checked before the roster return")
reject_text("${roster_branch}" "CheckLfgQueue()" "LFG queue in the roster path (#548)")

# CheckBgQueue: count keys filled first, roster bots counted, no bot loop without a real player.
region("${mgr}" "void RandomPlayerbotMgr::CheckBgQueue()" "void RandomPlayerbotMgr::CheckLfgQueue()" check_bg)
require_order("${check_bg}" "NeedBots[j][i][1] = false;" "bgCountsReady = true;" "keys filled before they are marked ready")
require_text("${check_bg}" "if (!IsFreeBot(bot) && !IsPersistentRosterMember(bot->GetGUIDLow()))" "roster bots counted")
require_text("${check_bg}" "[BGFill] state=queue bg=%u bracket=%u real_a=%u real_h=%u" "queue line")
require_order("${check_bg}" "if (!realPlayerQueued)\n            return;" "ForEachPlayerbot(" "no bot loop without a real player")
require_text("${mgr_h}" "std::atomic<bool> bgCountsReady{false};" "ready flag")

# Bot side: roster bots only with the switch, after the keys exist, and only when free.
region("${join}" "bool BGJoinAction::isUseful()" "bool BGJoinAction::JoinQueue(" join_useful)
require_order("${join_useful}" "if (!sPlayerbotAIConfig.rosterBgFill || !sRandomPlayerbotMgr.bgCountsReady)" "shouldJoinBg(queueTypeId, bracketId)" "roster gate before any count read")
require_text("${join_useful}" "bot->GetGroup() || bot->GetMap()->Instanceable() || bot->IsTaxiFlying() || bot->GetTransport()" "only free roster bots")
# No Refresh (free repair, money, consumables, AI reset) for roster bots.
require_text("${join}" "if (sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()))\n       sLog.outBasic(\"[BGFill] state=join" "join line instead of Refresh for roster bots")
require_text("${join}" "   else\n       sRandomPlayerbotMgr.Refresh(bot);" "Refresh kept for other random bots")

message(STATUS "ROSTER_BG_FILL_SOURCE_CONTRACT=PASS")
