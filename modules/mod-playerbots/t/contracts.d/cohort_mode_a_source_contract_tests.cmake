# twow-repo#601 (OB-00 31.10.2026, crossover A/B designs): CohortPolicy mode 3 = only cohort A, so unit 2 of an A/B can swap the
# treated cohort and every bot is its own control (per-bot heterogeneity cancels; CLI-PATH / CLI-Deep power check, PREREG cand4G 4b).
# Mode 3 is opt-in per key; this contract pins that nothing else changes meaning:
# 1. the rule: 0 off, 1 all, 2 only B, 3 only A, any other value off.
# 2. Grind.NeighbourAware keeps its own 3 = shadow (computes for every bot, applies for none).
# 3. every config member passed straight into cohort::Applies is clamped in PlayerbotAIConfig.cpp (> 2 -> 0, or > 3 -> 0 for a
#    key that opts in to mode 3, or a Clamp helper) - an unclamped key would silently start meaning "only A" at 3. The list is
#    found by scanning the sources, so a new key without a clamp fails here (CLI-Deep audit 31.10.: Rotation.SpecFromPath was it).

cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#601 cohort mode A: missing ${description}: ${needle}")
  endif()
endfunction()

read_source("CohortPolicy.h" cohort)
read_source("GrindNeighbourPolicy.h" neighbour)
read_source("PlayerbotAIConfig.cpp" config_cpp)

# ---- 1. the rule ----------------------------------------------------------------------------------------------------
require_text("${cohort}" "constexpr std::uint32_t ModeOnlyB = 2;\nconstexpr std::uint32_t ModeOnlyA = 3;" "mode numbers")
require_text("${cohort}" "    return mode == ModeAll || (mode == ModeOnlyB && InB(guidLow)) || (mode == ModeOnlyA && !InB(guidLow));" "the rule")
require_text("${cohort}" "is opt-in per key: a key whose loader clamps > 2 to 0 never passes 3 here" "opt-in documented")

# ---- 2. Grind.NeighbourAware: 3 stays shadow ------------------------------------------------------------------------
require_text("${neighbour}" "constexpr std::uint32_t ModeShadow = 3;" "the shadow mode")
require_text("${neighbour}" "    return mode != ModeShadow && ai::cohort::Applies(mode, guidLow);" "shadow applies for no bot")

# ---- 3. every key passed straight into cohort::Applies is clamped ------------------------------------------------------
file(GLOB_RECURSE pb_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(members "")
foreach(src IN LISTS pb_sources)
  file(READ "${src}" text)
  string(REGEX MATCHALL "cohort::Applies\\(sPlayerbotAIConfig\\.[A-Za-z0-9_]+" hits "${text}")
  foreach(hit IN LISTS hits)
    string(REGEX REPLACE ".*\\." "" member "${hit}")
    list(APPEND members "${member}")
  endforeach()
endforeach()
list(REMOVE_DUPLICATES members)
list(LENGTH members n)
if(n LESS 20)
  message(FATAL_ERROR "#601 cohort mode A: the scan found only ${n} keys (expected ~30) - the scan pattern is broken")
endif()
set(unclamped "")
foreach(member IN LISTS members)
  string(FIND "${config_cpp}" "if (${member} > 2)" a)
  string(FIND "${config_cpp}" "if (${member} > 3)" b)
  string(REGEX MATCH "${member} = [A-Za-z_:]*[Cc]lamp" c "${config_cpp}")
  if(a EQUAL -1 AND b EQUAL -1 AND c STREQUAL "")
    list(APPEND unclamped "${member}")
  endif()
endforeach()
if(unclamped)
  message(FATAL_ERROR "#601 cohort mode A: config keys passed to cohort::Applies without a mode clamp (3 would now mean 'only A'): ${unclamped}")
endif()
require_text("${config_cpp}" "    if (rotationSpecFromPath > 2)\n        rotationSpecFromPath = 0;" "Rotation.SpecFromPath clamped (CLI-Deep audit)")
message(STATUS "cohort_mode_a_source_contract: ${n} keys, all clamped")
