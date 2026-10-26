cmake_policy(VERSION 3.16)
# twow-repo#601 (CLI-Deep 26.10.2026, OB-00 go): AiPlayerbot.ActivityTrace.StuckDetail (default 0)
# - In cand3-C the quest cohort was stuck 21 % of its time (a travel target, under 5 yd moved in a minute), and no log said
#   why. With the switch the second stuck sample in a row writes one [StuckDetail] line for the episode, at most 30 a minute
#   for all bots. Pins the key, the policy (pure, no lock), the hook in the sample, the line and that it changes nothing.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "stuck detail: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "stuck detail: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "stuck detail: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "stuck detail: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "stuck detail: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "stuck detail: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("BotActivityPolicy.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)

# 1. Key: neutral default, documented, echoed.
require_text("${config_h}" "bool activityTraceStuckDetail = false;" "member default off")
require_text("${config_cpp}" "activityTraceStuckDetail = config.GetBoolDefault(\"AiPlayerbot.ActivityTrace.StuckDetail\", false);" "default 0")
require_text("${config_cpp}" "{\"ActivityTrace.StuckDetail\", Value(activityTraceStuckDetail)}," "ConfigEcho entry")
require_text("${conf_dist}" "\nAiPlayerbot.ActivityTrace.StuckDetail = 0\n" "documented")

# 2. Policy: the agreed bounds, one line per episode, relaxed atomics only (the policy stays pure: no game include, no log).
require_text("${policy}" "constexpr std::uint8_t StuckDetailSamples = 2;" "two stuck samples in a row")
require_text("${policy}" "constexpr std::uint32_t StuckDetailPerMinute = 30;" "30 lines a minute")
require_text("${policy}" "        return samples == StuckDetailSamples;" "once per episode")
require_text("${policy}" "        if (used.fetch_add(1, std::memory_order_relaxed) < StuckDetailPerMinute)" "the minute limit")
require_text("${policy}" "inline TrailPick PickFromTrail(std::string const& trail)" "action and trigger from the engine trail")
forbid_text("${policy}" "mutex" "a lock in the policy")

# 3. Hook: in the sample, after the kind, switch first, the episode noted at every sample, the limit last; before the credit.
between("${ai_cpp}" "void PlayerbotAI::SampleActivity(uint32 now)" "\n}\n" sample)
require_text("${sample}" "    if (sPlayerbotAIConfig.activityTraceStuckDetail && stuckEpisode.Note(kind == ai::bot_activity::Kind::Stuck) &&\n        ai::bot_activity::StuckDetailGlobal().Take(now))\n        ReportStuckDetail(state);" "switch, episode, limit")
require_order("${sample}" "ai::bot_activity::Kind const kind = ai::bot_activity::Classify(state);" "ReportStuckDetail(state);" "after the kind")
require_order("${sample}" "ReportStuckDetail(state);" "activityClock.Add(kind, now)" "before the credit")
require_text("${ai_h}" "ai::bot_activity::StuckEpisode stuckEpisode;" "per-bot episode")

# 4. The line: greppable, the agreed fields; log only (no status, target or movement change in the report).
between("${ai_cpp}" "void PlayerbotAI::ReportStuckDetail(ai::bot_activity::State const& state)" "\n}\n" report)
require_text("${report}" "[StuckDetail] bot=%u level=%u map=%u zone=%u x=%.0f y=%.0f dest=%s quest=%u entry=%d status=%u status_s=%d left_s=%d" "line head")
require_text("${report}" " dist=%.0f moved=%.1f move_retries=%u motion=%u moving=%u state=%u action=\\\"%s\\\" trigger=\\\"%s\\\" dropped=%u" "line tail")
require_text("${report}" "ai::bot_activity::PickFromTrail(currentEngine ? currentEngine->GetLastAction() : \"\")" "trail pick")
require_text("${report}" "ai::bot_activity::StuckDetailGlobal().TakeDropped()" "refused lines counted")
foreach(change "SetStatus(" "SetExpireIn(" "SetRetry(" "MoveTo(" "->Clear(" "->Set(")
  forbid_text("${report}" "${change}" "a behaviour change in the report")
endforeach()
