# twow-repo#597 (TICK-01, AhBot guard): live runs without ahbot.conf, and AhBot::Update scheduled an auction check on
# every world tick (detached boost::thread + synchronous mail LIKE query + log lines, updateInterval never read = 0).
# AiPlayerbot.AhBot.RequireConfig (aiplayerbot.conf, default 0) makes Update return as its first statement unless the
# AhBot config was loaded and AhBot.Enabled is on. Pinned here:
# - the switch default 0 in code and conf, and that it is read from aiplayerbot.conf (not ahbot.conf);
# - with 1: the early return comes before RunQueuedWork, the thread start, the mail query and every line of Update;
# - with 0: the guard is one short-circuited bool read and the old per-tick path is byte for byte the same;
# - the one [AhBotGuard] startup line (format, only with 1) and the defined AhBotConfig members.
# Run: cmake -DPB_MODULE_DIR=<modules/mod-playerbots> -P t/ahbot_guard_597_source_contract_tests.cmake

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

if(NOT PB_MODULE_DIR)
  message(FATAL_ERROR "#597: PB_MODULE_DIR not set")
endif()

function(read_source path out_var)
  file(READ "${PB_MODULE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#597: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#597: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#597: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#597: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#597: function end not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_length)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_length}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL ${expected})
    message(FATAL_ERROR "#597: ${description}: expected ${expected}, found ${count}")
  endif()
endfunction()

read_source("src/ahbot/AhBotGuardPolicy.h" policy)
read_source("src/ahbot/AhBot.cpp" ahbot)
read_source("src/ahbot/AhBotConfig.h" ahconf_h)
read_source("src/ahbot/AhBotConfig.cpp" ahconf_cpp)
read_source("src/ahbot/ahbot.conf.dist.in" ahconf_dist)
read_source("src/playerbot/PlayerbotAIConfig.h" config_h)
read_source("src/playerbot/PlayerbotAIConfig.cpp" config_cpp)
read_source("src/playerbot/aiplayerbot.conf.dist.in" conf_dist)
read_source("tests.cmake" tests_cmake)

# 1. Switch: default 0 in code and conf, read from aiplayerbot.conf because it must work without ahbot.conf.
require_text("${config_h}" "    bool ahBotRequireConfig = false;  // twow-repo#597 (TICK-01)" "member default off")
require_text("${config_cpp}" "\n    ahBotRequireConfig = config.GetBoolDefault(\"AiPlayerbot.AhBot.RequireConfig\", false);\n" "load default 0")
require_text("${conf_dist}" "\nAiPlayerbot.AhBot.RequireConfig = 0\n" "documented off")
require_text("${conf_dist}" "# twow-repo#597 (TICK-01, AhBot guard)" "documented with its ticket")
require_text("${conf_dist}" "#   0 = as before, no extra line (default)." "documented default meaning")
require_text("${conf_dist}" "# Live should run 1 after the A/B M-H1" "live recommendation after M-H1")
forbid_text("${ahconf_dist}" "RequireConfig" "switch in ahbot.conf (unreadable exactly when the file is missing)")
forbid_text("${ahconf_cpp}" "RequireConfig\"" "switch read through the ahbot config")

# 2. Pure decision: fail closed, only a loaded AND enabled config runs with the switch on.
require_text("${policy}" "namespace ahbot::guard" "policy namespace")
require_text("${policy}" "inline bool SkipUpdate(bool requireConfig, bool configLoaded, bool enabled)" "decision signature")
require_text("${policy}" "        return requireConfig && !(configLoaded && enabled);" "fail-closed decision")
require_text("${policy}" "        return SkipUpdate(requireConfig, configLoaded, enabled) ? \"idle\" : \"scheduled\";" "update= token")
forbid_text("${policy}" "#include \"" "project includes in the header-only policy")
forbid_text("${policy}" "shared_mutex" "shared_mutex")

# 3. Update: the guard is the first statement, before anything that costs (queued work, shutdown checks, the clock,
#    the line, the thread, the query). With 0 it is one short-circuited bool read.
require_text("${ahbot}" "#include \"AhBotGuardPolicy.h\"" "policy include")
function_body("${ahbot}" "void AhBot::Update()\n{\n" update)
set(guard "    if (sPlayerbotAIConfig.ahBotRequireConfig &&\n        ahbot::guard::SkipUpdate(true, sAhBotConfig.configLoaded, sAhBotConfig.enabled))\n        return;\n")
require_text("${update}" "${guard}" "guard block (switch first, short circuit, plain return)")
require_count("${update}" "ahbot::guard::SkipUpdate(" 1 "one guard in Update")
require_count("${update}" "sPlayerbotAIConfig.ahBotRequireConfig" 1 "one switch read in Update")
# Nothing but comments between the opening brace and the guard.
string(FIND "${update}" "${guard}" guard_at)
string(LENGTH "void AhBot::Update()\n{\n" head_len)
math(EXPR prefix_len "${guard_at} - ${head_len}")
string(SUBSTRING "${update}" ${head_len} ${prefix_len} prefix)
string(REGEX REPLACE "[ ]*//[^\n]*\n" "" prefix_code "${prefix}")
string(STRIP "${prefix_code}" prefix_code)
if(NOT prefix_code STREQUAL "")
  message(FATAL_ERROR "#597: code in front of the guard in AhBot::Update: ${prefix_code}")
endif()
require_order("${update}" "${guard}" "if (sWorld.IsShutdowning())" "guard before the shutdown checks")
require_order("${update}" "${guard}" "RunQueuedWork();" "guard before RunQueuedWork")
require_order("${update}" "${guard}" "time(0)" "guard before any clock read")
require_order("${update}" "${guard}" "sLog.out" "guard before any line")
require_order("${update}" "${guard}" "activateAhbotThread();" "guard before the thread start")
require_order("${update}" "${guard}" "CleanupPropositions();" "guard before the mail LIKE query")
# Off path (switch 0, or loaded and enabled): the old body after the guard, unchanged.
set(old_path "\n    if (sWorld.IsShutdowning())\n        return;\n\n    if (sWorld.IsStopped())\n        return;\n\n")
require_text("${update}" "${old_path}" "shutdown checks unchanged")
set(old_tail "    RunQueuedWork();\n\n    time_t now = time(0);\n\n    if (now < nextAICheckTime)\n        return;\n\n    if (updating)\n    {\n        sLog.outString(\"[AhBot] Update skipped — previous check still running\");\n        return;\n    }\n\n    sLog.outString(\"[AhBot] Scheduling auction check (next after this: in %d seconds)\", sAhBotConfig.updateInterval);\n    nextAICheckTime = time(0) + sAhBotConfig.updateInterval;\n    activateAhbotThread();\n    CleanupPropositions();")
require_text("${update}" "${old_tail}" "per-tick schedule path unchanged")
# The query the guard keeps off the world thread, and the thread body that only logs "disabled" without a config.
require_text("${ahbot}" "select id, receiver from mail where subject like 'AH Proposition%%' and deliver_time <= '%u'" "CleanupPropositions query unchanged")
require_text("${ahbot}" "\tif (!sAhBotConfig.enabled)\n\t{\n\t\tsLog.outString(\"[AhBot] ForceUpdate called but AhBot is disabled in ahbot.conf\");\n\t\treturn;\n\t}" "ForceUpdate enabled check unchanged")

# 4. Init: one [AhBotGuard] line per process, only with the switch on; the old lines and their order unchanged.
function_body("${ahbot}" "void AhBot::Init()\n{\n" init)
require_text("${init}" "    bool const configured = sAhBotConfig.Initialize();\n" "Initialize called once")
require_count("${init}" "sAhBotConfig.Initialize()" 1 "one Initialize call")
require_text("${init}" "    if (sPlayerbotAIConfig.ahBotRequireConfig)\n        sLog.outString(\"[AhBotGuard] require_config=1 config_loaded=%u enabled=%u update=%s\",\n            uint32(sAhBotConfig.configLoaded), uint32(sAhBotConfig.enabled),\n            ahbot::guard::UpdateMode(true, sAhBotConfig.configLoaded, sAhBotConfig.enabled));\n" "[AhBotGuard] line only with the switch")
require_count("${ahbot}" "[AhBotGuard]" 1 "one [AhBotGuard] line in AhBot.cpp (startup only, never per tick)")
require_order("${init}" "[AhBot] Initializing AhBot by ike3" "sAhBotConfig.Initialize()" "init line first")
require_order("${init}" "sAhBotConfig.Initialize()" "[AhBotGuard]" "guard line after the config read")
require_order("${init}" "[AhBotGuard]" "    if (!configured)\n    {\n        sLog.outString(\"[AhBot] Disabled or failed to load ahbot.conf" "guard line before the disabled line")

# 5. AhBotConfig: members defined (0 = what live used without a config), configLoaded only after a found file.
foreach(member IN ITEMS "bool enabled = false;" "bool configLoaded = false;" "uint64 guid = 0;" "uint32 updateInterval = 0;"
        "uint32 historyDays = 0, maxSellInterval = 0;" "uint32 alwaysAvailableMoney = 0;"
        "float priceMultiplier = 0.0f, priceQualityMultiplier = 0.0f;" "float underPriceProbability = 0.0f;" "bool sendmail = false;")
  require_text("${ahconf_h}" "\n    ${member}" "defined AhBotConfig member")
endforeach()
foreach(member IN ITEMS "bool enabled;" "uint32 updateInterval;" "uint64 guid;" "bool sendmail;")
  forbid_text("${ahconf_h}" "\n    ${member}" "indeterminate AhBotConfig member")
endforeach()
function_body("${ahconf_cpp}" "bool AhBotConfig::Initialize()\n{\n" initialize)
require_count("${initialize}" "configLoaded = true;" 1 "one configLoaded store")
require_order("${initialize}" "AhBot is Disabled. No configuration file" "configLoaded = true;" "no config -> not loaded")
require_order("${initialize}" "            return false;" "configLoaded = true;" "the missing-file return stays in front of the flag")
require_order("${initialize}" "AhBot configuration read from %s." "configLoaded = true;" "flag after the file was read")
require_order("${initialize}" "configLoaded = true;" "enabled = config.GetBoolDefault(\"AhBot.Enabled\", true);" "flag before AhBot.Enabled")
require_text("${initialize}" "updateInterval = config.GetIntDefault(\"AhBot.UpdateIntervalInSeconds\", 900);" "configured interval default unchanged")

# 6. Registration.
require_text("${tests_cmake}" "add_test(NAME ahbot_guard_597_policy\n" "policy test registered")
require_text("${tests_cmake}" "add_test(NAME ahbot_guard_597_source_contract\n" "contract registered")

message(STATUS "#597 AhBot guard source contract OK")
