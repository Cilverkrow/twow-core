# twow-repo#541 (spikes per region update, OB-00 go 10.10.2026 for (a) jitter): behind
# AiPlayerbot.AiDelayJitterPct (default 0 = off, max 50) the AI wait times out of combat are spread by
# +-pct (mean unchanged), the first AI update after login gets a random offset (0..2x GCD), and bots
# parked by one command get their 10 s rhythm spread. In combat nothing changes (cast times, GCD).
# Pure arithmetic: t/ai_jitter_policy_tests.cpp.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 jitter: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 jitter: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 jitter: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIBase.cpp" base_cpp)
read_source("PlayerbotAIBase.h" base_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# Switch: default 0, capped at 50, documented.
require_text("${config_h}" "uint32 aiDelayJitterPct = 0;" "member default off")
require_text("${config_cpp}" "aiDelayJitterPct = std::min<uint32>(config.GetIntDefault(\"AiPlayerbot.AiDelayJitterPct\", 0), 50);" "config default 0, cap 50")
require_text("${conf_dist}" "AiPlayerbot.AiDelayJitterPct = 0" "documented key")

# Every delay goes through SetAIInternalUpdateDelay; the spread is gated by the switch and the bot.
function_body("${base_cpp}" "void PlayerbotAIBase::SetAIInternalUpdateDelay(uint32 delay)" set_body)
require_order("${set_body}" "if (sPlayerbotAIConfig.aiDelayJitterPct && AllowDelayJitter())" "aiInternalUpdateDelay = delay;" "jitter before the delay is set")
require_text("${set_body}" "ai::jitter::JitteredDelay(delay, sPlayerbotAIConfig.aiDelayJitterPct, urand(0, ai::jitter::RollRange))" "symmetric spread")
require_text("${base_h}" "virtual bool AllowDelayJitter() const { return false; }" "off for other AIs")

# Bots: out of combat only.
function_body("${ai_cpp}" "bool PlayerbotAI::AllowDelayJitter() const" allow_body)
require_text("${allow_body}" "return bot && !bot->IsInCombat();" "out of combat only")
require_text("${ai_h}" "bool AllowDelayJitter() const override;" "override")

# Start offset after login (constructor), gated.
require_order("${ai_cpp}" "PlayerbotAI::PlayerbotAI(Player* bot)" "aiInternalUpdateDelay += ai::jitter::StartOffset(sPlayerbotAIConfig.globalCoolDown * 2, urand(0, ai::jitter::RollRange));" "start offset in the constructor")

# Parked spread, gated; unpark keeps lastParkedUpdateMs = 0.
function_body("${ai_cpp}" "void PlayerbotAI::SetParked(bool value)" park_body)
require_order("${park_body}" "lastParkedUpdateMs = 0;" "if (value && sPlayerbotAIConfig.aiDelayJitterPct)" "reset, then spread when parking")
require_text("${park_body}" "ai::jitter::SpreadLastUpdate(WorldTimer::getMSTime(), ai::park::AiIntervalMs, urand(0, ai::jitter::RollRange))" "parked spread")

message(STATUS "ai_jitter source contract passed")
