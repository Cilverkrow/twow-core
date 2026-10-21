cmake_policy(VERSION 3.16)
# twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder, cohort mode 0/1/2, default 0; OB-00 go 18.10.2026 with the v1 defaults F1-F4,
# F2 threshold and F3 spell list configurable). Pins:
#  - the pure policy EmergencyLadderPolicy.h and that neither it nor the runtime enters the PlayerbotAI.h / PlayerbotAIConfig.h chain,
#  - the neutral defaults (switch 0, FightTargetHp 20, SoloCcSpells polymorph,banish,fear; plain load lines),
#  - switch off = only the gate test (OB-10 18.10.: not even the episode check): PanicTrigger keeps its condition, OutNumbered its body,
#  - healers outside the ladder (F1), the solo CC branch of CcTargetValue ending before the group members are read,
#  - FleeStrategy's flee priorities unchanged, the hourly report after ReportRotCast, budgeted lines, no shared_mutex.
# Reads only named files below modules/mod-playerbots/src/playerbot (never a glob, never src/modules/Eluna).

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
    message(FATAL_ERROR "#602 X3 EmergencyLadder: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#602 X3 EmergencyLadder: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if(first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#602 X3 EmergencyLadder: order (${description}): '${first}' must come before '${second}'")
  endif()
endfunction()

# The part of `text` from `start` up to the first `end` after it (both required).
function(region text start end out)
  string(FIND "${text}" "${start}" start_at)
  if(start_at EQUAL -1)
    message(FATAL_ERROR "#602 X3 EmergencyLadder: missing region start: ${start}")
  endif()
  string(SUBSTRING "${text}" ${start_at} -1 rest)
  string(LENGTH "${start}" start_length)
  string(SUBSTRING "${rest}" ${start_length} -1 tail)
  string(FIND "${tail}" "${end}" end_at)
  if(end_at EQUAL -1)
    message(FATAL_ERROR "#602 X3 EmergencyLadder: missing region end: ${end}")
  endif()
  math(EXPR length "${start_length} + ${end_at}")
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR next "${at} + ${len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#602 X3 EmergencyLadder: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("EmergencyLadderPolicy.h" policy)
read_source("EmergencyLadderTrace.cpp" rt_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" rpm_cpp)
read_source("strategy/triggers/GenericTriggers.h" triggers_h)
read_source("strategy/triggers/GenericTriggers.cpp" triggers_cpp)
read_source("strategy/values/CcTargetValue.cpp" cc_cpp)
read_source("strategy/generic/FleeStrategy.cpp" flee_cpp)

# 1. Pure policy, not in the botpch chain.
require_text("${policy}" "namespace ai::emergency_ladder\n" "policy namespace")
foreach(token "#include \"" "mutex" "atomic" "static " "sPlayerbotAIConfig" "sLog" "thread_local")
  forbid_text("${policy}" "${token}" "token in the pure policy")
endforeach()
foreach(header "EmergencyLadderPolicy.h" "EmergencyLadderTrace.h")
  forbid_text("${ai_h}" "${header}" "include in PlayerbotAI.h (botpch chain)")
  forbid_text("${config_h}" "${header}" "include in PlayerbotAIConfig.h (botpch chain)")
endforeach()
require_text("${policy}" "constexpr std::uint32_t EscapeWindowMs = 2000;" "2 s escape window")
# Review 18.10.: a ready escape comes first - its class node fires on "critical health" whether panic is quiet or not.
region("${policy}" "inline Choice Decide(Situation const& s)\n{\n" "\n}\n" decide)
require_order("${decide}" "if (s.escapeReady)" "if (FightFits(s))" "escape before fight")
require_text("${policy}" "constexpr std::uint32_t EpisodeGapMs = 10000;" "episode gap")

# 2. Neutral defaults.
require_text("${config_h}" "    uint32 rotationEmergencyLadder = 0;  // twow-repo#602 ROT-01 X3:" "member default 0")
require_text("${config_h}" "    uint32 rotationEmergencyLadderFightTargetHp = 20;" "F2 default 20")
require_text("${config_cpp}" "\n    rotationEmergencyLadder = config.GetIntDefault(\"AiPlayerbot.Rotation.EmergencyLadder\", 0);\n" "plain load line")
require_text("${config_cpp}" "\n    rotationEmergencyLadderFightTargetHp = config.GetIntDefault(\"AiPlayerbot.Rotation.EmergencyLadder.FightTargetHp\", 20);\n" "F2 load line")
require_text("${config_cpp}" "config.GetStringDefault(\"AiPlayerbot.Rotation.EmergencyLadder.SoloCcSpells\", \"polymorph,banish,fear\")" "F3 default list")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.EmergencyLadder = 0\n" "conf key = 0")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.EmergencyLadder.FightTargetHp = 20\n" "conf F2 = 20")
require_text("${conf_dist}" "\nAiPlayerbot.Rotation.EmergencyLadder.SoloCcSpells = polymorph,banish,fear\n" "conf F3 list")
foreach(value 1 2)
  forbid_text("${conf_dist}" "AiPlayerbot.Rotation.EmergencyLadder = ${value}" "conf key on")
