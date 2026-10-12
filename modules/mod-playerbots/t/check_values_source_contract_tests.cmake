# twow-repo#541 (roster spikes, OB-00 go 10.10.2026): the idle action "check values" forced six value
# searches per call and threw the results away (17.8 % of the slowest roster bot updates, ~54 ms each).
# Behind AiPlayerbot.CheckValues.SkipSearches (default 0 = as before) it keeps only its debug parts.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 check values: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 check values: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("strategy/actions/CheckValuesAction.cpp" action)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

require_text("${config_h}" "bool checkValuesSkipSearches = false;" "member default off")
require_text("${config_cpp}" "checkValuesSkipSearches = config.GetBoolDefault(\"AiPlayerbot.CheckValues.SkipSearches\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.CheckValues.SkipSearches = 0" "documented key")

# Debug parts first, then the switch, then the six searches (unchanged when off).
require_order("${action}" "if (ai->HasStrategy(\"debug move\", BotState::BOT_STATE_NON_COMBAT))" "if (sPlayerbotAIConfig.checkValuesSkipSearches)\n        return true;" "debug parts stay")
require_order("${action}" "sTravelNodeMap.manageNodes(" "if (sPlayerbotAIConfig.checkValuesSkipSearches)" "map parts stay")
foreach(value "possible targets" "all targets" "nearest npcs" "nearest corpses" "nearest game objects no los" "nearest friendly players")
  require_order("${action}" "if (sPlayerbotAIConfig.checkValuesSkipSearches)" "AI_VALUE(std::list<ObjectGuid>, \"${value}\")" "search ${value} behind the switch")
endforeach()

message(STATUS "check_values source contract passed")
