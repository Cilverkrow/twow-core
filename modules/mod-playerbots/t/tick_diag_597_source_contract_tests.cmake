# twow-repo#597 (TICK-01, Refs #592 #541): module parts of the tick diagnostics, log only.
# AiPlayerbot.BotUpdateTraceTick (default 0): " tick=<world loop> region=<map>.<inst>" at the END of the
# [BotUpdateSlow] line. AiPlayerbot.WorldBotsHookTrace (default 0): " hook_ms= remote_ms= ahbot_ms=
# dungeonclear_ms=" at the end of the [WorldBots] minute line. Off: both lines byte-identical, the hook
# calls untimed. Pure logic: t/tick_diag_597_tests.cpp.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#597: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#597: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotScripts.cpp" scripts)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("RandomPlayerbotMgr.h" mgr_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("BotUpdateTrace.h" trace_h)
read_source("WorldBotsTracePolicy.h" window_h)

# Switches default 0, documented with that default.
require_text("${config_h}" "bool botUpdateTraceTick = false;" "BotUpdateTraceTick member default off")
require_text("${config_h}" "bool worldBotsHookTrace = false;" "WorldBotsHookTrace member default off")
require_text("${config_cpp}" "botUpdateTraceTick = config.GetBoolDefault(\"AiPlayerbot.BotUpdateTraceTick\", false);" "BotUpdateTraceTick default 0")
require_text("${config_cpp}" "worldBotsHookTrace = config.GetBoolDefault(\"AiPlayerbot.WorldBotsHookTrace\", false);" "WorldBotsHookTrace default 0")
require_text("${conf_dist}" "\nAiPlayerbot.BotUpdateTraceTick = 0\n" "BotUpdateTraceTick documented 0")
require_text("${conf_dist}" "\nAiPlayerbot.WorldBotsHookTrace = 0\n" "WorldBotsHookTrace documented 0")

# [BotUpdateSlow]: the suffix is appended to the end of the description only with the switch; the line
# format itself is unchanged (description last), so the suffix ends the line.
require_text("${mgr}" "\"[BotUpdateSlow] us=%llu %s\"" "line format unchanged, description last")
require_order("${scripts}" "\" action=\" + (engine ? engine->GetLastAction() : std::string(\"-\"));" "if (sPlayerbotAIConfig.botUpdateTraceTick)\n                            who += ai::bot_update::TickRegionSuffix(World::m_worldLoopCounter, player->GetMapId(), player->GetInstanceId());" "suffix after action=, only with the switch")
require_text("${trace_h}" "return \" tick=\" + std::to_string(worldLoop) + \" region=\" + std::to_string(mapId) + \".\" + std::to_string(instanceId);" "tick/region format")

# [WorldBots]: the hook calls stay plain when off; the suffix is empty when off.
string(FIND "${scripts}" "void OnUpdate(uint32 diff) override" hook_at)
if(hook_at EQUAL -1)
  message(FATAL_ERROR "#597: PlayerbotWorldScript::OnUpdate not found")
endif()
string(SUBSTRING "${scripts}" ${hook_at} 2600 hook)
require_order("${hook}" "if (!sPlayerbotAIConfig.worldBotsHookTrace || !sPlayerbotAIConfig.worldBotsTrace)\n            {\n                sRandomPlayerbotMgr.RunQueuedRemoteCommands();\n\n                sRandomPlayerbotMgr.UpdateAI(diff);\n                auctionbot.Update();\n                return;\n            }" "uint64 const hookStartUs = ai::bot_update::NowUs();" "off path first, untimed")
require_text("${hook}" "sRandomPlayerbotMgr.AddWorldBotsHook(hookEndUs - hookStartUs, remoteEndUs - hookStartUs, hookEndUs - ahbotStartUs);" "hook parts recorded")
require_text("${mgr}" "std::string const hook = w.HookSuffix(sPlayerbotAIConfig.worldBotsHookTrace);" "suffix only with the switch")
require_text("${mgr}" "teleport_ack_calls=%u online=%u%s\"" "suffix at the end of the [WorldBots] line")
require_text("${window_h}" "if (!on)\n            return std::string();" "empty suffix when off")
require_text("${window_h}" "\" hook_ms=%u remote_ms=%u ahbot_ms=%u dungeonclear_ms=%u\"" "hook suffix format")
require_text("${mgr_h}" "void AddWorldBotsDungeonClearUs(uint64 us) { worldBotsWindow.AddDungeonClear(us); }" "dungeon-clear hook entry")

# The dungeon-clear reaper hook (own WorldScript in mod-dungeon-clear) times itself only with both switches.
# Fails closed (review follow-up): tests.cmake passes DC_SOURCE_FILE; a missing file fails the contract instead
# of skipping these checks. Without DC_SOURCE_FILE (manual run) the in-tree path next to this module is used.
if(DEFINED DC_SOURCE_FILE)
  set(dc_path "${DC_SOURCE_FILE}")
else()
  set(dc_path "${PB_SOURCE_DIR}/../../../mod-dungeon-clear/src/DungeonClearModule.cpp")
endif()
if(NOT EXISTS "${dc_path}")
  message(FATAL_ERROR "#597: dungeon-clear hook source not found: ${dc_path}")
endif()
file(READ "${dc_path}" dc)
string(REPLACE "\r" "" dc "${dc}")
require_text("${dc}" "bool const hookTrace = sPlayerbotAIConfig.worldBotsHookTrace && sPlayerbotAIConfig.worldBotsTrace;" "dungeon-clear hook switch")
require_text("${dc}" "uint64 const hookStartUs = hookTrace ? ai::bot_update::NowUs() : 0;" "no clock read when off")
require_order("${dc}" "uint64 const hookStartUs = hookTrace ? ai::bot_update::NowUs() : 0;" "DcFollowerLifecycle::ReapOrphanedFollows();\n        // Authoritative" "timed from the start of the hook")
require_text("${dc}" "if (hookTrace)\n            sRandomPlayerbotMgr.AddWorldBotsDungeonClearUs(ai::bot_update::NowUs() - hookStartUs);" "recorded at the end of the hook")

message(STATUS "TICK_DIAG_597_SOURCE_CONTRACT=PASS")