endforeach()
require_order("${conf_dist}" "AiPlayerbot.Rotation.SpecFromPath = 0" "AiPlayerbot.Rotation.EmergencyLadder = 0" "key in the Rotation block")

# 3. Switch off = only the gate test.
region("${triggers_cpp}" "bool PanicTrigger::IsActive()\n{\n" "\n}\n" panic)
require_text("${panic}" "    bool const panic = !ai->IsInPvp() &&\n           AI_VALUE2(uint8, \"health\", \"self target\") < sPlayerbotAIConfig.criticalHealth &&" "panic condition unchanged")
require_order("${panic}" "    if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n        return panic;\n" "    return EmergencyLadder(panic);" "off path before the ladder")
# twow-repo#602 MeleeFleeBreak: the X3 part of "outnumbered" moved unchanged into OutnumberedActive(), which IsActive() calls
region("${triggers_cpp}" "bool OutNumberedTrigger::OutnumberedActive()\n{\n" "\n}\n" outnumbered)
require_text("${outnumbered}" "{\n    // twow-repo#602 ROT-01 X3 (AiPlayerbot.Rotation.EmergencyLadder): with the switch off only this test is added.\n    if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n        return Outnumbered();\n" "outnumbered off path first")
require_text("${triggers_cpp}" "\nbool OutNumberedTrigger::Outnumbered()\n{\n    // Below level 10, Hunter has no viable kiting toolkit" "old outnumbered body moved unchanged")
require_text("${triggers_h}" "        PanicTrigger(PlayerbotAI* ai) : Trigger(ai) {}\n" "panic trigger keeps the macro's constructor")
require_text("${triggers_h}" "        virtual std::string getName() override { return \"panic\"; }\n" "trigger name panic")
require_text("${triggers_h}" "        virtual std::string getName() override { return \"outnumbered\"; }\n" "trigger name outnumbered")

