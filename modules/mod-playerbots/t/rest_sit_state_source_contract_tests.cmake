cmake_policy(VERSION 3.16)
# twow-repo#610 BOT-REST-01 item 1 (bug fix, no switch; OB-10 / OB-00 18.10.2026: before the X1 RestGate measurement).
# EatAction / DrinkAction called bot->addUnitState(UNIT_STAND_STATE_SIT): a stand state (1) passed to the unit-state setter, where 1 is
# UNIT_STAT_MELEE_ATTACKING. Every meal left a melee state with no victim behind; Unit::AttackStop returns early without a victim, so it
# stayed until the next real attack and its stop. Pins: no stand state through the unit-state setter, the food / drink casts unchanged.
# Reads only the named file below modules/mod-playerbots/src/playerbot.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

file(READ "${PB_SOURCE_DIR}/strategy/actions/UseItemAction.h" use_item)
string(REPLACE "\r" "" use_item "${use_item}")

function(require_count text needle expected description)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${at} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#610 BOT-REST-01: ${description}: found ${count} of '${needle}', expected ${expected}")
  endif()
endfunction()

require_count("${use_item}" "addUnitState(UNIT_STAND_STATE_" 0 "no stand state through the unit-state setter")
require_count("${use_item}" "twow-repo#610 BOT-REST-01" 2 "both cheat paths (drink, eat) carry the fix note")
require_count("${use_item}" "ai->CastSpell(24355, bot);" 1 "drink cast unchanged")
require_count("${use_item}" "ai->CastSpell(24005, bot);" 1 "food cast unchanged")
require_count("${use_item}" "SetDuration(drinkDuration);" 1 "drink duration unchanged")
require_count("${use_item}" "SetDuration(eatDuration);" 1 "eat duration unchanged")
message(STATUS "REST_SIT_STATE_SOURCE_CONTRACT=PASS")
