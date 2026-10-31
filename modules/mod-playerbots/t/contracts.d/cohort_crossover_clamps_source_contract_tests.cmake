cmake_policy(VERSION 3.16)
# twow-repo#601 crossover units (OB-00 31.10.2026, after CLI-PATH's cand4G 4b: per-bot heterogeneity persists across units, so the
# in-realm A/B units swap the cohorts in their second unit). The switches tested in a crossover accept 3 = only cohort A
# (CohortPolicy ModeOnlyA, CLI-PATH's pair); until that lands, 3 passes the clamp and Applies stays false (= off). Pins the opt-in
# clamps and their documentation. Every other cohort switch keeps its own > 2 clamp (audit 31.10.: 3 never reaches Applies there).

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "cohort crossover clamps: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "cohort crossover clamps: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# QuestLoop.RelationWorkNoGrind (turn_in unit 2 crossover: u2-1 = 2, u2-2 = 3).
require_text("${config_cpp}" "if (questLoopRelationWorkNoGrind > 3)" "RelationWorkNoGrind accepts 3")
require_text("${config_cpp}" "config key=RelationWorkNoGrind value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)" "RelationWorkNoGrind error line")
require_text("${conf_dist}" "#   RelationWorkNoGrind also takes 3 = only cohort A (even low guid)" "RelationWorkNoGrind 3 documented")

# QuestFirstProgression.StaleObjectiveFilter (cand4 + guards crossover: unit 1 = 2, unit 2 = 3).
require_text("${config_cpp}" "if (questFirstProgressionStaleObjectiveFilter > 3)" "StaleObjectiveFilter accepts 3")
require_text("${config_cpp}" "config key=StaleObjectiveFilter value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)" "StaleObjectiveFilter error line")
require_text("${conf_dist}" "3 = only cohort A (even low guid) skips" "StaleObjectiveFilter 3 documented")

# The other three #778 switches stay at > 2 (not crossover-tested).
require_text("${config_cpp}" "if (questLoopRelationArriveRpgTarget > 2)" "RelationArriveRpgTarget unchanged")
