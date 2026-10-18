# twow-repo#596 / P3-b review (CLI-PATH 18.10.2026): AiPlayerbot.Gather.FishingSkillCheck (default 0) - the fishing branch of
# GatherTravelDestination::IsPossible hands the zone's fishing skill to the shared check instead of a shadowed local.

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
    message(FATAL_ERROR "fishing skill check: missing ${description}: ${needle}")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "fishing skill check: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "fishing skill check: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("TravelMgr.cpp" travel)

require_text("${config_h}" "bool gatherFishingSkillCheck = false;" "default off")
require_text("${config_cpp}" "gatherFishingSkillCheck = config.GetBoolDefault(\"AiPlayerbot.Gather.FishingSkillCheck\", false);" "config")
require_text("${conf_dist}" "AiPlayerbot.Gather.FishingSkillCheck = 0" "documented")

between("${travel}" "bool GatherTravelDestination::IsPossible(const PlayerTravelInfo& info) const" "else if (GetEntry() > 0)" fishing)
string(FIND "${fishing}" "        int32 reqSkillValue" shadow)   # the branch-level declaration (the outer one is uint32)
if(NOT shadow EQUAL -1)
  message(FATAL_ERROR "fishing skill check: the fishing branch must not declare its own reqSkillValue (shadowing)")
endif()
require_text("${fishing}" "if (sPlayerbotAIConfig.gatherFishingSkillCheck)\n            reqSkillValue = uint32(std::max<int32>(1, fishingSkillValue));" "the zone value reaches the shared check, behind the switch")

message(STATUS "fishing_skill_check source contract passed")
