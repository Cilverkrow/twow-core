# twow-repo#541 (deep dive R2b, owner 08.10.2026): a solo bot (no group, no master) skips the
# shared-targets lookup in AttackersValue::Calculate ("nearest friendly players" + loop) and counts
# only its own attackers. Groups and bots with a master keep the old path. Switch
# AiPlayerbot.SoloOwnAttackersOnly, default 1.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 R2b: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 R2b: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("strategy/values/AttackersValue.cpp" attackers)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

set(solo "bool const soloBot = !bot->GetGroup() && !GetMaster();")
set(gate "if (sPlayerbotAIConfig.shareTargets && !(soloBot && sPlayerbotAIConfig.soloOwnAttackersOnly))")
set(lookup "GetValue<std::list<ObjectGuid> >(\"nearest friendly players\")")

# Solo = no group and no master; the gate wraps the lookup (it is the only shareTargets check).
require_order("${attackers}" "${solo}" "${gate}" "solo before the gate")
require_order("${attackers}" "${gate}" "${lookup}" "gate before the lookup")
string(FIND "${attackers}" "if (sPlayerbotAIConfig.shareTargets)" old_gate)
if(NOT old_gate EQUAL -1)
  message(FATAL_ERROR "#541 R2b: the shared-targets lookup must be gated for solo bots")
endif()

# The own-targets path stays, and groups and masters still add theirs.
require_order("${attackers}" "${lookup}" "AddTargetsOf(bot, targets, invalidTargets, getOne);" "own targets after the lookup")
require_order("${attackers}" "AddTargetsOf(bot, targets, invalidTargets, getOne);" "AddTargetsOf(group, targets, invalidTargets, getOne);" "group targets")
require_text("${attackers}" "AddTargetsOf(master, targets, invalidTargets, getOne);" "master targets")

# Switch: default on, documented.
require_text("${config_h}" "bool soloOwnAttackersOnly;" "switch member")
require_text("${config_cpp}" "soloOwnAttackersOnly = config.GetBoolDefault(\"AiPlayerbot.SoloOwnAttackersOnly\", true);" "switch default 1")
require_text("${conf_dist}" "#AiPlayerbot.SoloOwnAttackersOnly = 1" "documented key")

message(STATUS "solo_attackers source contract passed")
