# twow-repo#587 (soak 17.10.2026): AiPlayerbot.QuestRescue.KeepRequestWhenFull (default 0) - while the global rescue limit
# is used up a pending request stays with the bot instead of being taken and dropped.

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
    message(FATAL_ERROR "#587 keep rescue request: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#587 keep rescue request: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("QuestSearchPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("RandomPlayerbotMgr.cpp" mgr)

require_text("${config_h}" "bool questRescueKeepRequestWhenFull = false;" "default off")
require_text("${config_cpp}" "questRescueKeepRequestWhenFull = config.GetBoolDefault(\"AiPlayerbot.QuestRescue.KeepRequestWhenFull\", false);" "config")
require_text("${conf_dist}" "AiPlayerbot.QuestRescue.KeepRequestWhenFull = 0" "documented")
require_text("${policy}" "return recent.size() < RescueGlobalMax;" "capacity without taking a slot")
require_text("${ai_h}" "bool HasQuestRescueRequest() const { return questRescueRequested.load(); }" "peek, not take")

# The check comes before the request is taken; the old take and limiter lines stay as they are.
require_text("${mgr}" "if (sPlayerbotAIConfig.questRescueKeepRequestWhenFull && ai && ai->HasQuestRescueRequest() &&\n                !questRescueLimiter.HasCapacity(now))\n            break;" "keep the request while full")
require_order("${mgr}" "!questRescueLimiter.HasCapacity(now))" "!ai->TakeQuestRescueRequest() || IsParkedBot(guid)" "check before the take")
require_order("${mgr}" "!ai->TakeQuestRescueRequest() || IsParkedBot(guid)" "questRescueLimiter.TryAcquire(now)" "take before the limiter, as before")

message(STATUS "rescue_keep_request source contract passed")
