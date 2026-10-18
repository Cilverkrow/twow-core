# twow-repo#601 / #614 (owner 18.10.2026): AiPlayerbot.ActionLog.Guids - EnableActionLog for a fixed sample of bots; empty = every
# bot, as before. A bot outside the sample opens no file and never takes the action log mutex.

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
    message(FATAL_ERROR "#614 action log sample: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#614 action log sample: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" len)
  set(n 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  if(NOT n EQUAL ${expected})
    message(FATAL_ERROR "#614 action log sample: ${description}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

read_source("ActionLogSamplePolicy.h" policy)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("BotActionLog.cpp" log_cpp)

require_text("${policy}" "return sortedSample.empty() || std::binary_search(sortedSample.begin(), sortedSample.end(), guid);" "empty = every bot")
require_text("${config_h}" "std::vector<uint32> actionLogGuids;" "config member")
require_text("${config_cpp}" "actionLogGuids = ai::action_log::ParseSample(config.GetStringDefault(\"AiPlayerbot.ActionLog.Guids\", \"\"));" "config read, default empty")
require_text("${conf_dist}" "AiPlayerbot.ActionLog.Guids =\n" "documented, empty by default")

# Both entry points: Open (no file) and GetHandle (no mutex) ask the sample before the global mutex.
count_text("${log_cpp}" "if (!ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, " 2 "sample test in Open and GetHandle")
string(FIND "${log_cpp}" "std::FILE* BotActionLog::Open(PlayerbotAI* ai)" open_at)
string(SUBSTRING "${log_cpp}" ${open_at} -1 open_body)
require_order("${open_body}" "ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, guid)" "std::lock_guard<std::mutex> g(sFilesMutex);" "Open: sample before the mutex")
string(FIND "${log_cpp}" "std::FILE* BotActionLog::GetHandle(PlayerbotAI* ai)" handle_at)
string(SUBSTRING "${log_cpp}" ${handle_at} -1 handle_body)
require_order("${handle_body}" "ai::action_log::Sampled(sPlayerbotAIConfig.actionLogGuids, bot->GetGUIDLow())" "std::lock_guard<std::mutex> g(sFilesMutex);" "GetHandle: sample before the mutex")
# The master switch stays first in Open.
require_order("${open_body}" "if (!ai::botdiag::IsActionLogEnabled()) return nullptr;" "ai::action_log::Sampled(" "Open: EnableActionLog first")

message(STATUS "action_log_sample source contract passed")
