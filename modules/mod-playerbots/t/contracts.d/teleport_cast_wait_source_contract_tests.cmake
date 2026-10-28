# twow-repo#333 finding 4 (CLI-Berufe pattern hunt 27.10.2026, OB-00; M1r run 4: Hearthstone 21 x INTERRUPTED + 1 x
# SPELL_IN_PROGRESS against 8 x OK). The travel step (HandleSpecialMovement), unstuck and zone escape started the hearthstone
# (and the travel step a teleport spell) through DoSpecificAction; the engine then set the AI wait to the outer action's own
# duration, so the cast wait of the use action was lost, the next pass failed the step and reset the path, and the next
# movement interrupted the cast. Now every site holds for the cast (the wait taken from the spell), and the travel step and
# unstuck wait for a teleport the bot already prepares. Rules: t/teleport_cast_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#333 finding 4 teleport cast wait: missing ${description}: ${needle}")
  endif()
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" found)
  if(NOT found EQUAL ${expected})
    message(FATAL_ERROR "#333 finding 4 teleport cast wait: ${description} (found ${found}, expected ${expected}): ${needle}")
  endif()
endfunction()

read_source("TeleportCastPolicy.h" policy)
read_source("TeleportCastWait.h" wait_h)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("strategy/actions/UnstuckAction.cpp" unstuck_cpp)
read_source("strategy/actions/ZoneEscapeActions.cpp" escape_cpp)

# The rules and the spell-side wait (rest of the cast + react delay, only for the bot's own PREPARING generic spell).
require_text("${policy}" "return stepSpell == kHearthstone && preparingSpell == kScourgestone;" "the Scourgestone stands in for the hearthstone only")
require_text("${policy}" "return castRestMs + reactDelayMs;" "the wait for a prepared teleport")
require_text("${policy}" "    if (preparedWaitMs)\n        return preparedWaitMs;\n    return innerWaitMs > reactDelayMs ? innerWaitMs : reactDelayMs;" "the hold: the spell's own wait first")
require_text("${wait_h}" "    Spell* const spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);\n    if (!spell || spell->getState() != SPELL_STATE_PREPARING || !SameTeleport(stepSpell, spell->m_spellInfo->Id))\n        return 0;\n    return WaitMs(spell->GetCastedTime(), reactDelayMs);" "the prepared wait from the spell")

# HandleSpecialMovement (the live travel path; MoveTo returns MoveTo2 at once, so its old teleport tail is not touched): the
# prepared wait first in the NODE_TELEPORT block, then a hold after both starts.
require_text("${move_cpp}" "#include \"playerbot/TeleportCastWait.h\"" "the include in MovementActions.cpp")
require_count("${move_cpp}" "teleport_cast::PreparedWaitMs(" 3 "the prepared wait (guard + two holds)")
require_count("${move_cpp}" "SetDuration(teleport_cast::HoldMs(teleport_cast::PreparedWaitMs(bot, nextPoint.entry, reactDelay), ai->GetAIInternalUpdateDelay(), reactDelay));" 2 "the hold after the hearthstone and the teleport cast start")
require_count("${move_cpp}" "return ai->DoSpecificAction(\"hearthstone\", Event(\"move action\"), true) ? true : false;" 0 "no hearthstone start in HandleSpecialMovement that returns before the hold")
string(FIND "${move_cpp}" "if (nextPoint.type == PathNodeType::NODE_TELEPORT && nextPoint.entry)" block_at)
string(FIND "${move_cpp}" "if (uint32 const waitMs = teleport_cast::PreparedWaitMs(bot, nextPoint.entry, reactDelay))" wait_at)
string(FIND "${move_cpp}" "if (nextPoint.entry == 8690) // Hearthstone" hearth_at)
if(block_at EQUAL -1 OR wait_at EQUAL -1 OR hearth_at EQUAL -1 OR NOT block_at LESS wait_at OR NOT wait_at LESS hearth_at)
  message(FATAL_ERROR "#333 finding 4 teleport cast wait: the prepared wait sits in the NODE_TELEPORT block before the hearthstone use")
endif()

# Unstuck: each run from the react delay; both long-stuck branches wait for a prepared hearthstone before "action useful"
# (no repop mid-cast) and hold after a start.
require_text("${unstuck_cpp}" "    uint32 const reactDelay = sPlayerbotAIConfig.reactDelay;\n    SetDuration(reactDelay);\n    auto const hearthPrepared = [&]()" "unstuck starts each run from the react delay")
require_count("${unstuck_cpp}" "        if (hearthPrepared())\n            return true;\n        if (AI_VALUE2(bool, \"action useful\", \"hearthstone\") && bot->IsAlive())\n        {\n            return hearth();\n        }" 2 "both long-stuck branches")
require_count("${unstuck_cpp}" "DoSpecificAction(\"hearthstone\"" 1 "the hearthstone start only in the helper")

# Zone escape: each run from the react delay; the hearthstone step holds for the cast.
require_text("${escape_cpp}" "bool ZoneEscapeAction::Execute(Event& event)\n{\n    // twow-repo#333 finding 4: each run starts from the react delay" "zone escape starts each run from the react delay")
require_text("${escape_cpp}" "            SetDuration(teleport_cast::HoldMs(teleport_cast::PreparedWaitMs(bot, teleport_cast::kHearthstone, reactDelay),\n                ai->GetAIInternalUpdateDelay(), reactDelay));" "zone escape holds for the hearthstone")
