# twow-repo#596 D (AiPlayerbot.BotUpdateTraceTriggers, default 0): with BotUpdateTrace a slow trigger pass names its three
# slowest trigger checks in the TIME entry of the trail; off, no clock read and the trail as before.

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
    message(FATAL_ERROR "#596 slow triggers: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#596 slow triggers: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#596 slow triggers: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#596 slow triggers: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/Engine.cpp" engine)
read_source("TriggerTimePolicy.h" policy)

require_text("${config_h}" "bool botUpdateTraceTriggers = false;" "switch default off")
require_text("${config_cpp}" "botUpdateTraceTriggers = config.GetBoolDefault(\"AiPlayerbot.BotUpdateTraceTriggers\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.BotUpdateTraceTriggers = 0" "documented")

# ProcessTriggers: the clock only with both switches, around Check() alone.
between("${engine}" "void Engine::ProcessTriggers(bool minimal)" "\nvoid Engine::PushDefaultActions()" triggers)
require_text("${triggers}" "bool const timeTriggers = sPlayerbotAIConfig.botUpdateTrace && sPlayerbotAIConfig.botUpdateTraceTriggers;" "both switches")
require_text("${triggers}" "std::uint64_t const checkStartUs = timeTriggers ? ai::bot_update::NowUs() : 0;" "no clock read when off")
require_order("${triggers}" "std::uint64_t const checkStartUs = timeTriggers" "Event event = trigger->Check();" "clock before the check")
require_order("${triggers}" "Event event = trigger->Check();" "slowTriggers.Add(trigger->getName(), ai::bot_update::SinceUs(checkStartUs));" "recorded right after the check")
require_order("${triggers}" "slowTriggers.Add(" "if (!event)" "recorded before an empty event is skipped")

# DoNextAction: the old TIME entry unchanged when off or fast; the slow form only with the switch and >= SlowPhaseUs.
between("${engine}" "bool Engine::DoNextAction(" "PushDefaultActions();" walk)
require_text("${walk}" "LogAction(\"TIME:values %lluus triggers %lluus\", (unsigned long long)valuesUs," "old TIME entry kept")
require_text("${walk}" "if (traceUs && sPlayerbotAIConfig.botUpdateTraceTriggers && triggersUs >= ai::trigger_time::SlowPhaseUs)" "slow form gated")
require_text("${walk}" "LogAction(\"TIME:values %lluus triggers %lluus slow=%s\"" "slow form")

require_text("${policy}" "constexpr std::uint64_t SlowPhaseUs = 20000;" "20 ms threshold")
require_text("${policy}" "constexpr std::size_t TopCount = 3;" "three slowest")

message(STATUS "slow_triggers source contract passed")
