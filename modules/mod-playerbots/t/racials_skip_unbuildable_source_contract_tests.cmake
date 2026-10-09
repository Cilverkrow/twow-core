# twow-repo#541 (audit A13, card 65): RacialsStrategy registers "gift of the naaru", "mana tap" and
# "arcane torrent", but this vanilla build has no action creator for them (ActionContext.h,
# #ifndef MANGOSBOT_ZERO), so their queue entries can only end UNKNOWN. With
# AiPlayerbot.Perf.RacialsSkipUnbuildable = 1 these three nodes are not registered (read only under
# #ifdef MANGOSBOT_ZERO). Default 0 = the original twelve nodes in the original order with the original
# relevances. NOT strictly behaviour-neutral when on (engine re-push / prerequisite path in rare ticks),
# see the conf.dist text; enabling needs an OB-30 measurement and owner go.
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
    message(FATAL_ERROR "#541 A13: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A13: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A13: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A13: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A13: region start not found: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 tail)
  string(LENGTH "${start_marker}" start_len)
  string(SUBSTRING "${tail}" ${start_len} -1 after)
  string(FIND "${after}" "${end_marker}" end_offset)
  if(end_offset EQUAL -1)
    message(FATAL_ERROR "#541 A13: region end not found after ${start_marker}: ${end_marker}")
  endif()
  math(EXPR len "${start_len} + ${end_offset}")
  string(SUBSTRING "${tail}" 0 ${len} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/generic/RacialsStrategy.cpp" racials)
read_source("strategy/actions/ActionContext.h" action_context)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" config_dist)

# --- Switch: own member, default 0, read with default false, documented as 0 ---
require_text("${config_h}" "    bool perfRacialsSkipUnbuildable = false;  // twow-repo#541 (audit A13): "
  "A13 member with default false")
require_order("${config_h}" "bool perfTrainableSpellsPrecheck = false;" "bool perfRacialsSkipUnbuildable = false;"
  "A13 member after the earlier #541 audit switches")
require_count("${config_h}" "perfRacialsSkipUnbuildable" 1 "A13 member declared once")
require_text("${config_cpp}"
  "    perfRacialsSkipUnbuildable = config.GetBoolDefault(\"AiPlayerbot.Perf.RacialsSkipUnbuildable\", false);"
  "A13 switch read with default false")
require_count("${config_cpp}" "AiPlayerbot.Perf.RacialsSkipUnbuildable" 1 "A13 switch read once")
require_order("${config_cpp}" "perfTrainableSpellsPrecheck = config." "perfRacialsSkipUnbuildable = config."
  "A13 switch read next to the earlier #541 audit switches")
require_text("${config_dist}" "\nAiPlayerbot.Perf.RacialsSkipUnbuildable = 0\n" "A13 conf.dist entry with value 0")
require_count("${config_dist}" "AiPlayerbot.Perf.RacialsSkipUnbuildable =" 1 "A13 conf.dist entry once")
require_text("${config_dist}" "# twow-repo#541 (audit A13): 1 = the racials strategy no longer registers"
  "A13 conf.dist documentation")
require_text("${config_dist}" "# NOT strictly behaviour-neutral:" "A13 conf.dist behaviour disclosure")
require_order("${config_dist}" "\nAiPlayerbot.Perf.TrainableSpellsPrecheck = 0\n"
  "\nAiPlayerbot.Perf.RacialsSkipUnbuildable = 0\n" "A13 conf.dist entry next to the #541 performance switches")

# --- Premise: the three spells have no action creator in the vanilla build ---
string(FIND "${action_context}" "creators[\"mana tap\"]" mana_tap_creator)
string(FIND "${action_context}" "creators[\"arcane torrent\"]" arcane_torrent_creator)
string(FIND "${action_context}" "creators[\"gift of the naaru\"]" gift_creator)
if(mana_tap_creator EQUAL -1 OR arcane_torrent_creator EQUAL -1 OR gift_creator EQUAL -1)
  message(FATAL_ERROR "#541 A13: ActionContext.h racial creators for mana tap / arcane torrent / gift of the naaru missing")
endif()
string(SUBSTRING "${action_context}" 0 ${mana_tap_creator} before_mana_tap)
string(FIND "${before_mana_tap}" "#ifndef MANGOSBOT_ZERO" zero_gate REVERSE)
if(zero_gate EQUAL -1)
  message(FATAL_ERROR "#541 A13: ActionContext.h mana tap creator is not under #ifndef MANGOSBOT_ZERO - re-check A13")
endif()
string(SUBSTRING "${action_context}" ${zero_gate} -1 zero_tail)
# The first preprocessor line after that #ifndef must be its #endif (no #else / nested #if inside).
string(FIND "${zero_tail}" "\n#" zero_end)
if(zero_end EQUAL -1)
  message(FATAL_ERROR "#541 A13: ActionContext.h #ifndef MANGOSBOT_ZERO block not closed")
endif()
math(EXPR zero_end_text "${zero_end} + 1")
string(SUBSTRING "${zero_tail}" ${zero_end_text} 6 zero_end_directive)
if(NOT zero_end_directive STREQUAL "#endif")
  message(FATAL_ERROR "#541 A13: ActionContext.h #ifndef MANGOSBOT_ZERO block must close with #endif before any other directive, found: ${zero_end_directive}")
endif()
math(EXPR zero_end_abs "${zero_gate} + ${zero_end}")
foreach(creator_pos ${mana_tap_creator} ${arcane_torrent_creator} ${gift_creator})
  if(NOT zero_gate LESS creator_pos OR NOT creator_pos LESS zero_end_abs)
    message(FATAL_ERROR "#541 A13: ActionContext.h mana tap / arcane torrent / gift of the naaru creators must stay in one #ifndef MANGOSBOT_ZERO block - re-check A13 if the vanilla build gains them")
  endif()
endforeach()

# --- RacialsStrategy: gate, two guarded blocks, nine untouched nodes ---
require_text("${racials}" "#include \"playerbot/PlayerbotAIConfig.h\"" "config include in RacialsStrategy.cpp")
require_count("${racials}" "perfRacialsSkipUnbuildable" 1 "A13 switch read once in RacialsStrategy.cpp")
region("${racials}" "void RacialsStrategy::InitNonCombatTriggers(" "void RacialsStrategy::InitCombatTriggers(" non_combat)
region("${racials}" "void RacialsStrategy::InitCombatTriggers(" "\n}\n" combat)
require_text("${combat}" "    InitNonCombatTriggers(triggers);" "combat triggers still delegate to the non-combat list")
require_count("${combat}" "new TriggerNode(" 0 "combat triggers add no own nodes")
# The combat list is exactly the non-combat list: the body is the delegation and nothing else.
if(NOT combat STREQUAL "void RacialsStrategy::InitCombatTriggers(std::list<TriggerNode*>& triggers)\n{\n    InitNonCombatTriggers(triggers);")
  message(FATAL_ERROR "#541 A13: InitCombatTriggers must only delegate to InitNonCombatTriggers, found:\n${combat}")
endif()

set(gate "#ifdef MANGOSBOT_ZERO\n    const bool skipUnbuildable = sPlayerbotAIConfig.perfRacialsSkipUnbuildable;\n#else\n    const bool skipUnbuildable = false;\n#endif\n")
require_text("${non_combat}" "${gate}" "A13 gate: switch read only under #ifdef MANGOSBOT_ZERO, else false")
require_count("${non_combat}" "if (!skipUnbuildable)" 2 "A13 guarded blocks")
require_count("${non_combat}" "skipUnbuildable" 4 "A13 local flag uses (two definitions, two guards)")
require_count("${non_combat}" "new TriggerNode(" 13 "racial nodes (12 active + commented-out shadowmeld)")
require_count("${non_combat}" "triggers.push_back(" 13 "racial pushes (12 active + commented-out shadowmeld)")

set(gift_node "    if (!skipUnbuildable)\n    {\n        triggers.push_back(new TriggerNode(\n            \"low health\",\n            NextAction::array(0, new NextAction(\"gift of the naaru\", 71.0f), NULL)));\n    }\n")
set(unbuildable_tail "    if (!skipUnbuildable)\n    {\n        triggers.push_back(new TriggerNode(\n            \"mana tap\",\n            NextAction::array(0, new NextAction(\"mana tap\", 71.0f), NULL)));\n\n        triggers.push_back(new TriggerNode(\n            \"arcane torrent\",\n            NextAction::array(0, new NextAction(\"arcane torrent\", 71.0f), NULL)));\n    }\n}\n")
require_text("${non_combat}" "${gift_node}" "OFF path: original gift of the naaru node (low health, 71) inside the first guard only")
require_text("${non_combat}" "${unbuildable_tail}" "OFF path: original mana tap and arcane torrent nodes (71) inside the last guard, at the end")
require_count("${racials}" "NextAction(\"gift of the naaru\"" 1 "gift of the naaru node")
require_count("${racials}" "NextAction(\"mana tap\"" 1 "mana tap node")
require_count("${racials}" "NextAction(\"arcane torrent\"" 1 "arcane torrent node")

# The nine buildable nodes (and the commented-out shadowmeld) stay unconditional, byte for byte, in the
# original order between the two guarded blocks. Entry = comment flag | trigger | action | relevance
# (the node text holds ';', so it is built per entry instead of being stored in a CMake list).
set(kept_nodes
  "|melee medium aoe|war stomp|71.0f"
  "|war stomp|war stomp|71.0f"
  "|cannibalize|cannibalize|71.0f"
  "|perception|perception|71.0f"
  "|rooted|escape artist|71.0f"
  "|will of the forsaken|will of the forsaken|71.0f"
  "commented|shadowmeld|shadowmeld|71.0f"
  "|berserking|berserking|58.0f"
  "|blood fury|blood fury|71.0f"
  "|stoneform|stoneform|71.0f")
string(FIND "${non_combat}" "${gate}" previous)
string(FIND "${non_combat}" "${gift_node}" gift_pos)
string(FIND "${non_combat}" "${unbuildable_tail}" tail_pos)
if(NOT previous LESS gift_pos)
  message(FATAL_ERROR "#541 A13: the gate must come before the gift of the naaru node")
endif()
# No statement between the gate and the first guard (one blank line, then the gift of the naaru guard).
require_text("${non_combat}" "${gate}\n${gift_node}" "A13 gate directly followed by the gift of the naaru guard")
# Between the opening brace of InitNonCombatTriggers and the gate there are only comment lines.
string(FIND "${non_combat}" "\n{\n" body_open)
if(body_open EQUAL -1 OR NOT body_open LESS previous)
  message(FATAL_ERROR "#541 A13: InitNonCombatTriggers opening brace not found before the gate")
endif()
math(EXPR head_start "${body_open} + 3")
math(EXPR head_len "${previous} - ${head_start}")
string(SUBSTRING "${non_combat}" ${head_start} ${head_len} head)
string(REGEX REPLACE "    //[^\n]*\n" "" head_rest "${head}")
if(NOT head_rest STREQUAL "")
  message(FATAL_ERROR "#541 A13: only comment lines may precede the gate in InitNonCombatTriggers, found:\n${head_rest}")
endif()
set(previous ${gift_pos})
foreach(entry IN LISTS kept_nodes)
  string(REPLACE "|" ";" fields "${entry}")
  list(GET fields 0 commented)
  list(GET fields 1 trigger_name)
  list(GET fields 2 action_name)
  list(GET fields 3 relevance)
  set(node "triggers.push_back(new TriggerNode(\n        \"${trigger_name}\",\n        NextAction::array(0, new NextAction(\"${action_name}\", ${relevance}), NULL)));")
  if(commented STREQUAL "commented")
    set(node "\n    /*${node}*/\n")
  else()
    set(node "\n    ${node}\n")
  endif()
  string(FIND "${non_combat}" "${node}" node_pos)
  if(node_pos EQUAL -1)
    message(FATAL_ERROR "#541 A13: buildable racial node changed or missing: ${node}")
  endif()
  if(NOT previous LESS node_pos OR NOT node_pos LESS tail_pos)
    message(FATAL_ERROR "#541 A13: buildable racial node out of order or inside a guard: ${node}")
  endif()
  set(previous ${node_pos})
endforeach()

# Nothing between the first guarded block and the last guarded block may be conditional.
string(LENGTH "${gift_node}" gift_len)
math(EXPR middle_start "${gift_pos} + ${gift_len}")
math(EXPR middle_len "${tail_pos} - ${middle_start}")
string(SUBSTRING "${non_combat}" ${middle_start} ${middle_len} middle)
forbid_text("${middle}" "skipUnbuildable" "guard around a buildable racial node")
forbid_text("${middle}" "\n    if (" "condition around a buildable racial node")
forbid_text("${middle}" "\n#" "preprocessor condition around a buildable racial node")
require_count("${middle}" "new TriggerNode(" 10 "buildable racial nodes between the guards (9 + commented-out shadowmeld)")

message(STATUS "racials_skip_unbuildable source contract passed")
