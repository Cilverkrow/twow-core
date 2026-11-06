# twow-repo#602 ROT-01 W24 pattern B (OB-00 06.11.2026, decision (A): trace first, the rule after the data): AiPlayerbot.Rotation.RetargetEmptyTrace,
# default 0, LOG ONLY. After a kill a tank spun ~20 empty AI passes a second for up to 29 s ("select new target" FAILED while "has attackers"
# stayed true; the dps/tank assist picks only from "possible attack targets"). With the mode on, SelectNewTargetAction writes one
# [RetargetEmpty] line per bot at most every 10 s: the empty picks since the last line, the first attacker that is not a possible attack
# target and the first filter it fails (PossibleAttackTargetsValue order). [StuckDetail] gets cur_target_entry / cur_target_visible from the
# stored selection. This contract pins the dist key, the config member and read (unknown = 0), the echo, the pure policy, the unchanged
# value filters, the wrapped assist returns (the pick result passes through unchanged), the line and the [StuckDetail] fields.
# The rules are tested in t/retarget_empty_policy_tests.cpp.

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
    message(FATAL_ERROR "#602 RetargetEmptyTrace: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#602 RetargetEmptyTrace: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#602 RetargetEmptyTrace: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#602 RetargetEmptyTrace: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#602 RetargetEmptyTrace: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" n)
  set(count 0)
  string(FIND "${text}" "${needle}" at)
  while(NOT at EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR from "${at} + ${n}")
    string(SUBSTRING "${text}" ${from} -1 text)
    string(FIND "${text}" "${needle}" at)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("RetargetEmptyPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/values/CurrentTargetValue.h" current_h)
read_source("strategy/values/PossibleAttackTargetsValue.h" pat_h)
read_source("strategy/values/PossibleAttackTargetsValue.cpp" pat_cpp)
read_source("strategy/actions/ChooseTargetActions.h" choose_h)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_cpp)

# 1. The switch: default 0 in the dist, the member and the read; unknown modes are off; the parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.RetargetEmptyTrace = 0\n" "dist key, default 0")
require_text("${config_h}" "uint32 rotationRetargetEmptyTrace = 0;" "config member, default 0")
require_text("${config_cpp}" "rotationRetargetEmptyTrace = config.GetIntDefault(\"AiPlayerbot.Rotation.RetargetEmptyTrace\", 0);   // twow-repo#602 W24 pattern B trace\n    if (rotationRetargetEmptyTrace > 2)\n        rotationRetargetEmptyTrace = 0;" "config read, > 2 = off")
require_text("${config_cpp}" "            {\"Rotation.RestoShamanOffdps\", Value(rotationRestoShamanOffdps)},\n            {\"Rotation.RetargetEmptyTrace\", Value(rotationRetargetEmptyTrace)},\n            {\"Rotation.RogueTankStance\", Value(rotationRogueTankStance)},\n" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only), the filter order, 10 s per bot.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 RetargetEmptyTrace: RetargetEmptyPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "    if (!exists)\n        return Reason::Gone;\n    if (!alive)\n        return Reason::Dead;\n    if (!validAttack)\n        return Reason::NotValidAttack;\n    if (!inRange)\n        return Reason::OutOfRange;\n    if (immune)\n        return Reason::Immune;\n    if (isNpc && !tapped)\n        return Reason::NotTapped;\n    if (!passesRest)\n        return Reason::NoVictimNoTarget;\n    return Reason::Ok;" "the filter order of PossibleAttackTargetsValue")
require_text("${policy}" "return lastLine == 0 || now >= lastLine + intervalSeconds;" "one line per interval, the first at once")
require_text("${policy}" "constexpr std::uint32_t LineIntervalSeconds = 10;" "10 s per bot (OB-00)")

# 3. The value: the filters unchanged, the reader asks the same checks and is never called by the value itself.
require_text("${pat_h}" "static uint32 FirstFailingFilter(Unit* target, Player* player, float range);" "the reader, static")
require_text("${pat_cpp}" "        if (!IsValid(target, bot, sPlayerbotAIConfig.sightDistance, true, false))\n" "RemoveNonThreating unchanged")
between("${pat_cpp}" "uint32 PossibleAttackTargetsValue::FirstFailingFilter(Unit* target, Player* player, float range)" "\n}\n" reader)
require_text("${reader}" "bool const validAttack = alive && player->IsValidAttackTarget(target);" "the core attackability check")
require_text("${reader}" "bool const inRange = player->IsWithinDistInMap(target, range);" "the range check")
require_text("${reader}" "bool const immune = IsImmuneToDamage(target, player);" "the immune check")
require_text("${reader}" "bool const tapped = !isNpc || IsTapped(target, player);" "the tap check for NPCs")
require_text("${reader}" "bool const passesRest = IsValid(target, player, range, true, false);" "the rest as RemoveNonThreating asks it")
between("${pat_cpp}" "std::list<ObjectGuid> PossibleAttackTargetsValue::Calculate()" "\nbool PossibleAttackTargetsValue::HasIgnoreCCRti" value_body)
forbid_text("${value_body}" "FirstFailingFilter" "the reader inside the value (log only)")

