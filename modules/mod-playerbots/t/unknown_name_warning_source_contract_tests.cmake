cmake_policy(VERSION 3.16)

# twow-repo#568 (c), strategy diet (owner order 10.10.2026): behind AiPlayerbot.WarnUnknownNames (default 0)
# a trigger, action or strategy name without a creator is logged once per process as
# "[UnknownName] kind=.. name='..' bot=.. class=.. engine=.. strategies=..". Today such names die silently:
# addStrategy ignores the strategy, ProcessTriggers skips the node, the action ends "A:<name> - UNKNOWN".
# Warning only: the hooks sit inside the existing null branches, the lookups and their paths are unchanged.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#568 unknown names: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#568 unknown names: forbidden ${description}: ${needle}")
  endif()
endfunction()

# Each needle must exist and come after the previous one. Needles are read as ARGV<i>, not ARGN:
# ARGN is a ';' list and most needles end with ';'.
function(require_sequence text description)
  set(last -1)
  math(EXPR argc_last "${ARGC} - 1")
  foreach(i RANGE 2 ${argc_last})
    set(needle "${ARGV${i}}")
    string(FIND "${text}" "${needle}" at)
    if(at EQUAL -1)
      message(FATAL_ERROR "#568 unknown names: ${description}: missing '${needle}'")
    endif()
    if(NOT at GREATER last)
      message(FATAL_ERROR "#568 unknown names: ${description}: '${needle}' out of order")
    endif()
    set(last ${at})
  endforeach()
endfunction()

