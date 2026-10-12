# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A21): behind AiPlayerbot.Perf.LazyEngineInit (default 0 = off) PlayerbotAI::ChangeStrategy lets
# an engine that is neither running nor the reaction engine take the new strategy set without rebuilding its triggers;
# the rebuild comes on activation (ChangeEngine/Reset(true) call Init() anyway, Engine::DoNextAction catches
# Reset(false) and a mid-walk engine change). Off: every engine rebuilds at once, as before. The count of three
# `currentEngine = ` assignments is a deliberate tripwire (the DoNextAction catch covers every activation path): a new
# activation path must update this contract on purpose.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A21: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A21: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A21: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A21: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#541 A21: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL ${expected})
    message(FATAL_ERROR "#541 A21: ${description}: expected ${expected}, found ${count}: ${needle}")
  endif()
endfunction()

read_source("strategy/Engine.h" engine_h)
read_source("strategy/Engine.cpp" engine_cpp)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

# Switch: default off in code and documented with 0.
require_text("${config_h}" "bool perfLazyEngineInit = false;" "member default off")
require_text("${config_cpp}" "perfLazyEngineInit = config.GetBoolDefault(\"AiPlayerbot.Perf.LazyEngineInit\", false);" "config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.LazyEngineInit = 0\n" "documented key with value 0")
forbid_text("${conf_dist}" "\nAiPlayerbot.Perf.LazyEngineInit = 1" "shipped value 1")

# Engine API: defaulted parameter (every direct engine->ChangeStrategy caller stays eager), stale flag in the protected
# section next to reinitPending (compile-time guard against outside writes), relaxed counters, no locks.
require_text("${engine_h}" "void ChangeStrategy(const std::string& names, bool deferInitWhileInactive = false);" "defaulted parameter")
require_text("${engine_h}" "inline std::atomic<uint64> deferred{ 0 };" "deferred counter")
require_text("${engine_h}" "inline std::atomic<uint64> caught{ 0 };" "caught counter")
require_text("${engine_h}" "#include <atomic>" "atomic include")
require_order("${engine_h}" "    protected:\n\t    Queue queue;" "bool reinitPending = false;" "reinitPending in the protected member block")
require_order("${engine_h}" "bool reinitPending = false;" "bool initStale = false;" "stale flag next to reinitPending")
require_order("${engine_h}" "bool initStale = false;" "\n    public:\n\t\tbool testMode;" "stale flag before the public members")
require_count("${engine_h}" "bool initStale" 1 "stale flag declarations")
forbid_text("${engine_h}" "shared_mutex" "shared_mutex")
forbid_text("${engine_cpp}" "shared_mutex" "shared_mutex")
forbid_text("${engine_cpp}" "perfLazyEngineInit" "engine-side gating (PlayerbotAI is the single gate)")

# Init clears the flag only after a real reset, before the rebuild.
function_body("${engine_cpp}" "void Engine::Init()" init_body)
require_order("${init_body}" "if (!Reset())\n        return;" "initStale = false;" "flag cleared after a real reset")
require_order("${init_body}" "initStale = false;" "strategy->InitTriggers(triggers, state);" "flag cleared before the rebuild")

# DoNextAction: owed rebuild after the walk state is read, before the walk flag and the first ProcessTriggers,
# never inside a walk.
function_body("${engine_cpp}" "bool Engine::DoNextAction(Unit* unit, int depth, bool minimal, bool isStunned)" next_body)
require_text("${next_body}" "if (initStale && !wasInDoNextAction)\n    {\n        ai::lazy_engine_init::caught.fetch_add(1, std::memory_order_relaxed);\n        LogAction(\"S:stale init\");\n        Init();\n    }" "stale catch")
require_order("${next_body}" "bool const wasInDoNextAction = inDoNextAction;" "if (initStale && !wasInDoNextAction)" "catch after the walk state is read")
require_order("${next_body}" "if (initStale && !wasInDoNextAction)" "inDoNextAction = true;" "catch before the walk flag")
require_order("${next_body}" "inDoNextAction = true;" "ProcessTriggers(minimal);" "walk flag before triggers")
require_text("${next_body}" "if (!inDoNextAction && reinitPending)\n    {\n        reinitPending = false;\n        Init();\n    }" "walk-end reinit unchanged")

