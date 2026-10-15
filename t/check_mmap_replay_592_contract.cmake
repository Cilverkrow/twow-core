if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#592 PATH-02, PR pair 0: `.mmaps replay <name> [offset]` replays fixture rows on fresh PathInfo
# objects and writes [PathReplay] lines with hexfloat points, the proof base for PathInfo variant A
# (old = new, bit exact). Diagnostic only, no switch: SEC_DEVELOPER, no console, reads only
# DataDir/pathreplay/<plain name>, at most 500 lines and 200 ms wall clock per call, then
# "[PathReplay] partial next=<offset>". Map-mode rows are skipped while PathInfo(mapId, instanceId)
# still drops the map id (crash in calculate()); PR pair 2 changes both together.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#592 replay: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#592 replay: ${label}: ${needle}")
  endif()
endfunction()

function(require_order text first second label)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#592 replay: order ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

# Body of one function: from its signature to the first closing brace at column 0.
function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if (start EQUAL -1)
    message(FATAL_ERROR "#592 replay: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if (end EQUAL -1)
    message(FATAL_ERROR "#592 replay: end of function not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("src/game/Chat/Chat.cpp" chat_cpp)
read_source("src/game/Chat/Chat.h" chat_h)
read_source("src/game/Commands/Commands.cpp" commands)
read_source("src/game/Maps/PathReplay.h" replay_h)
read_source("src/game/Maps/PathFinder.h" finder_h)

# 1. Registered under .mmaps (".mmap replay" abbreviates it), SEC_DEVELOPER, not from the console.
string(FIND "${chat_cpp}" "static ChatCommand mmapsCommandTable[] =" table_at)
if (table_at EQUAL -1)
  message(FATAL_ERROR "#592 replay: mmapsCommandTable not found")
endif()
string(SUBSTRING "${chat_cpp}" ${table_at} -1 table)
string(FIND "${table}" "};" table_end)
string(SUBSTRING "${table}" 0 ${table_end} table)
require_text("${table}"
  "{ \"replay\",          SEC_DEVELOPER,     false, &ChatHandler::HandleMmapsReplayCommand,"
  "replay entry (SEC_DEVELOPER, console false) in mmapsCommandTable")
require_text("${chat_h}" "bool HandleMmapsReplayCommand(char* args);" "handler declaration")

# 2. Limits and the fixed directory are constants of the policy header (t/path_replay_test.cpp runs them).
require_text("${replay_h}" "constexpr char const* PATH_REPLAY_DIR = \"pathreplay/\";" "fixed directory")
require_text("${replay_h}" "constexpr uint32_t PATH_REPLAY_MAX_LINES = 500;" "line cap 500")
require_text("${replay_h}" "constexpr uint32_t PATH_REPLAY_BUDGET_MS = 200;" "time budget 200 ms")
require_text("${replay_h}" "constexpr uint32_t PATH_REPLAY_MAX_CHAIN = 40;" "chain cap 40")

function_body("${replay_h}" "inline PathReplayStop PathReplayShouldStop(uint32_t linesRead, uint64_t elapsedMs)" body)
require_text("${body}" "if (linesRead >= PATH_REPLAY_MAX_LINES)" "line cap check")
require_text("${body}" "if (elapsedMs >= PATH_REPLAY_BUDGET_MS)" "time budget check")

# Name guard: whitelist of plain characters, no leading dot, no "..", no separator or drive allowed.
function_body("${replay_h}" "inline bool PathReplayNameAllowed(std::string const& name)" body)
require_text("${body}" "name.size() > PATH_REPLAY_MAX_NAME_LEN" "name length")
require_text("${body}" "if (name[0] == '.' || name.find(\"..\") != std::string::npos)" "leading dot and dot dot")
forbid_text("${body}" "c == '/'" "slash allowed in the name")
forbid_text("${body}" "c == '\\\\'" "backslash allowed in the name")
forbid_text("${body}" "c == ':'" "drive separator allowed in the name")
require_text("${body}" "if (path_replay_detail::IsReservedDeviceStem(name))" "reserved device names refused")
function_body("${replay_h}" "inline bool IsReservedDeviceStem(std::string const& name)" body)
require_text("${body}" "stem == \"CON\" || stem == \"PRN\" || stem == \"AUX\" || stem == \"NUL\"" "CON PRN AUX NUL")
require_text("${body}" "stem.compare(0, 3, \"COM\") == 0 || stem.compare(0, 3, \"LPT\") == 0" "COMn LPTn")

# Memory and I/O bounds: a line keeps at most MAX_LINE_LEN + 1 characters, the file has a size cap.
require_text("${replay_h}" "constexpr uint64_t PATH_REPLAY_MAX_FILE_BYTES = 64ull * 1024 * 1024;" "file size cap 64 MB")
function_body("${replay_h}" "inline PathReplayRead ReadPathReplayLine(std::istream& in, std::string& line)" body)
require_text("${body}" "char buf[PATH_REPLAY_MAX_LINE_LEN + 2];" "fixed line buffer")
require_text("${body}" "in.getline(buf, sizeof(buf));" "bounded read")
require_text("${body}" "in.ignore(std::numeric_limits<std::streamsize>::max(), '\\n');" "rest of a long line skipped")
forbid_text("${body}" "std::getline" "unbounded line read")

function_body("${replay_h}" "inline void AppendPathReplayHex(std::string& out, float v)" body)
require_text("${body}" "\"%a\", double(v)" "hexfloat points")

# 3. The handler: guard before the file is opened, only DataDir/pathreplay/, both limits, unit rows only.
function_body("${commands}" "bool ChatHandler::HandleMmapsReplayCommand(char* args)" body)
require_order("${body}" "if (!PathReplayNameAllowed(name) || offset > PATH_REPLAY_MAX_OFFSET)" "std::ifstream in(fileName);"
  "name guard before the file is opened")
require_text("${body}" "std::string const fileName = sWorld.GetDataPath() + PATH_REPLAY_DIR + name;" "fixed directory path")
require_text("${body}" "probe->GetMap() != player->GetMap() || probe->GetTransport()" "probe on the caller's map, not on a transport")
require_text("${body}" "PathFinder pathFinder(probe);" "fresh unit-mode PathInfo per row")
require_order("${body}" "while ((lineRead = ReadPathReplayLine(in, line)) != PathReplayRead::End)" "PathFinder pathFinder(probe);"
  "PathInfo inside the row loop")
require_text("${body}" "while (lineNo < offset && ReadPathReplayLine(in, line) != PathReplayRead::End)" "bounded offset skip")
forbid_text("${body}" "std::getline" "unbounded line read in the handler")
require_order("${body}" "if (!in || !PathReplayFileSizeAllowed(fileSize))" "while (lineNo < offset"
  "file size cap before the first read")
require_text("${body}" "PathReplayRead::TooLong ? PathReplayParse::Bad" "skipped long line counts as bad")
require_text("${body}" "skipped=map_mode" "map rows reported as skipped")
forbid_text("${body}" "PathFinder pathFinder(row." "map-mode PathInfo before PR pair 2")
forbid_text("${body}" "std::make_unique<PathFinder>" "map-mode PathInfo before PR pair 2")
require_text("${body}" "stop = PathReplayShouldStop(linesRead, elapsedMs);" "limits checked in the row loop")
require_order("${body}" "LogPathReplayResult(row.id, -1, pathFinder);" "stop = PathReplayShouldStop(linesRead, elapsedMs);"
  "limits checked after the running row")
require_text("${body}" "std::chrono::steady_clock::now() - started" "wall clock")
require_text("${body}" "\"[PathReplay] partial next=%u reason=%s" "partial line with the next offset")
require_text("${body}" "\"[PathReplay] done lines=%u" "done line")
forbid_text("${body}" "getConfig(" "no switch: the command reads no configuration")
forbid_text("${body}" "sConfig" "no switch: the command reads no configuration")
forbid_text("${body}" "ofstream" "the command writes no file")

# 4. Map rows stay skipped exactly as long as the map-mode constructor drops the map id. PathInfo variant A
#    (PR pair 2) changes this line and must enable the map rows of the replay in the same change.
require_text("${finder_h}" "PathInfo(uint32 /*mapId*/, uint32 /*instanceId*/) : PathInfo((Unit const*)nullptr) {}"
  "map-mode constructor unchanged (update the replay map rows together with PathInfo variant A)")

message(STATUS "MMAP_REPLAY_592_CONTRACT=PASS")
