# twow-repo#602 ROT-01 hunter deaths (focus check 25.10.2026, OB-00): AiPlayerbot.Rotation.HunterMeleeStart (default 0, behaviour
# change) makes the hunter's "switch to melee" really start the swing (Player::MeleeAttackStart is a no-op stub in this core),
# "switch to ranged" stop it, and an attack in "close" mode start melee.
# Pins: switch default 0; off = the stub calls and AttackAction's old melee rule; on = Unit::Attack(target, true) / clearing the
# melee state; hunters only in AttackAction; the core stub untouched; report; pure policy.
# Every check runs once on the real sources and once per negative probe (a mutated copy that must fail).

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_file path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

read_file("${PB_SOURCE_DIR}/HunterMeleeStartPolicy.h" policy)
read_file("${PB_SOURCE_DIR}/strategy/actions/CombatActions.cpp" combat_cpp)
read_file("${PB_SOURCE_DIR}/strategy/actions/AttackAction.cpp" attack_cpp)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_file("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_file("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr_cpp)
read_file("${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)

# text from `start` up to (not including) `stop`, empty when either is missing
function(between text start stop out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${stop}" b)
  if(b EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${rest}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

# append to `errors` of the calling function instead of failing (functions, not macros: macro arguments would
# re-evaluate the backslashes of the source text)
function(need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(errors "${errors};missing ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(ban text needle what)
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(errors "${errors};forbidden ${what}: ${needle}" PARENT_SCOPE)
  endif()
endfunction()
function(before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(errors "${errors};order ${what}: ${first} before ${second}" PARENT_SCOPE)
  endif()
endfunction()

function(check policy combat_cpp attack_cpp config_h config_cpp dist mgr_cpp player_h out_var)
  set(errors "")

  # 1. Switch: default 0 in the dist, the member and the read; unknown modes are off; parsed mode in [ConfigEcho].
  need("${dist}" "\nAiPlayerbot.Rotation.HunterMeleeStart = 0\n" "dist key, default 0")
  need("${config_h}" "uint32 rotationHunterMeleeStart = 0;" "config member, default 0")
  need("${config_cpp}" "rotationHunterMeleeStart = config.GetIntDefault(\"AiPlayerbot.Rotation.HunterMeleeStart\", 0);   // twow-repo#602 hunter deaths\n    if (rotationHunterMeleeStart > 2)\n        rotationHunterMeleeStart = 0;" "config read, > 2 = off")
  need("${config_cpp}" "{\"Rotation.HunterMeleeStart\", Value(rotationHunterMeleeStart)}," "parsed mode in [ConfigEcho] (owner rule #615)")

  # 2. Pure policy (std + the cohort rule only); the old melee rule is part of it.
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
  foreach(inc ${incs})
    if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
      list(APPEND errors "HunterMeleeStartPolicy.h is not pure: ${inc}")
    endif()
  endforeach()
  need("${policy}" "return ai::cohort::Applies(mode, guidLow);" "cohort rule")
  need("${policy}" "return !ranged || distance2d < 5.0f || (applies && closeMode);" "old rule + close mode")

  # 3. Switch to melee / ranged: off = the stub calls as before, on = the real swing start / stop.
  between("${combat_cpp}" "bool SwitchToMeleeAction::Execute(Event &event)" "\n}\n" tomelee)
  need("${tomelee}" "if (ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow()))" "cohort mode in switch to melee")
  need("${tomelee}" "if (bot->Attack(target, true))" "Unit::Attack with melee")
  need("${tomelee}" "        else\n            bot->MeleeAttackStart(target);" "off path = the stub call")
  need("${tomelee}" "return ChangeCombatStrategyAction::Execute(event);" "strategy change kept")
  between("${combat_cpp}" "bool SwitchToRangedAction::Execute(Event &event)" "\n}\n" toranged)
  need("${toranged}" "bot->ClearUnitState(UNIT_STAT_MELEE_ATTACKING);\n                bot->SendMeleeAttackStop(target);" "melee state cleared + client told")
  need("${toranged}" "        else\n            bot->MeleeAttackStop(target);" "off path = the stub call")

  # 4. AttackAction: hunters only, close mode only with the switch, the result goes into bot->Attack.
  between("${attack_cpp}" "ai->PlayAttackEmote(1);" "SC_LOG(\"attack-cmd bot->Attack" att)
  need("${att}" "bool const hunterSwitch = bot->getClass() == CLASS_HUNTER &&\n                ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow());" "hunters only, cohort mode")
  need("${att}" "hunterSwitch && ai->HasStrategy(\"close\", BotState::BOT_STATE_COMBAT));" "close mode only with the switch")
  need("${att}" "result = bot->Attack(target, melee);" "the flag goes into Attack")
  ban("${att}" "result = bot->Attack(target, !ai->IsRanged(bot)" "the old inline rule left in place")

  # 5. The core stub stays untouched (other code paths keep their behaviour).
  need("${player_h}" "void MeleeAttackStart(Unit* /*pVictim*/) {}" "core stub untouched")
  need("${player_h}" "void MeleeAttackStop(Unit* /*pVictim*/ = nullptr) {}" "core stub untouched")

  # 6. Report once per minute after [CombatIdle], nothing when off.
  before("${mgr_cpp}" "    ReportCombatIdle();" "    ReportHunterMelee();" "report after ReportCombatIdle")
  need("${mgr_cpp}" "if (!sPlayerbotAIConfig.rotationHunterMeleeStart || now < lastReport + 60)" "report only when on")

  set(${out_var} "${errors}" PARENT_SCOPE)
endfunction()

check("${policy}" "${combat_cpp}" "${attack_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" "${player_h}" errs)
if(errs)
  string(REPLACE ";" "\n  " errs "${errs}")
  message(FATAL_ERROR "#602 hunter melee start contract failed:\n  ${errs}")
endif()

# Negative probes: each mutation must make the contract fail.
set(probes 0)
function(probe label var from to)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "#602 probe '${label}': mutation did not apply")
  endif()
  set(${var} "${mut}")
  check("${policy}" "${combat_cpp}" "${attack_cpp}" "${config_h}" "${config_cpp}" "${dist}" "${mgr_cpp}" "${player_h}" perrs)
  if(NOT perrs)
    message(FATAL_ERROR "#602 probe '${label}': the contract did not catch it")
  endif()
  math(EXPR probes "${probes} + 1")
  set(probes "${probes}" PARENT_SCOPE)
endfunction()
probe("default on" dist "\nAiPlayerbot.Rotation.HunterMeleeStart = 0\n" "\nAiPlayerbot.Rotation.HunterMeleeStart = 1\n")
probe("unknown mode kept" config_cpp "    if (rotationHunterMeleeStart > 2)\n        rotationHunterMeleeStart = 0;" "")
probe("melee start without melee" combat_cpp "if (bot->Attack(target, true))" "if (bot->Attack(target, false))")
probe("off path changed" combat_cpp "        else\n            bot->MeleeAttackStart(target);" "")
probe("stop keeps the swing" combat_cpp "                bot->ClearUnitState(UNIT_STAT_MELEE_ATTACKING);\n" "")
probe("all classes" attack_cpp "bool const hunterSwitch = bot->getClass() == CLASS_HUNTER &&\n" "bool const hunterSwitch = \n")
probe("close mode ignored" policy "|| (applies && closeMode);" "|| applies;")
probe("old rule changed" policy "distance2d < 5.0f ||" "distance2d < 8.0f ||")
probe("stub patched in the core" player_h "void MeleeAttackStart(Unit* /*pVictim*/) {}" "void MeleeAttackStart(Unit* v) { Attack(v, true); }")
probe("impure policy" policy "#include <cstdint>" "#include <cstdint>\n#include \"Entities/Player.h\"")
probe("report always" mgr_cpp "if (!sPlayerbotAIConfig.rotationHunterMeleeStart || now < lastReport + 60)" "if (now < lastReport + 60)")

message(STATUS "hunter_melee_start source contract passed (${probes} negative probes)")
