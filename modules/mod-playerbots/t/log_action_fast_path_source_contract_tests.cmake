# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A11, cards 33/34/35): Engine::LogAction fast path and last-action trail tail.
# AiPlayerbot.Perf.LogActionFastPath (default 0): the per-bot action-log lookup (global sFilesMutex) only
# when a file can be written - for the engine tee and every BotActionLog::Write/LogState caller - and the
# PerfMon action/trigger key strings only with PerfMon on. The engine's sLog.outDetail call stays as it is:
# sLog is BotLog there, which already drops a DETAIL line nobody keeps (deep dive L1); a core
# Log::HasLogLevelOrHigher guard would drop bots.log DETAIL lines and is forbidden.
# AiPlayerbot.BotUpdateTraceTailFix (default 0): the trail keeps the newest 512 characters.
# Off = the old code, verbatim. Pure trim logic: t/action_trail_tests.cpp.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A11: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A11: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A11: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  string(LENGTH "${needle}" needle_length)
  set(count 0)
  set(rest "${text}")
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A11: ${description}: '${needle}' found ${count} times, expected ${expected}")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A11: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A11: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/Engine.cpp" engine)
read_source("BotActionLog.cpp" bal)
read_source("BotActionLog.h" bal_h)
read_source("ActionTrail.h" trail_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("RandomPlayerbotMgr.cpp" rnd_mgr)

set(fast "sPlayerbotAIConfig.perfLogActionFastPath")
set(perf_guard "if (!${fast} || sPlayerbotAIConfig.perfMonEnabled)")

# 1. Switches: default off everywhere, documented with 0.
require_text("${config_h}" "bool perfLogActionFastPath = false;" "fast-path member default off")
require_text("${config_h}" "bool botUpdateTraceTailFix = false;" "tail-fix member default off")
require_text("${config_cpp}" "perfLogActionFastPath = config.GetBoolDefault(\"AiPlayerbot.Perf.LogActionFastPath\", false);" "fast-path config read, default false")
require_text("${config_cpp}" "botUpdateTraceTailFix = config.GetBoolDefault(\"AiPlayerbot.BotUpdateTraceTailFix\", false);" "tail-fix config read, default false")
require_text("${conf_dist}" "\nAiPlayerbot.Perf.LogActionFastPath = 0\n" "documented fast-path key with value 0")
require_text("${conf_dist}" "\nAiPlayerbot.BotUpdateTraceTailFix = 0\n" "documented tail-fix key with value 0")
forbid_text("${conf_dist}" "AiPlayerbot.Perf.LogActionFastPath = 1" "fast path shipped as on")
forbid_text("${conf_dist}" "AiPlayerbot.BotUpdateTraceTailFix = 1" "tail fix shipped as on")

# 2. LogAction trim: old trim verbatim on the off path, KeepNewest on the on path, both before any output.
require_text("${engine}" "#include \"playerbot/ActionTrail.h\"" "trail helper included")
region("${engine}" "void Engine::LogAction(const char* format, ...)" "void Engine::ChangeStrategy(" log_action)
require_order("${log_action}" "lastAction += buf;" "if (!sPlayerbotAIConfig.botUpdateTraceTailFix)" "trim after the append")
require_text("${log_action}" "if (!sPlayerbotAIConfig.botUpdateTraceTailFix)
        {
            lastAction = lastAction.substr(512);
            size_t pos = lastAction.find(\"|\");
            lastAction = (pos == std::string::npos ? \"\" : lastAction.substr(pos));
        }
        else" "old trim verbatim on the off path")
require_order("${log_action}" "lastAction = lastAction.substr(512);" "ai::action_trail::KeepNewest(lastAction, 512);" "new trim in the else branch")
require_order("${log_action}" "ai::action_trail::KeepNewest(lastAction, 512);" "if (testMode)" "trim before output")
count_text("${log_action}" "KeepNewest(" 1 "one KeepNewest call")

# 3. LogAction output: group gate first, the old outDetail unguarded and exactly once, then the tee gate.
require_order("${log_action}" "if (sPlayerbotAIConfig.logInGroupOnly && !bot->GetGroup())" "sLog.outDetail( \"%s %s\", bot->GetName(), buf);" "group gate unchanged and first")
require_text("${log_action}" "            return;\n\n        sLog.outDetail( \"%s %s\", bot->GetName(), buf);\n" "outDetail unguarded, as before")
count_text("${log_action}" "sLog.outDetail(" 1 "Engine::LogAction calls sLog.outDetail exactly once")
forbid_text("${log_action}" "HasLogLevelOrHigher" "level guard around the BotLog outDetail (would drop bots.log DETAIL lines)")
set(tee_gate "if (${fast} && !ai::botdiag::BotActionLog::MayWrite())\n            return;")
require_text("${log_action}" "${tee_gate}" "fast-path tee gate (switch && !MayWrite)")
require_order("${log_action}" "sLog.outDetail( \"%s %s\", bot->GetName(), buf);" "${tee_gate}" "detail log before the tee gate")
require_order("${log_action}" "${tee_gate}" "const char* tag = \"ENGINE\";" "tee gate before the tag")
require_order("${log_action}" "const char* tag = \"ENGINE\";" "ai::botdiag::BotActionLog::Write(ai, tag, \"%s\", buf);" "tee unchanged")
count_text("${log_action}" "BotActionLog::Write(" 1 "Engine::LogAction calls BotActionLog::Write exactly once")
count_text("${log_action}" "${fast}" 1 "one fast-path read in LogAction")

# 4. PerfMon keys only with PerfMon on (fast path); the old unguarded forms are gone, the calls are kept.
forbid_text("${engine}" "auto pmo = sPerformanceMonitor.start(PERF_MON_TRIGGER" "unguarded trigger PerfMon key")
forbid_text("${engine}" "auto pmo1 = sPerformanceMonitor.start(PERF_MON_ACTION, actionName, ai);" "unguarded action PerfMon key")
region("${engine}" "void Engine::ProcessTriggers(bool minimal)" "LogAction(\"T:%s\", trigger->getName().c_str());" triggers)
require_text("${triggers}" "std::unique_ptr<PerformanceMonitorOperation> pmo;\n            ${perf_guard}\n                pmo = sPerformanceMonitor.start(PERF_MON_TRIGGER, trigger->getName(), ai);\n            Event event = trigger->Check();" "trigger key guarded, probe still around Check")
region("${engine}" "Action* action = InitializeAction(actionNode);" "action->setRelevance(relevance);" popped)
require_order("${popped}" "std::unique_ptr<PerformanceMonitorOperation> pmo1;" "${perf_guard}" "pmo1 declared in the old scope")
require_text("${popped}" "${perf_guard}
            {
                std::string actionName = (action ? action->getName() : \"unknown\");
                if (!event.getSource().empty())
                    actionName += \" <\" + event.getSource() + \">\";

                pmo1 = sPerformanceMonitor.start(PERF_MON_ACTION, actionName, ai);
            }" "action key built only behind the guard, same key as before")
count_text("${engine}" "${fast}" 3 "fast-path reads in Engine.cpp (action key, trigger key, tee gate)")
count_text("${engine}" "sPlayerbotAIConfig.botUpdateTraceTailFix" 1 "tail-fix read in Engine.cpp")

# 5. BotActionLog: lock-free gate before sFilesMutex; open-file count kept under the lock.
require_text("${bal_h}" "static bool MayWrite();" "MayWrite declared")
require_text("${bal}" "#include <atomic>" "atomic include")
require_text("${bal}" "static std::atomic<std::size_t> sOpenFiles{0};" "open-file counter, constant-initialised")
region("${bal}" "bool BotActionLog::MayWrite()" "}" may_write)
require_text("${may_write}" "return ai::botdiag::IsActionLogEnabled() || sOpenFiles.load(std::memory_order_acquire) != 0;" "MayWrite: log on OR a file still open")
region("${bal}" "std::FILE* BotActionLog::GetHandle(PlayerbotAI* ai)" "static std::string TimestampWithMs()" get_handle)
require_text("${get_handle}" "if (${fast} && !MayWrite())\n        return nullptr;" "GetHandle fast-path gate")
require_order("${get_handle}" "if (${fast} && !MayWrite())" "std::lock_guard<std::mutex> g(sFilesMutex);" "gate before the global mutex")
require_order("${get_handle}" "std::lock_guard<std::mutex> g(sFilesMutex);" "return Open(ai);" "old lookup and lazy open kept")
region("${bal}" "void BotActionLog::Write(PlayerbotAI* ai, const char* tag, const char* fmt, ...)" "void BotActionLog::LogState(" write_fn)
require_order("${write_fn}" "FILE* f = GetHandle(ai);" "char line[2048];" "Write goes through GetHandle first")
forbid_text("${write_fn}" "sFilesMutex" "lock in Write ahead of the gate")
region("${bal}" "std::FILE* BotActionLog::Open(PlayerbotAI* ai)" "void BotActionLog::Close(PlayerbotAI* ai)" open_fn)
require_order("${open_fn}" "if (!ai::botdiag::IsActionLogEnabled()) return nullptr;" "std::lock_guard<std::mutex> g(sFilesMutex);" "Open refuses while the log is off (neutrality premise)")
require_order("${open_fn}" "sFiles[guid] = f;" "sOpenFiles.store(sFiles.size(), std::memory_order_release);" "count after insert")
region("${bal}" "void BotActionLog::Close(PlayerbotAI* ai)" "std::FILE* BotActionLog::GetHandle(" close_fn)
require_order("${close_fn}" "std::lock_guard<std::mutex> g(sFilesMutex);" "sFiles.erase(it);" "erase under the lock")
require_order("${close_fn}" "sFiles.erase(it);" "sOpenFiles.store(sFiles.size(), std::memory_order_release);" "count after erase")
require_order("${close_fn}" "sOpenFiles.store(sFiles.size(), std::memory_order_release);" "if (handle)" "count stored inside the lock block")
# Every sFiles writer updates the count: one insert, one erase, two stores.
count_text("${bal}" "sFiles[" 1 "sFiles insert sites")
count_text("${bal}" "sFiles.erase(" 1 "sFiles erase sites")
count_text("${bal}" "sOpenFiles.store(" 2 "open-file count stores")
forbid_text("${bal}" "shared_mutex" "std::shared_mutex")

# 6. Trim helper: newest window aligned to '|', in place, no allocation.
require_text("${trail_h}" "if (trail.size() <= limit)\n        return;" "short trail unchanged")
require_text("${trail_h}" "std::size_t const pos = trail.find('|', trail.size() - limit);" "newest window aligned to '|'")
require_text("${trail_h}" "trail.clear();" "no entry start in the window -> empty")
require_text("${trail_h}" "trail.erase(0, pos);" "in-place erase")
forbid_text("${trail_h}" "substr" "allocating substr in the trim")

# 7. Nothing of A11 before PerfMon Init in UpdateAIInternal (perfmon_init_reachable).
region("${rnd_mgr}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "sPerformanceMonitor.Init(0, 0);" pre_init)
forbid_text("${pre_init}" "perfLogActionFastPath" "A11 code before PerfMon Init")
forbid_text("${pre_init}" "botUpdateTraceTailFix" "A11 code before PerfMon Init")

message(STATUS "log_action_fast_path source contract passed")