# 4. The action: both assist returns pass their result through the trace; the trace returns it unchanged and writes nothing else.
require_text("${choose_cpp}" "#include \"playerbot/RetargetEmptyPolicy.h\"" "the policy include")
require_text("${choose_cpp}" "            return TraceRetargetEmpty(ai->DoSpecificAction(\"dps assist\", event, true));" "dps assist wrapped")
require_text("${choose_cpp}" "            return TraceRetargetEmpty(ai->DoSpecificAction(\"tank assist\", event, true));" "tank assist wrapped")
require_order("${choose_cpp}" "    if (AI_VALUE(bool, \"has attackers\"))" "TraceRetargetEmpty(ai->DoSpecificAction(\"dps assist\"" "only with attackers")
require_text("${choose_h}" "bool TraceRetargetEmpty(bool picked);" "the trace member")
require_text("${choose_h}" "uint64 retargetEmptyLastLine = 0;" "the per-bot line clock")
between("${choose_cpp}" "bool SelectNewTargetAction::TraceRetargetEmpty(bool picked)" "\n}\n" trace)
require_text("${trace}" "    if (picked || !ai::retarget_empty::Applies(sPlayerbotAIConfig.rotationRetargetEmptyTrace, bot->GetGUIDLow()))\n        return picked;" "nothing for a pick or with the switch off")
require_order("${trace}" "if (!ai::retarget_empty::Due(retargetEmptyLastLine, now, ai::retarget_empty::LineIntervalSeconds))" "AI_VALUE(std::list<ObjectGuid>, \"attackers\")" "the values are read only when a line is due")
require_text("${trace}" "PossibleAttackTargetsValue::FirstFailingFilter(unit, bot, sPlayerbotAIConfig.sightDistance)" "the same range as RemoveNonThreating")
require_text("${trace}" "sLog.outBasic(\"[RetargetEmpty] bot=%u level=%u map=%u x=%.0f y=%.0f fails=%u n_att=%u n_pat=%u entry=%u dist=%.1f reason=%s threat=%u\"\n        \" victim=%s in_combat=%u\"," "the line")
require_text("${trace}" "    retargetEmptyLastLine = now;\n    retargetEmptyFails = 0;\n    return picked;" "the clock and the count reset after the line")
count_text("${trace}" "return " returns)
count_text("${trace}" "return picked;" returns_picked)
if(NOT returns EQUAL returns_picked OR NOT returns EQUAL 3)
  message(FATAL_ERROR "#602 RetargetEmptyTrace: every return of the trace must be 'return picked;' (3), found ${returns_picked} of ${returns}")
endif()
foreach(write "->Set(" "SET_AI_VALUE" "DoSpecificAction" "AttackStop" "SetSelectionGuid" "Attack(" "->Reset(")
  forbid_text("${trace}" "${write}" "write in the log-only trace")
endforeach()

# 5. [StuckDetail]: the two fields last, from the stored selection (the value's Get hides an unseen target).
require_text("${current_h}" "ObjectGuid GetSelection() const { return selection; }" "the stored selection")
between("${ai_cpp}" "void PlayerbotAI::ReportStuckDetail(ai::bot_activity::State const& state)" "\n}\n" stuck_report)
require_text("${stuck_report}" "dynamic_cast<CurrentTargetValue*>(aiObjectContext->GetValue<Unit*>(\"current target\"))" "the current target value")
require_text("${stuck_report}" "ObjectGuid const curTargetGuid = curTargetValue ? curTargetValue->GetSelection() : ObjectGuid();" "the raw selection")
require_text("${stuck_report}" "int32 const curTargetVisible = !curTargetGuid ? -1 :" "-1 without a target")
require_text("${stuck_report}" "curTarget->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true)) ? 1 : 0;" "visible as CurrentTargetValue::Get")
require_text("${stuck_report}" "far_wait=%s\"\n        \" cur_target_entry=%u cur_target_visible=%d\"," "the fields last on the line")
require_text("${stuck_report}" "ai::far_move::WaitStateName(FarMoveWaitState()), curTargetGuid.GetEntry(), curTargetVisible);" "their values in order")

message(STATUS "#602 RetargetEmptyTrace source contract passed")
