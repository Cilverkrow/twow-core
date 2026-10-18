# twow-repo#601 (soak 17.10.2026): AiPlayerbot.QuestFirstProgression.GrindFallbackMinutes (default 0) - the 8.32 grind
# fallback holds only N minutes after the last empty quest route search instead of for good.

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
    message(FATAL_ERROR "#601 grind minutes: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#601 grind minutes: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("QuestSearchPolicy.h" policy)
read_source("strategy/values/TravelValues.cpp" travel_values)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

require_text("${config_h}" "uint32 questFirstProgressionGrindFallbackMinutes = 0;" "default 0")
require_text("${config_cpp}" "questFirstProgressionGrindFallbackMinutes = uint32(std::max<int32>(0, config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.GrindFallbackMinutes\", 0)));" "config")
require_text("${conf_dist}" "AiPlayerbot.QuestFirstProgression.GrindFallbackMinutes = 0" "documented")

# Policy: 0 or no stamp = as before.
require_text("${policy}" "return !minutes || !lastFailAt || now < lastFailAt || now - lastFailAt < minutes * 60;" "fresh rule")

# The stamp is written where the failure is counted.
require_order("${choose}" "SET_AI_VALUE2(int, \"manual int\", \"quest route failures\", int(failures));" "SET_AI_VALUE2(int, \"manual int\", \"quest route failed at\", int(uint32(time(nullptr))));" "stamp with the count")

# The grind purpose: the time limit before both fallback branches (fresh-bot and 8.32), which stay unchanged.
require_order("${travel_values}" "if (!ai::quest_search::GrindFallbackFresh(sPlayerbotAIConfig.questFirstProgressionGrindFallbackMinutes," "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.directTargetsFreshGrindFailures)" "limit before the fresh-bot branch")
require_order("${travel_values}" "if (!ai::quest_search::GrindFallbackFresh(" "return ai::quest_search::GrindFallback(true, failures, sPlayerbotAIConfig.questFirstProgressionGrindFallbackFailures)" "limit before the 8.32 branch")

message(STATUS "grind_fallback_minutes source contract passed")
