cmake_policy(VERSION 3.16)
# twow-repo#601 A3 step 1 (owner 04.11.2026 via OB-00, ws-00/projekt-durchleuchtung/A3-BOT-LOGIK.md 1.3; CLI-Deep):
# AiPlayerbot.Rpg.TurnInFirst (cohort modes, default 0). The upstream RPG sequencing (one weighted-random sub-action per "rpg" pick,
# RpgActionMultiplier x0.1 for every other RPG action, a 10 % / 30 % "rpg cancel" after each RPG action) made a hand-in at the
# quest giver wait or get lost. With the key a possible "rpg end quest" is picked first, never multiplied down, and no cancel is
# rolled while a hand-in at the rpg target is possible. Off: the sequencing stays exactly as it was.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "rpg turn-in first: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "rpg turn-in first: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "rpg turn-in first: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/RpgSubActions.h" sub_h)
read_source("strategy/actions/RpgSubActions.cpp" sub_cpp)
read_source("strategy/actions/RpgAction.cpp" rpg_action)
read_source("strategy/generic/RpgStrategy.cpp" rpg_strategy)

# 1. The key: default 0, an unknown mode is off, documented, echoed.
require_text("${config_h}" "uint32 rpgTurnInFirst = 0;" "member off")
require_text("${config_cpp}" "rpgTurnInFirst = config.GetIntDefault(\"AiPlayerbot.Rpg.TurnInFirst\", 0);" "default 0")
require_text("${config_cpp}" "if (rpgTurnInFirst > 3)\n        rpgTurnInFirst = 0;" "unknown mode off")
require_text("${config_cpp}" "{\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.Rpg.TurnInFirst = 0\n" "documented off")

# 2. One gate: cohort selection, never a bot led by a real player.
require_text("${sub_h}" "bool RpgTurnInFirst(PlayerbotAI* ai);" "shared gate declared")
require_text("${sub_cpp}" "return ai && ai->GetBot() && ai::cohort::Applies(sPlayerbotAIConfig.rpgTurnInFirst, ai->GetBot()->GetGUIDLow()) && !ai->HasRealPlayerMaster();" "gate")

# 3. The multiplier: the hand-in exempt before the x0.1 sequencing rule.
require_order("${rpg_strategy}" "if (name == RpgEndQuestActionName && RpgTurnInFirst(ai))\n            return 1.0f;" "if (!nextAction.empty() && name != nextAction)\n            return 0.1f;" "end quest exempt before the x0.1 rule")

# 4. The pick: a possible hand-in first; the weighted draw only without one (or with the key off).
require_order("${rpg_action}" "if (candidate->getName() == RpgEndQuestActionName)" "WeightedShuffle(actions.begin(), actions.end(), relevances.begin(), relevances.end(), gen);" "end quest picked before the draw")
require_text("${rpg_action}" "    if (!action)\n    {\n        std::mt19937 gen(time(0));" "the draw only without a hand-in")

# 5. The cancel: not while a hand-in at the rpg target is possible; the value read only when the roll would cancel.
require_order("${sub_cpp}" "if (nextAction == \"rpg\" && urand(0, 100) < goToDifferentTargetChance)" "AI_VALUE2(bool, \"can turn in quest npc\", entry)" "value read only after the roll")
require_text("${sub_cpp}" "[RpgTurnIn] state=cancel_skipped bot=%u level=%u entry=%d" "visible skip")
require_text("${sub_cpp}" "        else\n            nextAction = \"rpg cancel\";" "otherwise the cancel as before")