# Body from the signature up to the first line that is exactly "<indent>}".
function(function_body text signature end_marker out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#568 unknown names: function not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" end)
  if(end EQUAL -1)
    message(FATAL_ERROR "#568 unknown names: end of function not found: ${signature}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/Engine.h" engine_h)
read_source("strategy/Engine.cpp" engine_cpp)
read_source("strategy/ReactionEngine.cpp" reaction_cpp)
read_source("RandomPlayerbotMgr.cpp" rpm_cpp)

# Switch: default off everywhere.
require_text("${config_h}" "bool warnUnknownNames = false;" "member default off")
require_text("${config_cpp}" "warnUnknownNames = config.GetBoolDefault(\"AiPlayerbot.WarnUnknownNames\", false);" "config default 0")
require_text("${conf_dist}" "\nAiPlayerbot.WarnUnknownNames = 0\n" "documented key, default 0")

# Engine-local set (own thread only), declared in the header.
require_text("${engine_h}" "#include <unordered_set>" "unordered_set include")
require_text("${engine_h}" "std::unordered_set<std::string> unknownNamesSeen[3];" "engine-local set per kind")
require_text("${engine_h}" "void WarnUnknownName(UnknownNameKind kind, const std::string& name);" "warning helper")
forbid_text("${engine_cpp}" "shared_mutex" "shared_mutex")
forbid_text("${engine_h}" "shared_mutex" "shared_mutex")

# Process-wide registry only under its mutex.
function_body("${engine_cpp}" "bool FirstUnknownNameInProcess(const std::string& key)" "\n    }\n" registry_body)
require_sequence("${registry_body}" "registry under its mutex"
  "static std::mutex unknownNamesLock;"
  "static std::set<std::string> unknownNames;"
  "std::lock_guard<std::mutex> guard(unknownNamesLock);"
  "unknownNames.size() >= UnknownNamesProcessCap"
  "return unknownNames.insert(key).second;")

# Helper: local set first, then the registry, then one greppable line. It changes no lookup state.
function_body("${engine_cpp}" "void Engine::WarnUnknownName(UnknownNameKind kind, const std::string& name)" "\n}\n" warn_body)
require_sequence("${warn_body}" "local set before registry before log"
  "seen.find(name) != seen.end() || seen.size() >= UnknownNamesPerEngineCap"
  "seen.insert(name);"
  "if (!FirstUnknownNameInProcess(key))"
  "sLog.outError(\"[UnknownName] kind=%s name='%s' bot=%s class=%u engine=%s strategies=%s\"")
require_text("${warn_body}" "list.resize(297);" "strategy list truncated to ~300 chars")
foreach(mutation "setTrigger" "setAction" "triggers.erase" "triggers.remove" "strategies.erase" "strategies.insert")
  forbid_text("${warn_body}" "${mutation}" "lookup state change in WarnUnknownName")
endforeach()

# Hook 1: addStrategy - null branch, gated, Init path after it unchanged.
function_body("${engine_cpp}" "void Engine::addStrategy(const std::string& name)" "\n}\n" add_body)
require_sequence("${add_body}" "strategy hook in the null branch"
  "Strategy* strategy = aiObjectContext->GetStrategy(name);"
  "if (strategy)"
  "strategy->OnStrategyAdded(state);"
  "else if (sPlayerbotAIConfig.warnUnknownNames)"
  "WarnUnknownName(UnknownNameKind::Strategy, name);"
  "if (!initMode && StrategySetToken() != tokenBefore)")

# Hook 2: ProcessTriggers - null branch, gated, the original 'continue' kept.
function_body("${engine_cpp}" "void Engine::ProcessTriggers(bool minimal)" "\n}\n" triggers_body)
require_sequence("${triggers_body}" "trigger hook in the null branch"
  "trigger = aiObjectContext->GetTrigger(node->getName());"
  "node->setTrigger(trigger);"
  "if (!trigger && sPlayerbotAIConfig.warnUnknownNames)"
  "WarnUnknownName(UnknownNameKind::Trigger, node->getName());"
  "if (!trigger)\n            continue;")

# Hook 3: InitializeAction in both engines - null branch, gated; the UNKNOWN path kept.
function_body("${engine_cpp}" "Action* Engine::InitializeAction(ActionNode* actionNode)" "\n}\n" init_body)
require_sequence("${init_body}" "action hook in the null branch"
  "action = aiObjectContext->GetAction(actionNode->getName());"
  "actionNode->setAction(action);"
  "if (!action && sPlayerbotAIConfig.warnUnknownNames)"
  "WarnUnknownName(UnknownNameKind::Action, actionNode->getName());"
  "action->SetReaction(false);")
function_body("${reaction_cpp}" "ai::Action* ReactionEngine::InitializeAction(ActionNode* actionNode)" "\n}\n" rinit_body)
require_sequence("${rinit_body}" "reaction action hook in the null branch"
  "action = aiObjectContext->GetAction(actionNode->getName());"
  "actionNode->setAction(action);"
  "if (!action && sPlayerbotAIConfig.warnUnknownNames)"
  "WarnUnknownName(UnknownNameKind::Action, actionNode->getName());"
  "action->SetReaction(true);")
require_text("${engine_cpp}" "LogAction(\"A:%s - UNKNOWN\", actionNode->getName().c_str());" "original UNKNOWN path")

# No per-minute summary and nothing new in UpdateAIInternal (no new return before PerfMon Init).
function_body("${rpm_cpp}" "void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed, bool minimal)" "\n}\n" update_body)
forbid_text("${update_body}" "UnknownName" "unknown-name code in UpdateAIInternal")

# Exactly the hooks above: 3 calls in Engine.cpp plus the definition, 1 call in ReactionEngine.cpp (an extra,
# possibly ungated call would otherwise pass the sequence checks).
string(REGEX MATCHALL "WarnUnknownName[(]" engine_calls "${engine_cpp}")
list(LENGTH engine_calls engine_call_count)
if(NOT engine_call_count EQUAL 4)
  message(FATAL_ERROR "WarnUnknownName( in Engine.cpp: expected 4 (3 hooks + definition), found ${engine_call_count}")
endif()
string(REGEX MATCHALL "WarnUnknownName[(]" reaction_calls "${reaction_cpp}")
list(LENGTH reaction_calls reaction_call_count)
if(NOT reaction_call_count EQUAL 1)
  message(FATAL_ERROR "WarnUnknownName( in ReactionEngine.cpp: expected 1, found ${reaction_call_count}")
endif()

message(STATUS "unknown_name_warning source contract passed")