# ChangeStrategy: the only place that marks stale; eager branch kept.
function_body("${engine_cpp}" "void Engine::ChangeStrategy(const std::string& names, bool deferInitWhileInactive)" change_body)
require_text("${change_body}" "if (!initMode && StrategySetToken() != tokenBefore)\n    {\n        if (deferInitWhileInactive)\n        {\n            initStale = true;\n            ai::lazy_engine_init::deferred.fetch_add(1, std::memory_order_relaxed);" "deferral only for a real change")
require_text("${change_body}" "        else\n        {\n            Init();\n        }" "eager branch kept")
require_count("${engine_cpp}" "initStale = true;" 1 "places that mark an engine stale")

# Single-strategy mutators and Reset stay eager / untouched.
function_body("${engine_cpp}" "void Engine::addStrategy(const std::string& name)" add_body)
require_text("${add_body}" "if (!initMode && StrategySetToken() != tokenBefore)\n    {\n        Init();\n    }" "addStrategy eager")
forbid_text("${add_body}" "initStale" "stale flag in addStrategy")
function_body("${engine_cpp}" "bool Engine::removeStrategy(const std::string& name, bool init)" remove_body)
forbid_text("${remove_body}" "initStale" "stale flag in removeStrategy")
function_body("${engine_cpp}" "bool Engine::Reset()" reset_body)
require_text("${reset_body}" "reinitPending = true;" "walk deferral kept")
forbid_text("${reset_body}" "initStale" "stale flag in Reset")

# PlayerbotAI: one gate, both branches, current and reaction engine always eager, initMode first.
set(gated_call "engine->ChangeStrategy(names, sPlayerbotAIConfig.perfLazyEngineInit && !engine->initMode && engine != currentEngine && engine != reactionEngine);")
function_body("${ai_cpp}" "void PlayerbotAI::ChangeStrategy(const std::string& names, BotState type)" ai_change_body)
require_count("${ai_change_body}" "${gated_call}" 2 "gated calls in both branches")
require_count("${ai_change_body}" "engine->ChangeStrategy(" 2 "engine calls in PlayerbotAI::ChangeStrategy")
forbid_text("${ai_change_body}" "engine->ChangeStrategy(names);" "ungated call")
forbid_text("${ai_change_body}" "GetBotAI(" "cross-bot access")
forbid_text("${ai_cpp}" "initStale" "outside access to the stale flag")
forbid_text("${ai_h}" "initStale" "outside access to the stale flag")

# Every activation path still rebuilds.
function_body("${ai_cpp}" "void PlayerbotAI::ReInitCurrentEngine()" reinit_body)
require_text("${reinit_body}" "currentEngine->Init();" "ChangeEngine path rebuilds")
function_body("${ai_cpp}" "void PlayerbotAI::ChangeEngine(BotState type)" ce_body)
require_order("${ce_body}" "currentEngine = engine;" "ReInitCurrentEngine();" "ChangeEngine re-inits")
function_body("${ai_cpp}" "void PlayerbotAI::Reset(bool full)" pr_body)
require_order("${pr_body}" "if (full)" "engines[i]->Init();" "Reset(true) re-inits all")
require_count("${ai_cpp}" "currentEngine = " 3 "places that activate an engine (ctor, Reset, ChangeEngine) - tripwire")
forbid_text("${ai_h}" "currentEngine = " "inline engine activation in PlayerbotAI.h")

# [LazyEngineInit] minute line: gated, world thread, plain call after [BotUpdate], before the PerfMon Init.
function_body("${rnd_mgr}" "void ReportLazyEngineInit()" report_body)
require_text("${report_body}" "if (!sPlayerbotAIConfig.perfLazyEngineInit || now < lastReport + 60)\n        return;" "minute line gated")
require_text("${report_body}" "\"[LazyEngineInit] deferred=%llu caught=%llu saved=%llu\"" "minute line format")
require_text("${report_body}" "ai::lazy_engine_init::deferred.exchange(0, std::memory_order_relaxed)" "deferred drain")
require_text("${report_body}" "ai::lazy_engine_init::caught.exchange(0, std::memory_order_relaxed)" "caught drain")
function_body("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" upd_body)
require_order("${upd_body}" "\n    ReportBotUpdate();" "\n    ReportLazyEngineInit();" "report after [BotUpdate]")
require_order("${upd_body}" "\n    ReportLazyEngineInit();" "sPerformanceMonitor.Init(0, 0);" "report before the PerfMon Init")

message(STATUS "lazy_engine_init source contract passed")
