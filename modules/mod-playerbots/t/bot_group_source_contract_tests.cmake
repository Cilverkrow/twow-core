if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

# twow-repo#365 step 1: config keys and a [BotGroup] diagnostic only. The keys
# default off, the diagnostic is gated, and it mutates nothing.
file(READ "${PB_SOURCE_DIR}/BotGroupDiagnostics.cpp" diag)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
file(READ "${PB_SOURCE_DIR}/strategy/actions/AcceptInvitationAction.h" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/LeaveGroupAction.cpp" leave)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}")
  endif()
endfunction()

require_text("${config}" "GetBoolDefault(\"AiPlayerbot.BotGroups.Enabled\", false)" "Enabled defaults off")
require_text("${config}" "GetBoolDefault(\"AiPlayerbot.BotGroups.Diagnostics\", false)" "Diagnostics defaults off")
require_text("${config}" "ClampMaxBots(config.GetIntDefault(\"AiPlayerbot.BotGroups.MaxBots\", 3))" "MaxBots default 3, clamped")
require_text("${config}" "GetIntDefault(\"AiPlayerbot.BotGroups.LevelWindow\", 3)" "LevelWindow default 3")
foreach(line
    "AiPlayerbot.BotGroups.Enabled = 0"
    "AiPlayerbot.BotGroups.MaxBots = 3"
    "AiPlayerbot.BotGroups.LevelWindow = 3"
    "AiPlayerbot.BotGroups.Diagnostics = 0")
  require_text("${dist}" "${line}" "conf.dist entry ${line}")
endforeach()

require_text("${diag}" "if (!sPlayerbotAIConfig.botGroupsDiagnostics" "diagnostic gate first")
require_text("${diag}" "[BotGroup] event=%s" "log tag")
require_text("${diag}" "Classify(" "verdict from the tested policy")

# Step 1 changes no behaviour: no group, quest or database mutation here.
foreach(forbidden "AddQuest" "RemoveQuest" "SetQuestStatus" "SetQuestSlot" "DropQuest"
    "RemoveMember" "AddMember" "Disband" "Uninvite" "HandleGroup" "Database" "PQuery" "PExecute")
  forbid_text("${diag}" "${forbidden}" "mutation ${forbidden} in the diagnostic")
endforeach()
# Enabled is reported, not acted on, until the formation step lands.
forbid_text("${accept}" "botGroupsEnabled" "behaviour on BotGroups.Enabled in accept")
forbid_text("${leave}" "botGroupsEnabled" "behaviour on BotGroups.Enabled in leave")

require_text("${accept}" "bot_group::LogMembership(bot, bot->GetGroup(), \"join\");" "join diagnostic")
require_text("${leave}" "bot_group::LogMembership(bot, group, \"leave\");" "leave diagnostic")

# The leave diagnostic runs before the Core leave handler, while the group is still live.
string(FIND "${leave}" "bot_group::LogMembership(bot, group, \"leave\");" logged)
string(FIND "${leave}" "HandleGroupDisbandOpcode(p)" left)
if(logged EQUAL -1 OR left EQUAL -1 OR NOT logged LESS left)
  message(FATAL_ERROR "the leave diagnostic must run before the bot leaves")
endif()

message(STATUS "BOT_GROUP_SOURCE_CONTRACT=PASS")