# 4. Population and escapes.
require_text("${triggers_cpp}" "    return !bot->GetGroup() && !ai->IsRealPlayer() && sRandomPlayerbotMgr.IsFreeBot(bot) && !ai->ContainsStrategy(STRATEGY_TYPE_HEAL);" "solo free bots, healers outside (F1)")
require_order("${triggers_cpp}" "    if (!LadderBot(ai, bot))\n        return true;" "ai::cohort::Applies(sPlayerbotAIConfig.rotationEmergencyLadder, guid);" "population before the cohort")
require_text("${triggers_cpp}" "    if (!applied)\n        return true;\n" "the other cohort flees as before")
require_text("${triggers_cpp}" "ai::emergency_ladder_rt::RecordEscapeFallback(bot->getClass(), cohortB);" "escape fallback per class (OB-10 18.10.)")
require_text("${triggers_cpp}" "    if (ladderOpen && (el::EpisodeStale(nowMs, ladderLastSeenMs) || !bot->IsInCombat()))\n        ladderOpen = false;" "episode ends after a gap or outside combat (review 18.10.)")
require_text("${triggers_cpp}" "    if (escapeOpen && ai::emergency_ladder::EpisodeStale(nowMs, escapeLastSeenMs))\n        escapeOpen = false;" "outnumbered window ends after a gap")
require_order("${triggers_cpp}" "        if (el::FightWon(s))\n        {\n            if (!ladderWon)\n                ai::emergency_ladder_rt::RecordFightWon(bot->getClass(), cohortB);\n            ladderWon = true;\n            return false;\n        }" "        if (s.hasTarget && !ladderWon)\n            ai::emergency_ladder_rt::RecordFightAbort(" "a won fight opens no new episode (OB-10 review 18.10.)")
require_order("${triggers_cpp}" "        if (s.hasTarget && !ladderWon)\n            ai::emergency_ladder_rt::RecordFightAbort(" "        return EmergencyLadder(panic);" "a won fight is no abort; the ladder decides again")
require_text("${triggers_cpp}" "ai::emergency_ladder_rt::RecordEpisode(bot->getClass(), cohortB, choice, el::EscapeOverFight(s));" "escape_over_fight counted per episode (OB-10 review 18.10.)")
require_text("${config_cpp}" "    if (rotationEmergencyLadder > 2)\n        rotationEmergencyLadder = 0;" "unknown mode is off")

# 5. Solo CC: switch first, own branch ending before the group members.
require_text("${cc_cpp}" "    if (sPlayerbotAIConfig.rotationEmergencyLadder && !bot->GetGroup() && !ai->IsRealPlayer() && sRandomPlayerbotMgr.IsFreeBot(bot) &&" "solo cc gate")
require_text("${cc_cpp}" "ai::emergency_ladder::SpellAllowed(qualifier, sPlayerbotAIConfig.rotationEmergencyLadderSoloCcSpells)" "F3 spell list")
region("${cc_cpp}" "        Group* group = bot->GetGroup();\n        if (!group)\n        {\n" "\n        }\n" solo_branch)
require_text("${solo_branch}" "            if (!soloCc)\n                return;\n" "group-less bots without the switch return as before")
require_text("${solo_branch}" "                result = creature;\n            return;" "solo branch ends with return")
require_order("${cc_cpp}" "            if (!soloCc)\n                return;" "group->GetMemberSlots()" "solo branch before the group members")
require_text("${cc_cpp}" "(qualifier == \"fear\" && AI_VALUE(bool, \"possible adds\"))" "no fear with other mobs near")
require_text("${cc_cpp}" "ai::emergency_ladder_rt::RecordCcBroken(" "cc_broken counter (OB-10 18.10.)")

# 6. Flee itself unchanged; report; bounded runtime.
count_text("${flee_cpp}" "NextAction(\"flee\", ACTION_EMERGENCY + 9)" 2 "panic/outnumbered flee priorities unchanged")
require_order("${rpm_cpp}" "    ReportRotCast();" "    ReportEmergencyLadder();" "report after ReportRotCast")
region("${rpm_cpp}" "void ReportEmergencyLadder()\n{\n" "\n}\n" report)
require_text("${report}" "    if (!sPlayerbotAIConfig.rotationEmergencyLadder)\n    {\n        tracing = false;\n        return;" "report off = nothing")
require_text("${rt_cpp}" "constexpr std::uint32_t LinesPerMinute = 300;" "line budget")
forbid_text("${rt_cpp}" "shared_mutex" "shared_mutex (twow-core#171)")
forbid_text("${rt_cpp}" "sPlayerbotAIConfig" "config reads in the runtime")

message(STATUS "EMERGENCY_LADDER_SOURCE_CONTRACT=PASS")
