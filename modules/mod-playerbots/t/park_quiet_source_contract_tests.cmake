# twow-repo#551 (owner 11.10.2026, "Schleifen-Fluten"): AiPlayerbot.Park.Quiet (default 0). A quiet parked bot drops its
# social strategies, answers whispers only and emotes at most once per QuietIdleEmoteSeconds. Off: parking as before.

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
    message(FATAL_ERROR "#551 park quiet: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#551 park quiet: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "#551 park quiet: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "#551 park quiet: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" mgr)
read_source("ParkPolicy.h" policy)

require_text("${config_h}" "bool parkQuiet = false;" "switch default off")
require_text("${config_cpp}" "parkQuiet = config.GetBoolDefault(\"AiPlayerbot.Park.Quiet\", false);" "config default 0")
require_text("${conf_dist}" "AiPlayerbot.Park.Quiet = 0" "documented")
require_text("${conf_dist}" "AiPlayerbot.Park.QuietIdleEmoteSeconds = 600" "idle emote interval documented")

# Strategies: the social ones only with the switch, at both park sites.
require_text("${policy}" "return \",-emote,-custom::say,-ai chat\";" "social strategies dropped")
require_text("${mgr}" "return std::string(ParkDroppedStrategies) + (sPlayerbotAIConfig.parkQuiet ? ai::park::QuietDroppedStrategies() : \"\");" "only with the switch")
set(site "ai->ChangeStrategy(ParkStrategyChange(), BotState::BOT_STATE_NON_COMBAT)")
string(REPLACE "${site}" "" without_sites "${mgr}")
string(LENGTH "${mgr}" len_all)
string(LENGTH "${without_sites}" len_without)
string(LENGTH "${site}" len_site)
math(EXPR site_count "(${len_all} - ${len_without}) / ${len_site}")
if(NOT site_count EQUAL 2)
  message(FATAL_ERROR "#551 park quiet: both park sites (here / inn) use ParkStrategyChange (found ${site_count})")
endif()
string(FIND "${mgr}" "ChangeStrategy(ParkDroppedStrategies" old_site)
if(NOT old_site EQUAL -1)
  message(FATAL_ERROR "#551 park quiet: a park site still bypasses ParkStrategyChange")
endif()

# Chat replies: dropped at the queue for non-whispers, before anything is queued.
between("${ai_cpp}" "void PlayerbotAI::QueueChatResponse(" "\n}\n" queue_fn)
require_order("${queue_fn}" "if (ai::park::QuietDropsChatReply(sPlayerbotAIConfig.parkQuiet, parked, msgType == CHAT_MSG_WHISPER))\n        return;" "chatReplies.push(" "filter before the queue")
require_text("${policy}" "return quiet && parked && !whisper;" "whispers still answered")

# Idle emote: inside the parked branch, after the park interval check, out of combat only.
between("${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)" "SlowUpdateProbe const slowUpdateProbe" parked_branch)
require_order("${parked_branch}" "lastParkedUpdateMs = nowMs;" "if (sPlayerbotAIConfig.parkQuiet && !bot->IsInCombat())" "emote after the park interval check")
require_order("${parked_branch}" "ai::park::IdleEmoteDue(now, nextParkIdleEmote, sPlayerbotAIConfig.parkQuietIdleEmoteSeconds)" "nextParkIdleEmote = now + sPlayerbotAIConfig.parkQuietIdleEmoteSeconds;" "next emote a full interval away")
require_text("${ai_cpp}" "nextParkIdleEmote = value && sPlayerbotAIConfig.parkQuiet" "scheduled at park time, cleared on unpark")
require_text("${policy}" "return intervalSeconds && nextSeconds && nowSeconds >= nextSeconds;" "0 = no idle emote")

message(STATUS "park_quiet source contract passed")
