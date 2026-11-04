cmake_policy(VERSION 3.16)
# twow-repo#575 / #601 (OB-00 04.11.2026; evidence ws-10/cli-pathfinding/movement-a3/ROOT-HEARTH-FARMOVE.md §4; CLI-PATH):
# AiPlayerbot.Unstuck.CombatFightClock (cohort modes, default 0). "combat stuck" / "combat long stuck" compared the
# "time since last change" of "combat::self target", a value that only notices a change when it is read - and both
# triggers read it only in combat, so a new fight began with a clock from an earlier one (cand4G u2: 2 of 390 "combat
# long stuck" hearthstones in a fight running > 60 s). With the key both triggers count the fight with the current
# target (CombatStuckClockPolicy.h; pure rules in t/combat_stuck_clock_policy_tests.cpp). Off: the old value, read as before.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "combat fight clock: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "combat fight clock: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "combat fight clock: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "combat fight clock: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

# The text from `first` to the next `second` after it.
function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "combat fight clock: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "combat fight clock: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

# Counts the plain-text occurrences of needle in text (no regex).
function(count_text text needle out_var)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_length)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${at} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("CombatStuckClockPolicy.h" policy_h)
read_source("strategy/triggers/StuckTriggers.h" triggers_h)
read_source("strategy/triggers/StuckTriggers.cpp" triggers_cpp)

# 1. The key: default 0, an unknown mode is off, documented, echoed in sorted order (owner rule #615).
require_text("${config_h}" "uint32 unstuckCombatFightClock = 0;" "member off")
require_text("${config_cpp}" "unstuckCombatFightClock = config.GetIntDefault(\"AiPlayerbot.Unstuck.CombatFightClock\", 0);" "default 0")
require_text("${config_cpp}" "if (unstuckCombatFightClock > 3)\n        unstuckCombatFightClock = 0;" "unknown mode off")
require_text("${config_cpp}" "            {\"Travel.RpgAreaLevelFromSpawn\", Value(travelRpgAreaLevelFromSpawn)},\n            {\"Unstuck.CombatFightClock\", Value(unstuckCombatFightClock)},\n            {\"ZoneExit.TurnInNearRadius\"" "[ConfigEcho] entry, sorted")
require_text("${conf_dist}" "\nAiPlayerbot.Unstuck.CombatFightClock = 0\n" "documented off")

# 2. The pure rules: no game header, the 30 s new-fight gap, the clock from the combat start / the target change.
require_text("${policy_h}" "constexpr std::int64_t NewFightGapSeconds = 30;" "new-fight gap")
require_text("${policy_h}" "inline std::uint32_t Seconds(Clock& c, std::int64_t now, std::int64_t combatStart, std::uint64_t target)" "clock signature")
require_text("${policy_h}" "bool const freshStart = combatStart > 0 && combatStart <= now && combatStart != c.startValue;" "a new, not future start value")
require_text("${policy_h}" "bool const newFight = c.anchor == 0 || now - c.lastSeen > NewFightGapSeconds || freshStart;" "new fight rule")
require_text("${policy_h}" "    else if (target != c.target)\n    {\n        c.target = target;\n        c.targetSince = now;\n    }" "restart on a target change")
forbid_text("${policy_h}" "#include \"" "the policy includes a project header (pure rules only)")

# 3. Both triggers: the shared clock, one own Clock each, checked after the existing gates (state, master, group, S7, activity).
foreach(trigger IN ITEMS "class CombatStuckTrigger : public Trigger" "class CombatLongStuckTrigger : public Trigger")
  between("${triggers_h}" "${trigger}" "\n    };\n" body)
  require_text("${body}" "uint32 timeSinceCombatChange = CombatStuckSeconds(ai, fightClock);" "${trigger}: the shared clock")
  require_text("${body}" "combat_stuck_clock::Clock fightClock;" "${trigger}: its own clock")
  require_order("${body}" "if (ai->GetState() != BotState::BOT_STATE_COMBAT)" "CombatStuckSeconds(ai, fightClock)" "${trigger}: only in combat")
  require_order("${body}" "if (!ai->AllowActivity(ALL_ACTIVITY))" "CombatStuckSeconds(ai, fightClock)" "${trigger}: after the activity gate")
  require_text("${body}" "if (bot->m_duel && bot->m_duel->startTime - time(0) > 15 * MINUTE)" "${trigger}: duel rule unchanged")
endforeach()
between("${triggers_h}" "class CombatStuckTrigger : public Trigger" "\n    };\n" short_body)
between("${triggers_h}" "class CombatLongStuckTrigger : public Trigger" "\n    };\n" long_body)
require_text("${short_body}" "if (timeSinceCombatChange > 5 * MINUTE)" "combat stuck limit 5 min unchanged")
require_text("${long_body}" "if (timeSinceCombatChange > 15 * MINUTE)" "combat long stuck limit 15 min unchanged")
count_text("${triggers_h}" "AI_VALUE2(uint32, \"time since last change\", \"combat::self target\")" old_reads)
if(NOT old_reads EQUAL 0)
  message(FATAL_ERROR "combat fight clock: StuckTriggers.h still reads the old clock directly ${old_reads} times (want 0; only the helper's off path)")
endif()
require_text("${triggers_h}" "uint32 CombatStuckSeconds(PlayerbotAI* ai, combat_stuck_clock::Clock& clock);" "helper declared")
require_text("${triggers_h}" "#include \"playerbot/CombatStuckClockPolicy.h\"" "policy included")

# 4. The helper: cohort gate on the key; off = exactly the old value; on = combat start time + current target.
between("${triggers_cpp}" "uint32 ai::CombatStuckSeconds(PlayerbotAI* ai, combat_stuck_clock::Clock& clock)" "\n}\n" helper)
require_text("${helper}" "if (!bot || !cohort::Applies(sPlayerbotAIConfig.unstuckCombatFightClock, bot->GetGUIDLow()))\n        return AI_VALUE2(uint32, \"time since last change\", \"combat::self target\");" "off path as before")
require_order("${helper}" "return AI_VALUE2(uint32, \"time since last change\", \"combat::self target\");" "Unit* target = AI_VALUE(Unit*, \"current target\");" "the new values read only with the key on")
require_text("${helper}" "std::int64_t(AI_VALUE(time_t, \"combat start time\"))" "the combat start")
require_text("${helper}" "target ? target->GetObjectGuid().GetRawValue() : 0" "the current target")
require_text("${triggers_cpp}" "#include \"playerbot/CohortPolicy.h\"" "cohort rules included")
count_text("${triggers_cpp}" "uint32 ai::CombatStuckSeconds(" helper_count)
if(NOT helper_count EQUAL 1)
  message(FATAL_ERROR "combat fight clock: CombatStuckSeconds defined ${helper_count} times (want 1)")
endif()

message(STATUS "combat fight clock: source contract ok")
