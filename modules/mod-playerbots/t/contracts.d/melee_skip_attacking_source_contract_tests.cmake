# twow-repo#602 ROT-01 failure census (OB-00 27.10.2026): AiPlayerbot.Rotation.MeleeSkipAttacking, code default 1 (0 = rollback), makes the
# default "melee" action USELESS while the bot already attacks its current target, faces it and its pet is on it - there Unit::Attack
# returned false (FAILED) after Attack() repeated its side effects (M1r run 4: druid "melee" 27,658 FAILED against 1 OK). This contract pins
# the dist key, the config member and read (unknown modes = the default 1), the parsed mode in [ConfigEcho] (owner rule #615), the pure
# policy, the wiring in MeleeAction::isUseful (the off path unchanged, the swing decision of AttackAction::Attack) and the [MeleeSkip]
# minute line. The rules are tested in t/melee_skip_policy_tests.cpp.

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
    message(FATAL_ERROR "#602 MeleeSkipAttacking: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("MeleeSkipPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/actions/GenericActions.cpp" actions_cpp)
read_source("strategy/actions/AttackAction.cpp" attack_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.MeleeSkipAttacking = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 rotationMeleeSkipAttacking = 1;" "config member, default 1")
require_text("${config_cpp}" "rotationMeleeSkipAttacking = config.GetIntDefault(\"AiPlayerbot.Rotation.MeleeSkipAttacking\", 1);   // twow-repo#602 failure census\n    if (rotationMeleeSkipAttacking > 2)\n        rotationMeleeSkipAttacking = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"Rotation.MeleeFleeBreak.Ranged\", Value(rotationMeleeFleeBreakRanged)},\n            {\"Rotation.MeleeSkipAttacking\", Value(rotationMeleeSkipAttacking)},\n            {\"Rotation.OomBackoff\"" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only) and its rules.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 MeleeSkipAttacking: MeleeSkipPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "return applies && targetIsVictim && inFront && petOnTarget && plainAttack && (swinging || !wantMelee);" "skip only where Unit::Attack fails and nothing else is lost")

# 3. MeleeAction::isUseful: the vehicle check first, then the switch block, then the unchanged "return true".
string(FIND "${actions_cpp}" "bool MeleeAction::isUseful()" at)
if(at EQUAL -1)
  message(FATAL_ERROR "#602 MeleeSkipAttacking: MeleeAction::isUseful not found")
endif()
string(SUBSTRING "${actions_cpp}" ${at} 5000 body)
string(FIND "${body}" "\n}\n" end)
string(SUBSTRING "${body}" 0 ${end} body)
require_text("${body}" "    if (ai->IsInVehicle() && !ai->IsInVehicle(false, false, true))\n        return false;\n" "the vehicle check stays first")
require_text("${body}" "    if (ai::melee_skip::Applies(sPlayerbotAIConfig.rotationMeleeSkipAttacking, bot->GetGUIDLow()))\n    {\n        Unit* const target = GetTarget();\n        if (target && bot->GetVictim() == target)\n" "the block needs the switch and the target as the victim")
# the swing decision is the one of AttackAction::Attack (hunter close mode with Rotation.HunterMeleeStart)
set(swing "bool const hunterSwitch = bot->getClass() == CLASS_HUNTER &&\n                ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow());")
require_text("${body}" "${swing}" "the hunter switch as in Attack()")
require_text("${attack_cpp}" "bool const hunterSwitch = bot->getClass() == CLASS_HUNTER &&\n                ai::hunter_melee_start::Applies(sPlayerbotAIConfig.rotationHunterMeleeStart, bot->GetGUIDLow());" "Attack() still decides the swing this way")
require_text("${body}" "bool const wantMelee = ai::hunter_melee_start::AttackWithMelee(ranged, distance2d, hunterSwitch,\n                hunterSwitch && ai->HasStrategy(\"close\", BotState::BOT_STATE_COMBAT));" "the swing decision")
require_text("${body}" "bool const inFront = !ai->CanMove() || sServerFacade.IsInFront(bot, target, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT);" "facing as Attack() checks it")
require_text("${body}" "bool const petOnTarget = !pet || !pet->IsAlive() || pet->GetVictim() == target;" "the pet on the target")
require_text("${body}" "bool const plainAttack = !bot->IsMounted() && !WaitForAttackStrategy::ShouldWait(ai) &&\n                !ai->HasStrategy(\"stealthed\", BotState::BOT_STATE_COMBAT);" "mounted / wait for attack / stealthed keep the old path")
require_text("${body}" "if (ai::melee_skip::Skip(true, true, bot->HasUnitState(UNIT_STAT_MELEE_ATTACKING), wantMelee, inFront, petOnTarget, plainAttack))\n            {\n" "the skip decision")
require_text("${body}" "                if (AI_VALUE(Unit*, \"old target\") != target)\n                    SET_AI_VALUE(Unit*, \"old target\", target);\n                ai::melee_skip::skipped.fetch_add(1, std::memory_order_relaxed);\n                return false;\n            }" "the skip keeps old target, counts and returns not useful")
# Attack() itself still turns, unmounts, waits and decides the swing as mirrored above (drift guard)
require_text("${attack_cpp}" "if (ai->CanMove() && !sServerFacade.IsInFront(bot, target, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))" "Attack() turns under the same condition")
require_text("${attack_cpp}" "bool const melee = ai::hunter_melee_start::AttackWithMelee(ranged, distance2d, hunterSwitch,\n                hunterSwitch && ai->HasStrategy(\"close\", BotState::BOT_STATE_COMBAT));" "Attack() decides the swing the same way")
require_text("${attack_cpp}" "if (bot->IsMounted() && (sServerFacade.GetDistance2d(bot, target) < 40.0f || bot->IsFlying()))" "Attack() unmounts")
require_text("${attack_cpp}" "if (!ai->HasStrategy(\"stealthed\", BotState::BOT_STATE_COMBAT) && !isWaitingForAttack)" "Attack() skips the attack when stealthed or waiting")
string(REGEX MATCH "\n    return true;$" tail "${body}")
if(NOT tail)
  message(FATAL_ERROR "#602 MeleeSkipAttacking: MeleeAction::isUseful must still end with return true")
endif()

# 4. The [MeleeSkip] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/MeleeSkipPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationMeleeSkipAttacking || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[MeleeSkip] mode=%u skipped=%llu\", sPlayerbotAIConfig.rotationMeleeSkipAttacking,\n        (unsigned long long)ai::melee_skip::skipped.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportMeleeSkip();" "the minute report is called")

message(STATUS "#602 MeleeSkipAttacking source contract passed")
