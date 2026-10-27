cmake_policy(VERSION 3.16)
# twow-repo#333 Berufe P1-x (OB-00 27.10.2026, pattern hunt finding 6). RpgTaxiAction lent the bot the fare (10 g) before
# four later failure returns (no taxi path entry, no taxi node, no flight master in reach, ActivateTaxiPathTo refused)
# and set the money back only after a flight started: every failure kept the 10 g. Player::ActivateTaxiPathTo also refuses
# a shapeshifted player, and the action only dismounted, so a druid in travel form or a shaman as ghost wolf failed every
# time - and kept 10 g every time. The fare is now lent around ActivateTaxiPathTo only and set back either way; the form
# is dropped with the mount. This contract pins the wiring.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#333 P1-x taxi: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-x taxi: order ${description}: '${first}' must come before '${second}'")
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

read_source("strategy/actions/RpgSubActions.cpp" rpg_cpp)

string(FIND "${rpg_cpp}" "bool RpgTaxiAction::Execute(Event& event)" taxi_at)
if(taxi_at EQUAL -1)
  message(FATAL_ERROR "#333 P1-x taxi: RpgTaxiAction::Execute not found")
endif()
string(SUBSTRING "${rpg_cpp}" ${taxi_at} -1 rest)
string(FIND "${rest}" "\n}\n" end_at)
if(end_at EQUAL -1)
  message(FATAL_ERROR "#333 P1-x taxi: end of RpgTaxiAction::Execute not found")
endif()
string(SUBSTRING "${rest}" 0 ${end_at} taxi)

# The form goes with the mount, before the flight.
require_text("${taxi}" "    ai->Unmount();\n" "the dismount")
require_text("${taxi}" "    if (bot->GetShapeshiftForm() != FORM_NONE)\n        ai->RemoveShapeshift();\n" "the form dropped")
require_order("${taxi}" "ai->RemoveShapeshift();" "bot->ActivateTaxiPathTo(" "the form dropped before the flight")

# The fare is lent around the activation only and set back either way.
require_text("${taxi}" "    uint32 const money = bot->GetMoney();\n    bot->SetMoney(money + 100000);\n    bool const flying = bot->ActivateTaxiPathTo({ entry->from, entry->to }, flightMaster, 0);\n    bot->SetMoney(money);\n    if (!flying)\n" "the fare lent around ActivateTaxiPathTo")
require_order("${taxi}" "Creature* flightMaster = bot->GetNPCIfCanInteractWith(guidP, UNIT_NPC_FLAG_FLIGHTMASTER);" "bot->SetMoney(money + 100000);" "every failure return before the loan")
count_text("${taxi}" "SetMoney(" set_money)
if(NOT set_money EQUAL 2)
  message(FATAL_ERROR "#333 P1-x taxi: exactly the loan and its return may set the money (found ${set_money} SetMoney)")
endif()
count_text("${taxi}" "GetMoney()" get_money)
if(NOT get_money EQUAL 1)
  message(FATAL_ERROR "#333 P1-x taxi: one money snapshot (found ${get_money} GetMoney)")
endif()

message(STATUS "#333 P1-x taxi free flight source contract passed")
