# twow-repo#601 decision order (AiPlayerbot.Grind.QuestFirstOrder, owner 20.10.2026 via OB-00): "attack anything" of quest-first
# roster bots on their own takes only quest objectives, attackers and creatures in the way, so it no longer wins over the walk
# to the quest giver, quest travel and turn-ins. Switch first, GrindMode 2 keeps precedence, the way test checks the distance
# itself (the core's isInFront ignores it), [QuestOrder] minute line, ConfigEcho.

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
    message(FATAL_ERROR "#601 QuestFirstOrder: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#601 QuestFirstOrder: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 QuestFirstOrder: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" len)
  set(n 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#601 QuestFirstOrder: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("QuestFirstOrderPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/values/GrindTargetValue.cpp" grind_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_cpp)

# Switch, default 0, documented, echoed.
require_text("${config_h}" "bool grindQuestFirstOrder = false;" "default off")
require_text("${config_cpp}" "grindQuestFirstOrder = config.GetBoolDefault(\"AiPlayerbot.Grind.QuestFirstOrder\", false);" "config read")
require_text("${config_cpp}" "{\"Grind.QuestFirstOrder\", Value(grindQuestFirstOrder)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.Grind.QuestFirstOrder = 0\n" "documented, 0 by default")
forbid_text("${conf_dist}" "AiPlayerbot.Grind.QuestFirstOrder = 1" "default 1 in the dist")

# Pure rules.
require_text("${policy}" "return enabled && questFirstRosterBot && !realPlayerMaster && level < maxLevel;" "population")
require_text("${policy}" "return !isPlayer && inFront && withinAggro && attacksOnSight && mobLevel < botLevel + 3;" "in the way, with the distance")
require_text("${policy}" "return !applies || neededForQuest || blocksPath;" "target rule")
forbid_text("${policy}" "#include \"" "game headers in the pure policy")

# Population: the config first, then the quest-first roster population (as IsQuestNoGrind).
require_text("${ai_cpp}" "bool PlayerbotAI::IsQuestFirstOrder()\n{\n    if (!sPlayerbotAIConfig.grindQuestFirstOrder || !bot)\n        return false;" "switch read first")
require_text("${ai_cpp}" "sPlayerbotAIConfig.questFirstProgressionEnabled && sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()),\n        HasRealPlayerMaster(), bot->GetLevel(), sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));" "quest-first roster population")

# GrindTargetValue: GrindMode 2 first, attackers untouched (loop before), the skip after the GrindMode 2 skip.
require_text("${grind_cpp}" "bool const questOrder = !noGrind && ai->IsQuestFirstOrder();" "GrindMode 2 keeps precedence")
count_text("${grind_cpp}" "ai->IsQuestFirstOrder()" 1 "one population read per pass")
require_text("${grind_cpp}" "if (questOrder && !ai::quest_first_order::TargetAllowed(true, needForQuest, !needForQuest &&" "skip gated by the switch, way test only without quest need")
require_text("${grind_cpp}" "bot->GetDistance(unit) <= unit->GetAttackDistance(bot) * 1.5f" "the distance itself (isInFront ignores it)")
require_order("${grind_cpp}" "\"(hostile) selected.\"" "bool const questOrder" "attacker loop before the rule")
require_order("${grind_cpp}" "ignored (GrindMode 2: not needed for a quest)." "ignored (QuestFirstOrder: not needed for a quest, not in the way)." "after the GrindMode 2 skip")
require_order("${grind_cpp}" "ignored (QuestFirstOrder: not needed for a quest, not in the way)." "ignored (not needed for active quest)." "before the old random skips")
require_text("${grind_cpp}" "if (!result && questOrderSkipped && !assistCount)\n        PlayerbotAI::NoteQuestOrder(uint8(ai::quest_first_order::EmptyEvent(AI_VALUE(bool, \"travel target active\"))));" "empty pass: quest work or no quest")

# AttackAnythingAction itself unchanged (the target value carries the rule).
forbid_text("${choose_cpp}" "QuestFirstOrder" "a second rule in AttackAnythingAction")
forbid_text("${choose_cpp}" "grindQuestFirstOrder" "a second rule in AttackAnythingAction")

# Minute line: gated by the switch alone, called from the world update.
require_text("${mgr_cpp}" "void ReportQuestOrder()\n{\n    if (!sPlayerbotAIConfig.grindQuestFirstOrder)\n        return;" "minute line gated by the switch")
require_text("${mgr_cpp}" "    ReportQuestOrder();" "minute line called")
require_text("${ai_cpp}" "return ai::quest_first_order::FormatMinuteLine(counts);" "minute line format")

message(STATUS "quest_first_order source contract passed")
