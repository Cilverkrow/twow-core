if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#597 (TICK-01, synthesis 3.H fix direction 2): asynchronous log sink, switch Log.AsyncSink
# (default 0 = every line written synchronously by the caller, as before) and Log.AsyncSinkMaxLines
# (default 0 = built-in bound). This contract pins the defaults and their documentation, the off path
# (one atomic load per writing call, the synchronous code after it), the one queue and its bounds, the
# writer that never calls back into sLog, the lock order, the [LogSink] lines, the shutdown drain, the
# crash flushes and the fail-closed start. Pure logic: t/log_sink_597_test.cpp.

function(read_source path out_var)
  if (NOT EXISTS "${TW_CORE_ROOT}/${path}")
    message(FATAL_ERROR "#597 log sink: missing source ${path}")
  endif()
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#597 log sink: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#597 log sink: forbidden ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#597 log sink: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  set(rest "${text}")
  while (TRUE)
    string(FIND "${rest}" "${needle}" at)
    if (at EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${at} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(require_count text needle expected label)
  count_text("${text}" "${needle}" n)
  if (NOT n EQUAL ${expected})
    message(FATAL_ERROR "#597 log sink: ${label}: expected ${expected} x '${needle}', found ${n}")
  endif()
endfunction()

# The text between first (inclusive) and the next second after it (exclusive).
function(slice text first second out_var)
  string(FIND "${text}" "${first}" a)
  if (a EQUAL -1)
    message(FATAL_ERROR "#597 log sink: slice start not found: ${first}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${second}" b)
  if (b EQUAL -1)
    message(FATAL_ERROR "#597 log sink: slice end not found: ${second}")
  endif()
  string(SUBSTRING "${tail}" 0 ${b} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("src/shared/LogSinkPolicy.h" policy_h)
read_source("src/shared/Log.h" log_h)
read_source("src/shared/Log.cpp" log_cpp)
read_source("src/game/World.cpp" world_cpp)
read_source("src/mangosd/Master.cpp" master)
read_source("src/mangosd/mangosd.conf.dist.in" dist)
read_source("src/game/DiscordBot/GMCommandHandler.cpp" gm_cpp)
read_source("t/log_sink_597_test.cpp" test_cpp)
read_source("CMakeLists.txt" root_cmake)

# ---- Switches: default 0, documented with that default, read where documented.
require_text("${dist}" "\nLog.AsyncSink = 0\n" "documented default of Log.AsyncSink")
require_text("${dist}" "\nLog.AsyncSinkMaxLines = 0\n" "documented default of Log.AsyncSinkMaxLines")
slice("${dist}" "# Log.AsyncSink\n" "\nLog.AsyncSink = 0\n" dist_doc)
require_text("${dist_doc}" "#   Default: 0 (off)" "Log.AsyncSink default documented")
require_text("${dist_doc}" "#   Default: 0 (built-in 50000)" "Log.AsyncSinkMaxLines default documented")
require_text("${dist_doc}" "\"[LogSink] lines=<n> dropped=<n> max_depth=<n> write_max_us=<x>\"" "[LogSink] line documented")
require_text("${dist_doc}" "\"[LogSink] stopped reason=<r> written=<n>\n#   dropped=<n> unwritten=<n> drained=<0|1> drain_ms=<n>\"" "stop line documented")
require_text("${dist_doc}" "Lines still queued when the process dies otherwise are lost" "crash loss documented")
require_text("${dist_doc}" "lines / 64 MiB (the bound covers queued lines and the lines the writer is still writing)" "crash loss maximum documented")
require_text("${dist_doc}" "a reload to 1 needs a restart" "reload documented")
slice("${dist}" "# PerformanceLog.LogWriteTrace\n" "\nPerformanceLog.LogWriteTrace = 0\n" logwait_doc)
require_text("${logwait_doc}" "With Log.AsyncSink = 1 a line is only formatted and queued by the caller" "[LogWait] meaning with the sink documented")

slice("${log_cpp}" "void Log::ApplyAsyncSinkConfig(bool reload)" "\n}\n" apply_fn)
require_text("${apply_fn}" "bool const on = sConfig.GetIntDefault(\"Log.AsyncSink\", 0) != 0;" "Log.AsyncSink default 0")
require_order("${apply_fn}" "if (on && !m_sink)" "sConfig.GetIntDefault(\"Log.AsyncSinkMaxLines\", 0)" "max lines only read with the sink on")
require_count("${log_cpp}" "\"Log.AsyncSinkMaxLines\"" 1 "max lines read in one place")
require_text("${apply_fn}" "StopAsyncSink(\"reload\");" "a reload to 0 drains and stops")
require_text("${apply_fn}" "[LogSink] Log.AsyncSink = 1 needs a restart, the log stays synchronous" "a reload to 1 says it needs a restart")
require_text("${world_cpp}" "    LoadConfigSettingsCommonPart(reload);\n\n    // twow-repo#597 (TICK-01): Log.AsyncSink" "applied in LoadConfigSettings")
require_text("${world_cpp}" "    sLog.ApplyAsyncSinkConfig(reload);\n" "World hands the reload flag over")

# ---- Off path: one atomic load, nothing else; the synchronous code after it unchanged.
require_text("${log_h}" "std::atomic<bool> m_asyncSinkOn{false};" "sink off until started")
require_text("${log_h}" "bool AsyncSinkOn() const { return m_asyncSinkOn.load(std::memory_order_acquire); }" "off = one atomic load")
require_count("${log_h}" "if (AsyncSinkOn())" 1 "the template out() has the sink branch")
require_count("${log_h}" "SinkSubmit(line, lwStartUs);" 1 "the template out() submits")
require_order("${log_h}" "uint64 const lwStartUs = LogWaitStartIf(logFiles[type] != nullptr);" "if (AsyncSinkOn())" "out(): sink branch after the [LogWait] start")
require_order("${log_h}" "SinkSubmit(line, lwStartUs);\n                return;" "fprintf(logFiles[type], \"%s\", log.c_str());" "out(): sink branch before the synchronous write")
# Every writing Log::out* path of Log.cpp: the branch after its [LogWait] start, submit + return before the
# first synchronous write (every synchronous path flushes).
set(paths
  "void Log::outString()\n"
  "void Log::outString( const char * str, ... )"
  "void Log::outInfo("
  "void Log::outHonor("
  "void Log::outRaid("
  "void Log::outError("
  "void Log::outErrorDb()\n"
  "void Log::outErrorDb( const char"
  "void Log::outBasic("
  "void Log::outDetail("
  "void Log::outDebug("
  "void Log::outWarden("
  "void Log::outWardenDebug("
  "void Log::outAnticheat("
  "void Log::outDiscord("
  "void Log::outDiscordCore("
  "void Log::outSpam("
  "void Log::outCommand("
  "void Log::outWorldPacketDump(")
list(LENGTH paths path_count)
foreach (fn IN LISTS paths)
  slice("${log_cpp}" "${fn}" "\n}\n" body)
  require_count("${body}" "if (AsyncSinkOn())" 1 "one sink branch in ${fn}")
  require_order("${body}" "lwStartUs = LogWaitStart" "if (AsyncSinkOn())" "${fn}: sink branch after the [LogWait] start")
  require_order("${body}" "if (AsyncSinkOn())" "SinkSubmit(line, lwStartUs);" "${fn}: sink branch submits")
  require_order("${body}" "SinkSubmit(line, lwStartUs);" "fflush(" "${fn}: submitted before the synchronous write")
  string(FIND "${body}" "SinkSubmit(line, lwStartUs);" at)
  string(SUBSTRING "${body}" ${at} 80 after_submit)
  string(REGEX MATCH "^SinkSubmit\\(line, lwStartUs\\);\n *return;" ret "${after_submit}")
  if (NOT ret)
    message(FATAL_ERROR "#597 log sink: ${fn}: the sink branch must return right after SinkSubmit")
  endif()
endforeach()
count_text("${log_cpp}" "if (AsyncSinkOn())" branches)
count_text("${log_cpp}" "SinkSubmit(line, lwStartUs);" submits)
if (NOT branches EQUAL path_count OR NOT submits EQUAL path_count)
  message(FATAL_ERROR "#597 log sink: ${path_count} writing paths, found ${branches} sink branches and ${submits} submits")
endif()
# The caller's side reads no clock of its own: only [LogWait] (its own switch) records the caller's cost.
slice("${log_cpp}" "void Log::SinkConsole(" "void Log::SinkSubmit(" builders)
forbid_text("${builders}" "NowUs" "clock read while a line is built")
slice("${log_cpp}" "void Log::SinkSubmit(" "\n}\n" submit_fn)
require_count("${submit_fn}" "NowUs" 1 "one clock read in SinkSubmit, for [LogWait] only")
require_text("${submit_fn}" "    if (lwStartUs)\n        m_logWait.Record(tick_diag::NowUs() - lwStartUs, 0);" "[LogWait] = caller's cost (format + enqueue), file 0")
require_text("${submit_fn}" "log_sink::WriteLine(line);    // the sink is stopping" "closed sink: written by the caller")
require_text("${submit_fn}" "log_sink::CloseOwned(line);   // queue full" "dropped line closes its per-line files")
forbid_text("${submit_fn}" "wait" "the caller never waits")

# ---- One queue, one writer, bounded; the writer never calls back into sLog.
require_count("${log_cpp}" "log_sink::BoundedFifo<log_sink::Line> fifo;" 1 "one queue for all targets")
require_count("${log_cpp}" "std::thread thread;" 1 "one writer thread")
require_text("${policy_h}" "static constexpr std::size_t DefaultMaxLines = 50000;" "built-in bound")
require_text("${policy_h}" "static constexpr std::size_t MaxMaxLines = 1000000;" "upper clamp")
require_text("${policy_h}" "static constexpr std::size_t MaxQueuedBytes = 64u * 1024u * 1024u;" "byte bound")
require_text("${policy_h}" "static constexpr std::uint32_t DrainWaitMs = 5000;" "bounded drain")
require_text("${policy_h}" "static constexpr std::uint32_t CrashFlushWaitMs = 2000;" "bounded crash flush")
# The bound covers every line not written yet (queued + the chunk the writer is writing), in lines and
# bytes: a stalled writer holding a popped chunk does not let a second full queue in (review follow-up).
require_text("${policy_h}" "if (Pending() >= m_maxLines || bytes > m_maxBytes - m_pendingBytes)\n        {\n            ++m_window.dropped;\n            ++m_droppedTotal;\n            return PushResult::Dropped;" "full (queued + in flight) = drop and count")
require_text("${policy_h}" "std::uint64_t Pending() const { return m_accepted - m_written; }" "pending = accepted - written")
slice("${policy_h}" "    void MarkWritten(" "\n    }\n" mark_fn)
require_text("${mark_fn}" "m_pendingBytes -= std::min(bytes, m_pendingBytes);" "bytes leave the bound only when written")
slice("${policy_h}" "    std::size_t PopUpTo(" "\n    }\n" pop_fn)
forbid_text("${pop_fn}" "m_pendingBytes" "a pop must not free the byte bound")
require_text("${pop_fn}" "std::size_t const n = std::min(m_count, maxItems);" "bounded pop")
forbid_text("${policy_h}" "PopAll" "an unbounded pop under the sink lock")
require_text("${policy_h}" "m_window.maxDepth = std::max<std::uint64_t>(m_window.maxDepth, Pending());" "max_depth = queued + in flight")
require_text("${policy_h}" "m_window.maxDepth = Pending();" "window starts at the backlog")
require_text("${policy_h}" "static constexpr std::size_t PopChunk = 256;" "writer chunk")
require_text("${policy_h}" "std::size_t const size = std::min(std::max<std::size_t>(m_ring.size() * 2, 16), m_maxLines);" "ring never above the bound")
require_text("${policy_h}" "static constexpr std::size_t MaxSegments = 3;" "bounded targets per line")
slice("${log_cpp}" "    void Run()\n" "    bool Drain(uint32 waitMs)" run_fn)
forbid_text("${run_fn}" "sLog" "sLog in the writer")
forbid_text("${run_fn}" "Log::" "Log members in the writer")
forbid_text("${run_fn}" "logLock" "logLock in the writer (lock order)")
require_order("${run_fn}" "fifo.PopUpTo(batch, log_sink::PopChunk, bytes);\n            lock.unlock();" "log_sink::WriteLine(line);" "a bounded chunk, written outside the sink mutex")
require_order("${run_fn}" "log_sink::WriteLine(line);" "fifo.MarkWritten(written, bytes, maxUs);" "marked written after the write")
require_text("${log_cpp}" "work.notify_one();" "the writer is woken per queued line")

# ---- Locks: short std::mutex + condition_variable; logLock (existing) only taken, no new shared_mutex.
forbid_text("${policy_h}" "mutex" "locks in the pure policy")
require_count("${log_h}" "std::shared_mutex " 1 "only the existing shared_mutex member")
forbid_text("${log_cpp}" "std::shared_mutex " "new shared_mutex in Log.cpp")
require_text("${log_cpp}" "    std::mutex mutex;\n    std::condition_variable work;" "short std::mutex + condition_variable")

# ---- Start: fails closed, starts before the switch goes on.
slice("${log_cpp}" "void Log::StartAsyncSink(std::size_t maxLines)" "\n}\n" start_fn)
require_order("${start_fn}" "#if PLATFORM == PLATFORM_WINDOWS" "[LogSink] Log.AsyncSink = 1 ignored: not supported on Windows" "Windows fails closed")
require_text("${start_fn}" "[LogSink] Log.AsyncSink = 1 ignored: cannot start the writer thread (%s), the log stays synchronous" "no thread fails closed")
require_order("${start_fn}" "state->thread = std::thread(" "m_sink = state;" "writer runs before the switch")
require_order("${start_fn}" "std::unique_lock<std::shared_mutex> l{ logLock };" "m_asyncSinkOn.store(true, std::memory_order_release);" "switch on under logLock")
require_order("${start_fn}" "m_sink = state;" "m_asyncSinkOn.store(true, std::memory_order_release);" "m_sink set before the switch")
require_text("${start_fn}" "s_prevTerminate = std::set_terminate(LogSinkTerminate);" "terminate handler flushes first")

# ---- Stop: drain under logLock, bounded, join or leave, final line written synchronously afterwards.
slice("${log_cpp}" "void Log::StopAsyncSink(char const* reason)" "\n}\n" stop_fn)
require_order("${stop_fn}" "std::unique_lock<std::shared_mutex> l{ logLock };" "m_asyncSinkOn.store(false, std::memory_order_release);" "switch off under logLock")
require_order("${stop_fn}" "m_asyncSinkOn.store(false, std::memory_order_release);" "m_sink->Drain(log_sink::DrainWaitMs);" "drained after the switch-off")
require_order("${stop_fn}" "m_sink->Drain(log_sink::DrainWaitMs);" "outString(\"%s\", log_sink::FormatStopLine(report).c_str());" "final line after the drain")
require_text("${stop_fn}" "m_sink->thread.detach();\n            m_sinkAbandoned = true;" "a blocked writer is left behind, files stay open")
require_text("${policy_h}" "\"[LogSink] stopped reason=%s written=%llu dropped=%llu unwritten=%llu drained=%u drain_ms=%llu\"" "stop line format")
require_order("${master}" "sWorld.InternalShutdown();" "sLog.StopAsyncSink(\"shutdown\");" "drained after the world shut down")
require_order("${master}" "sLog.StopAsyncSink(\"shutdown\");" "std::quick_exit(exitCode);" "drained before quick_exit")
require_order("${log_h}" "StopAsyncSink(\"exit\");" "fclose(logfile);" "~Log drains before closing files")
require_order("${log_h}" "if (m_sinkAbandoned)\n            return;" "fclose(logfile);" "files left open for a blocked writer")

# ---- Crash: the backtrace and the last lines are flushed (bounded) before the process dies.
slice("${master}" "sLog.outInfo(\"%s\", st.c_str());" "createdump();" sigsegv_part)
require_text("${sigsegv_part}" "sLog.FlushAsyncSink(log_sink::CrashFlushWaitMs);" "SIGSEGV backtrace flushed before the core dump")
require_order("${master}" "sLog.outError(\"World Thread hangs, kicking out server!\");" "sLog.FlushAsyncSink(log_sink::CrashFlushWaitMs);  // twow-repo#597" "freeze detector flushes")
require_order("${master}" "sLog.FlushAsyncSink(log_sink::CrashFlushWaitMs);  // twow-repo#597" "std::terminate(); // bang crash" "flush before terminate")
slice("${log_cpp}" "static void LogSinkTerminate()" "\n}\n" term_fn)
require_order("${term_fn}" "sink->Flush(log_sink::CrashFlushWaitMs);" "s_prevTerminate();" "terminate: flush, then the previous handler")
require_text("${log_cpp}" "std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);" "a crash flush never blocks on the sink mutex")
require_text("${gm_cpp}" "std::unique_lock<std::shared_mutex> l{ sLog.logLock };\n        // twow-repo#597" "Discord log read flushes under its exclusive lock")
# Short wait (every logging thread waits on logLock meanwhile), and a writer that is behind keeps its
# FILE position: no ftell/fseek after a failed flush (review follow-up).
require_text("${policy_h}" "static constexpr std::uint32_t DiscordReadFlushWaitMs = 100;" "Discord log read wait short")
forbid_text("${gm_cpp}" "FlushAsyncSink(log_sink::DrainWaitMs)" "a 5 s flush under the exclusive logLock")
slice("${gm_cpp}" "std::unique_lock<std::shared_mutex> l{ sLog.logLock };" "int streamPos = ftell(logfile);" gm_flush)
require_text("${gm_flush}" "if (!sLog.FlushAsyncSink(log_sink::DiscordReadFlushWaitMs))\n        {\n            l.unlock();" "failed flush: lock released")
require_order("${gm_flush}" "l.unlock();" "            return;\n        }" "failed flush: no file position moved")

# ---- [LogSink] minute line: world thread, off = one atomic load, counters taken before the line.
slice("${log_cpp}" "void Log::AsyncSinkMinute()" "\n}\n" minute_fn)
require_order("${minute_fn}" "if (!AsyncSinkOn())\n        return;" "tick_diag::NowUs()" "no clock read with the sink off")
require_text("${minute_fn}" "nowUs - m_sinkMinuteStartUs < 60000000" "once a minute")
require_order("${minute_fn}" "log_sink::Window const w = m_sink->TakeWindow();" "out(LOG_PERFORMANCE, \"%s\", log_sink::FormatMinuteLine(w).c_str());" "counters taken before the line")
require_text("${policy_h}" "\"[LogSink] lines=%llu dropped=%llu max_depth=%llu write_max_us=%llu\"" "[LogSink] line format")
slice("${world_cpp}" "void World::WorldTickEnd(uint64 updateUs, uint64 sleepUs)" "void World::LogWorldTickWindow(uint32 windowMs)" tick_end)
require_order("${tick_end}" "TickDiagMinute();" "sLog.AsyncSinkMinute();" "minute hook next to the T1 minute lines")

# ---- Formatting helpers: the same bytes as the synchronous path.
require_text("${policy_h}" "AppendF(out, \"%02d:%02d:%02d \", t.tm_hour, t.tm_min, t.tm_sec);" "outTime format")
require_text("${policy_h}" "AppendF(out, \"%-4d-%02d-%02d %02d:%02d:%02d \", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);" "outTimestamp format")
require_text("${log_cpp}" "fprintf(file,\"%-4d-%02d-%02d %02d:%02d:%02d \",aTm->tm_year+1900,aTm->tm_mon+1,aTm->tm_mday,aTm->tm_hour,aTm->tm_min,aTm->tm_sec);" "outTimestamp unchanged")
require_text("${log_cpp}" "fprintf(where, \"%02d:%02d:%02d \",aTm->tm_hour,aTm->tm_min,aTm->tm_sec);" "outTime unchanged")
require_text("${policy_h}" "static int const fg[15] = { 30, 31, 32, 33, 34, 35, 36, 37, 38, 31, 32, 34, 35, 36, 37 };" "ANSI table")
require_text("${log_cpp}" "fprintf((stdout_stream? stdout : stderr), \"\\x1b[%d%sm\",UnixColorFG[color],(color>=YELLOW&&color<Color_count ?\";1\":\"\"));" "SetColor bytes unchanged")
require_text("${log_h}" "    BLACK,\n    RED,\n    GREEN,\n    BROWN,\n    BLUE,\n    MAGENTA,\n    CYAN,\n    GREY,\n    YELLOW,\n    LRED,\n    LGREEN,\n    LBLUE,\n    LMAGENTA,\n    LCYAN,\n    WHITE\n" "Color order of the ANSI table")
# Console streams stay as before: errors on stderr, honor text on stderr with its time on stdout.
slice("${log_cpp}" "void Log::outError(" "\n}\n" err_fn)
require_text("${err_fn}" "SinkConsole(line, stderr, LogError, true, \"\", text);" "outError console on stderr")
slice("${log_cpp}" "void Log::outHonor(" "\n}\n" honor_fn)
require_text("${honor_fn}" "if (std::string* clock = line.Add(stdout))" "outHonor time on stdout")
require_text("${honor_fn}" "SinkConsole(line, stderr, LogNormal, false, \"\", text);" "outHonor text on stderr")
slice("${log_cpp}" "void Log::outString( const char * str, ... )" "\n}\n" str_fn)
require_text("${str_fn}" "SinkConsole(line, stdout, LogNormal, true, \"\", text);" "outString console on stdout")

# ---- Tests registered; tags.
require_text("${root_cmake}" "add_executable(log_sink_597_test\n    \"\${TW_CORE_ROOT}/t/log_sink_597_test.cpp\")" "unit test target")
require_text("${root_cmake}" "add_test(NAME log_sink_597_test COMMAND log_sink_597_test)" "unit test registered")
require_text("${root_cmake}" "-P \"\${TW_CORE_ROOT}/t/check_log_sink_597_contract.cmake\")" "contract registered")
require_text("${test_cpp}" "FifoOrderAndBoundTest();" "FIFO order and bound tested")
require_text("${test_cpp}" "CloseAndDrainTest();" "drain tested")
require_text("${test_cpp}" "InflightBoundTest();" "bound over in-flight lines tested")
require_text("${test_cpp}" "ChunkedPopTest();" "chunked pop tested")
foreach (src policy_h log_h log_cpp world_cpp master gm_cpp test_cpp)
  require_text("${${src}}" "twow-repo#597" "twow-repo#597 tag in ${src}")
endforeach()

message(STATUS "LOG_SINK_597_CONTRACT=PASS")
