# twow-repo#588 (BUFF-01, owner 14.10.2026): AiPlayerbot.Buff.RealPlayerStyle (default 0). Out-of-group buff targets like a
# real player: group, guild, a stranger only once per cooldown.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#588 buff targets: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#588 buff targets: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#588 buff targets: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#588 buff targets: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/values/PartyMemberWithoutAuraValue.cpp" value)
read_source("RandomPlayerbotMgr.cpp" mgr)

require_text("${config_h}" "bool buffRealPlayerStyle = false;" "switch default off")
require_text("${config_cpp}" "buffRealPlayerStyle = config.GetBoolDefault(\"AiPlayerbot.Buff.RealPlayerStyle\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Buff.RealPlayerStyle = 0" "documented")
require_text("${conf_dist}" "AiPlayerbot.Buff.StrangerCooldownSeconds = 900" "cooldown documented")

between("${value}" "Unit* FriendlyUnitWithoutAuraValue::Calculate()" "\n}\n" calc)
# #420 stays: bots grouped with a real player never look outside (ignoreOutOfGroup), switch or not.
require_order("${calc}" "bool const ignoreOutOfGroup = !ai::group_buff::MayBuffOutOfGroup(ai->IsInGroupWithRealPlayer());" "if (sPlayerbotAIConfig.buffRealPlayerStyle && !ignoreOutOfGroup)" "#420 decides first")
# Order: group, guards, guild, stranger with cooldown; the old path only without the switch.
require_order("${calc}" "if (Unit* member = FindPartyMember(predicate, true, ignoreTank))" "if (!ai->AllowActivity(OUT_OF_PARTY_ACTIVITY) || AI_VALUE2(uint32, \"current mount speed\", \"self target\"))" "group before the outside guards")
require_order("${calc}" "if (ai::buff_target::SameGuild(bot->GetGuildId(), player->GetGuildId()))" "if (!ai::buff_target::StrangerAllowed(now," "guild before strangers")
require_order("${calc}" "if (!ai::buff_target::StrangerAllowed(now," "SET_AI_VALUE2(int, \"manual int\", \"buff stranger next\"," "cooldown checked, then set")
require_order("${calc}" "if (!ai::buff_target::StrangerAllowed(now," "return stranger;" "stranger only after the cooldown check")
require_order("${calc}" "if (sPlayerbotAIConfig.buffRealPlayerStyle && !ignoreOutOfGroup)" "return FindPartyMember(predicate, ignoreOutOfGroup, ignoreTank);" "old path kept for switch off")
require_text("${calc}" "!predicate.Check(player)" "only players without the aura")

# Minute line only with the switch.
require_order("${mgr}" "if (sPlayerbotAIConfig.buffRealPlayerStyle)" "[BuffTargets] group=%llu guild=%llu stranger_bot=%llu stranger_player=%llu stranger_on_cooldown=%llu cooldown_s=%u" "minute line behind the switch")

message(STATUS "buff_target source contract passed")
