# twow-repo#602 ROT-01 failure census (OB-00 28.10.2026): AiPlayerbot.Rotation.InvalidTargetQuiet, code default 1 (0 = rollback), keeps the
# combat trigger "invalid target" quiet for an EMPTY current target while nothing attacks the bot - there "select new target" had nothing to
# clear and nobody to pick and failed on every AI pass (hunt2: 90,889 FAILED, Zexnik 1,274 in a row in 6 min). An empty target with
# attackers (the retarget to the next add) and a non-empty invalid target fire as before. This contract pins the dist key, the config
# member and read (unknown modes = the default 1), the parsed mode in [ConfigEcho] (owner rule #615), the pure policy, the trigger wiring
# (the old check stays the result), the log-only trace in SelectNewTargetAction (before the stop, no behaviour) and the [InvalidTarget]
# minute line. The rules are tested in t/invalid_target_policy_tests.cpp.

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
    message(FATAL_ERROR "#602 InvalidTargetQuiet: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#602 InvalidTargetQuiet: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("InvalidTargetPolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" dist)
read_source("RandomPlayerbotMgr.cpp" mgr_cpp)
read_source("strategy/triggers/GenericTriggers.cpp" triggers_cpp)
read_source("strategy/actions/ChooseTargetActions.cpp" choose_cpp)

# 1. The switch: default 1 in the dist, the member and the read; unknown modes are the default; parsed mode in [ConfigEcho], sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.InvalidTargetQuiet = 1\n" "dist key, default 1")
require_text("${config_h}" "uint32 rotationInvalidTargetQuiet = 1;" "config member, default 1")
require_text("${config_cpp}" "rotationInvalidTargetQuiet = config.GetIntDefault(\"AiPlayerbot.Rotation.InvalidTargetQuiet\", 1);   // twow-repo#602 failure census\n    if (rotationInvalidTargetQuiet > 2)\n        rotationInvalidTargetQuiet = 1;" "config read, > 2 = the default 1")
require_text("${config_cpp}" "            {\"Rotation.HunterMeleeStart\", Value(rotationHunterMeleeStart)},\n            {\"Rotation.InvalidTargetQuiet\", Value(rotationInvalidTargetQuiet)},\n" "parsed mode in [ConfigEcho], sorted (owner rule #615, #710)")

# 2. Pure policy (std + the cohort rule only) and its rule.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach(inc ${incs})
  if(NOT inc STREQUAL "#include <atomic>" AND NOT inc STREQUAL "#include <cstdint>" AND NOT inc STREQUAL "#include \"CohortPolicy.h\"")
    message(FATAL_ERROR "#602 InvalidTargetQuiet: InvalidTargetPolicy.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "return ai::cohort::Applies(mode, guidLow);" "the cohort rule")
require_text("${policy}" "return applies && targetEmpty && !hasAttackers;" "quiet only for an empty target without attackers")

# 3. The trigger: the switch block first, then the unchanged old check as the result.
string(FIND "${triggers_cpp}" "bool InvalidTargetTrigger::IsActive()" at)
if(at EQUAL -1)
  message(FATAL_ERROR "#602 InvalidTargetQuiet: InvalidTargetTrigger::IsActive not found")
endif()
string(SUBSTRING "${triggers_cpp}" ${at} 1500 body)
string(FIND "${body}" "\n}\n" end)
string(SUBSTRING "${body}" 0 ${end} body)
require_text("${triggers_cpp}" "#include \"playerbot/InvalidTargetPolicy.h\"" "the policy include in GenericTriggers.cpp")
require_text("${body}" "    if (ai::invalid_target::Applies(sPlayerbotAIConfig.rotationInvalidTargetQuiet, bot->GetGUIDLow()) &&\n        ai::invalid_target::Quiet(true, !AI_VALUE(Unit*, \"current target\"), AI_VALUE(bool, \"has attackers\")))\n    {\n        ai::invalid_target::quiet.fetch_add(1, std::memory_order_relaxed);\n        return false;\n    }\n" "the quiet block needs the switch, an empty target and no attackers")
require_text("${body}" "\n    return AI_VALUE2(bool, \"invalid target\", \"current target\");" "the old check stays the result")
require_order("${body}" "ai::invalid_target::Quiet(" "return AI_VALUE2(bool, \"invalid target\", \"current target\");" "the quiet block before the old check")

# 4. The trace in SelectNewTargetAction::Execute: log only, before the unchanged stop.
string(FIND "${choose_cpp}" "bool SelectNewTargetAction::Execute(Event& event)" sat)
if(sat EQUAL -1)
  message(FATAL_ERROR "#602 InvalidTargetQuiet: SelectNewTargetAction::Execute not found")
endif()
string(SUBSTRING "${choose_cpp}" ${sat} 3000 sel)
require_text("${choose_cpp}" "#include \"playerbot/InvalidTargetPolicy.h\"" "the policy include in ChooseTargetActions.cpp")
require_text("${sel}" "    if (target && !sServerFacade.UnitIsDead(target))\n    {\n        ai::invalid_target::droppedLive.fetch_add(1, std::memory_order_relaxed);\n        if (bot->IsNonMeleeSpellCasted(true))\n            ai::invalid_target::interruptedLive.fetch_add(1, std::memory_order_relaxed);\n    }\n\n    // Stop attacking\n    bot->SetSelectionGuid(ObjectGuid());\n    ai->InterruptSpell();\n    bot->AttackStop();\n" "the trace counts before the unchanged stop")

# 5. The [InvalidTarget] minute line: nothing when off, increments (exchange), called from the minute reports.
require_text("${mgr_cpp}" "#include \"playerbot/InvalidTargetPolicy.h\"" "the policy include in RandomPlayerbotMgr.cpp")
require_text("${mgr_cpp}" "    if (!sPlayerbotAIConfig.rotationInvalidTargetQuiet || now < lastReport + 60)\n        return;" "no line when off, once a minute")
require_text("${mgr_cpp}" "sLog.outBasic(\"[InvalidTarget] mode=%u quiet=%llu dropped_live=%llu interrupted_live=%llu\", sPlayerbotAIConfig.rotationInvalidTargetQuiet,\n        (unsigned long long)ai::invalid_target::quiet.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::invalid_target::droppedLive.exchange(0, std::memory_order_relaxed),\n        (unsigned long long)ai::invalid_target::interruptedLive.exchange(0, std::memory_order_relaxed));" "the minute line as increments")
require_text("${mgr_cpp}" "    ReportInvalidTarget();" "the minute report is called")

message(STATUS "#602 InvalidTargetQuiet source contract passed")
