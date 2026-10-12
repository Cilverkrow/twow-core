# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A08, cards 7 and 41, Engine::CreateActionNode): behind
# AiPlayerbot.Perf.ActionNodeOwnerCache (default 0) each Engine remembers, per base action name (the
# part before the first "::"), the first strategy in `strategies` order whose ActionNode factories
# answer it, or that none does, and then asks only that strategy. The ActionNode is still created
# fresh per call. Every insert/erase/clear of `strategies` empties the cache.
# Only the bot's own AI tick uses the cache (DoNextAction pushes, ProcessTriggers, PushDefaultActions,
# PushAgain from DoNextAction). ExecuteAction / CanExecuteAction / Init keep the plain walk, because
# chat commands and cross-bot calls ("do ...", .bot, RpgSubActions "trade") reach them on foreign
# threads (review 1, option a).
# The cache is neutral only while (a) no Strategy overrides GetAction, (b) Strategy::GetAction is the
# factory lookup, (c) the lookup key is the part before the first "::" and (d) every ActionNode
# creator returns a new node unconditionally. Those are pinned below, with negative probes at the end.
# Errors are collected so the probes can reuse the checks; every probe anchor must exist.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text err text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    set(${err} "${${err}}missing ${description}\n" PARENT_SCOPE)
  endif()
endfunction()

function(forbid_text err text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    set(${err} "${${err}}forbidden ${description}\n" PARENT_SCOPE)
  endif()
endfunction()

function(require_order err text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(${err} "${${err}}order: ${description}\n" PARENT_SCOPE)
  endif()
endfunction()

# Plain substring count (no regex, no list semantics).
function(count_text text needle out_var)
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
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

# Regex hit count. Each hit is replaced by a marker which is then counted as plain text, so ';' or
# an unbalanced '[' in a hit cannot change the count (CMake list semantics, review 2).
function(count_regex text regex out_var)
  string(REGEX REPLACE "${regex}" "<A08HIT>" marked "${text}")
  count_text("${marked}" "<A08HIT>" n)
  set(${out_var} ${n} PARENT_SCOPE)
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n}\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

# The original strategy walk of CreateActionNode, byte for byte (the switch-off path).
set(original_walk "    ActionNode* actionNode = nullptr;
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        actionNode = strategy->GetAction(name);
        if (actionNode)
        {
            break;
        }
    }

    if (!actionNode)
    {
        actionNode = new ActionNode(name);
    }

    return actionNode;")

set(gate "    if (ownerCache && sPlayerbotAIConfig.perfActionNodeOwnerCache)\n        return CreateActionNodeCached(name);\n")

function(check_engine out cpp h)
  set(errs "")
  # --- Gate and off path.
  function_body("${cpp}" "ActionNode* Engine::CreateActionNode(const std::string& name, bool ownerCache)" create)
  require_text(errs "${create}" "${gate}" "switch gate (ownerCache && switch) in CreateActionNode")
  require_text(errs "${create}" "${original_walk}" "original strategy walk in CreateActionNode (switch off)")
  require_order(errs "${create}" "return CreateActionNodeCached(name);" "    ActionNode* actionNode = nullptr;\n    for (" "gate before the original walk")

  # --- Cached path.
  function_body("${cpp}" "ActionNode* Engine::CreateActionNodeCached(const std::string& name)" cached)
  require_text(errs "${cached}" "std::string baseName(name, 0, name.find(\"::\"));" "base name = part before the first ::")
  require_order(errs "${cached}" "actionNodeOwners.find(baseName)" "Strategy* const owner = cached->second;" "lookup before use")
  require_order(errs "${cached}" "if (!owner)\n            return new ActionNode(name);" "if (ActionNode* actionNode = owner->GetAction(name))" "no owner: plain node, owner: full name")
  forbid_text(errs "${cached}" "GetAction(baseName)" "owner asked with the base name instead of the full name")
  require_text(errs "${cached}" "owner = strategy;\n            break;" "first answering strategy wins")
  require_order(errs "${cached}" "actionNode = strategy->GetAction(name);" "actionNodeOwners[baseName] = owner;" "walk, then remember")
  require_order(errs "${cached}" "actionNodeOwners[baseName] = owner;" "actionNode = new ActionNode(name);" "plain node when no strategy answers")
  count_text("${cpp}" "CreateActionNodeCached(" cached_refs)
  if(NOT cached_refs EQUAL 2)
    set(errs "${errs}CreateActionNodeCached must be defined once and called only from the gate (found ${cached_refs})\n")
  endif()

  # --- Only the bot's own AI tick passes ownerCache = true.
  require_text(errs "${cpp}" "ActionNode* actionNode = CreateActionNode(nextAction->getName(), ownerCache);" "MultiplyAndPush forwards ownerCache")
  function_body("${cpp}" "ActionResult Engine::ExecuteAction(const std::string& name, Event& event)" execute)
  require_text(errs "${execute}" "ActionNode* actionNode = CreateActionNode(name);" "ExecuteAction keeps the plain walk")
  require_text(errs "${execute}" "MultiplyAndPush(action->getContinuers(), 0.0f, false, event, \"default\");" "ExecuteAction continuers keep the plain walk")
  forbid_text(errs "${execute}" ", true)" "ownerCache in ExecuteAction (foreign-thread entry)")
  forbid_text(errs "${execute}" "ownerCache" "ownerCache in ExecuteAction (foreign-thread entry)")
  function_body("${cpp}" "bool Engine::CanExecuteAction(const std::string& name, bool isUseful, bool isPossible)" can_execute)
  require_text(errs "${can_execute}" "ActionNode* actionNode = CreateActionNode(name);" "CanExecuteAction keeps the plain walk")
  forbid_text(errs "${can_execute}" "ownerCache" "ownerCache in CanExecuteAction (foreign-thread entry)")
  forbid_text(errs "${can_execute}" ", true)" "ownerCache in CanExecuteAction (foreign-thread entry)")
  function_body("${cpp}" "void Engine::Init()" init)
  require_text(errs "${init}" "MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), \"default\");" "Init keeps the plain walk (strategy changes may come from foreign threads)")
  function_body("${cpp}" "void Engine::ProcessTriggers(bool minimal)" triggers)
  require_text(errs "${triggers}" "MultiplyAndPush(node->getHandlers(), 0.0f, false, event, \"trigger\", true);" "trigger pushes use the cache")
  function_body("${cpp}" "void Engine::PushDefaultActions()" defaults)
  require_text(errs "${defaults}" "MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), \"default\", true);" "default pushes use the cache")
  function_body("${cpp}" "void Engine::PushAgain(ActionNode* actionNode, float relevance, const Event& event, bool ownerCache)" again)
  require_text(errs "${again}" "MultiplyAndPush(nextAction, relevance, true, event, \"again\", ownerCache);" "PushAgain forwards ownerCache")
  function_body("${cpp}" "bool Engine::DoNextAction(Unit* unit, int depth, bool minimal, bool isStunned)" tick)
  require_text(errs "${tick}" "PushAgain(actionNode, relevance, event, true);" "relevance retry uses the cache")
  require_text(errs "${tick}" "MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.02, false, event, \"prereq\", true)" "prereq pushes use the cache")
  require_text(errs "${tick}" "PushAgain(actionNode, relevance + 0.01, event, true);" "prereq retry uses the cache")
  require_text(errs "${tick}" "MultiplyAndPush(actionNode->getContinuers(), 0, false, event, \"cont\", true);" "continuer pushes use the cache")
  count_text("${tick}" "MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.03, false, event, \"alt\", true);" alt_pushes)
  if(NOT alt_pushes EQUAL 2)
    set(errs "${errs}both alternative pushes of DoNextAction use the cache (found ${alt_pushes})\n")
  endif()
  # Every ownerCache = true call in Engine.cpp sits in the AI-tick functions above (7 + 1 + 1). The 7th in
  # DoNextAction is the Perf.ActionBudget re-queue of a deferred action (twow-repo#541, 11.10.2026), also on the
  # bot's own tick.
  count_regex("${cpp}" "(MultiplyAndPush|PushAgain|CreateActionNode)\\([^;{]*, true\\)" cache_calls)
  if(NOT cache_calls EQUAL 9)
    set(errs "${errs}ownerCache = true only in DoNextAction (7), ProcessTriggers and PushDefaultActions (found ${cache_calls})\n")
  endif()

  # --- Header: defaults off, owner map, clear helper, no node cache.
  require_text(errs "${h}" "#include <unordered_map>" "include")
  require_text(errs "${h}" "bool MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType, bool ownerCache = false);" "MultiplyAndPush ownerCache defaults to false")
  require_text(errs "${h}" "void PushAgain(ActionNode* actionNode, float relevance, const Event& event, bool ownerCache = false);" "PushAgain ownerCache defaults to false")
  require_text(errs "${h}" "ActionNode* CreateActionNode(const std::string& name, bool ownerCache = false);" "CreateActionNode ownerCache defaults to false")
  require_text(errs "${h}" "ActionNode* CreateActionNodeCached(const std::string& name);" "declaration")
  require_text(errs "${h}" "std::unordered_map<std::string, Strategy*> actionNodeOwners;" "per-engine owner map")
  require_text(errs "${h}" "void ClearActionNodeOwners() { if (!actionNodeOwners.empty()) actionNodeOwners.clear(); }" "clear helper")
  forbid_text(errs "${h}" "std::unordered_map<std::string, ActionNode*>" "cached ActionNode (nodes are owned and deleted by callers)")
  forbid_text(errs "${h}" "static std::unordered_map" "static cache in Engine.h")
  forbid_text(errs "${cpp}" "static std::unordered_map" "static cache in Engine.cpp")
  forbid_text(errs "${h}" "shared_mutex" "std::shared_mutex in Engine.h")
  forbid_text(errs "${cpp}" "shared_mutex" "std::shared_mutex in Engine.cpp")
  forbid_text(errs "${h}" "thread_local" "thread_local in Engine.h")
  forbid_text(errs "${cpp}" "thread_local" "thread_local in Engine.cpp")

  # --- Invalidation at every change of `strategies`, never gated.
  function_body("${cpp}" "Engine::~Engine(void)" dtor)
  require_order(errs "${dtor}" "strategies.clear();" "ClearActionNodeOwners();" "destructor clears the cache")
  function_body("${cpp}" "void Engine::addStrategy(const std::string& name)" add)
  require_order(errs "${add}" "strategies.insert(" "ClearActionNodeOwners();" "insert, then clear")
  require_order(errs "${add}" "ClearActionNodeOwners();" "strategy->OnStrategyAdded(state);" "clear before OnStrategyAdded")
  function_body("${cpp}" "bool Engine::removeStrategy(const std::string& name, bool init)" remove)
  require_text(errs "${remove}" "strategies.erase(i);\n    ClearActionNodeOwners();\n" "erase, then clear")
  require_order(errs "${remove}" "ClearActionNodeOwners();" "removed->OnStrategyRemoved(state);" "clear before OnStrategyRemoved")
  function_body("${cpp}" "void Engine::removeAllStrategies()" remove_all)
  require_order(errs "${remove_all}" "strategies.clear();" "ClearActionNodeOwners();" "clear all, then the cache")
  require_order(errs "${remove_all}" "ClearActionNodeOwners();" "Init();" "cache cleared before Init")

  count_regex("${cpp}" "strategies\\.(insert|erase|clear|emplace|emplace_hint|swap|merge|extract)\\(" m1)
  count_regex("${cpp}" "strategies\\[" m2)
  count_regex("${cpp}" "[^A-Za-z_]strategies[ \t]*=[^=]" m3)
  math(EXPR mutations "${m1} + ${m2} + ${m3}")
  count_regex("${cpp}" "ClearActionNodeOwners\\(\\)" clears)
  if(NOT mutations EQUAL clears)
    set(errs "${errs}every change of strategies needs ClearActionNodeOwners (mutations=${mutations}, clears=${clears})\n")
  endif()
  if(mutations LESS 4)
    set(errs "${errs}expected at least the 4 known changes of strategies (found ${mutations})\n")
  endif()
  # The switch is read exactly once (the gate); the invalidation is never gated.
  count_text("${cpp}" "perfActionNodeOwnerCache" gates)
  if(NOT gates EQUAL 1)
    set(errs "${errs}perfActionNodeOwnerCache must be read only in the CreateActionNode gate (found ${gates})\n")
  endif()
  forbid_text(errs "${h}" "perfActionNodeOwnerCache" "switch read in Engine.h")
  set(${out} "${errs}" PARENT_SCOPE)
endfunction()

function(check_config out h cpp conf)
  set(errs "")
  require_text(errs "${h}" "    bool perfActionNodeOwnerCache = false;  // twow-repo#541 (audit A08):" "member default off")
  require_text(errs "${cpp}" "    perfActionNodeOwnerCache = config.GetBoolDefault(\"AiPlayerbot.Perf.ActionNodeOwnerCache\", false);" "config default 0")
  require_text(errs "${conf}" "\nAiPlayerbot.Perf.ActionNodeOwnerCache = 0\n" "documented key, value 0")
  forbid_text(errs "${conf}" "\nAiPlayerbot.Perf.ActionNodeOwnerCache = 1" "documented key on")
  set(${out} "${errs}" PARENT_SCOPE)
endfunction()

function(check_invariants out strategy_cpp noc_h aiobject_h)
  set(errs "")
  function_body("${strategy_cpp}" "ActionNode* Strategy::GetAction(std::string name)" get_action)
  require_text(errs "${get_action}" "return actionNodeFactories.GetObject(name, ai);" "Strategy::GetAction is the factory lookup")
  require_order(errs "${noc_h}" "if (size_t pos = nameView.find(\"::\"); pos != std::string::npos)" "nameView = nameView.substr(0, pos);" "factory key = part before the first ::")
  require_order(errs "${noc_h}" "nameView = nameView.substr(0, pos);" "auto it = creators.find(std::string(nameView));" "factory looks up the base name")
  require_text(errs "${noc_h}" "if (T* obj = (*it)->Create(name, ai))" "factory list: first answer wins")
  count_regex("${aiobject_h}" "#define ACTION_NODE_" macros)
  count_regex("${aiobject_h}" "return new ActionNode\\(spell," macro_news)
  if(NOT macros EQUAL 3 OR NOT macro_news EQUAL 3)
    set(errs "${errs}ACTION_NODE_P/A/C must stay unconditional new ActionNode (macros=${macros}, news=${macro_news})\n")
  endif()
  set(${out} "${errs}" PARENT_SCOPE)
endfunction()

function(check_source_file out rel text)
  set(errs "")
  if(NOT rel STREQUAL "strategy/Strategy.h")
    count_regex("${text}" "ActionNode[ \t]*\\*[ \t]*GetAction[ \t]*\\(" overrides)
    if(overrides GREATER 0)
      set(errs "${errs}${rel}: Strategy::GetAction must not be overridden\n")
    endif()
  endif()
  if(NOT rel STREQUAL "strategy/AiObject.h")
    count_regex("${text}" "static[ \t]+ActionNode[ \t]*\\*" creators)
    count_regex("${text}" "static[ \t]+ActionNode[ \t]*\\*[ \t]*[A-Za-z_0-9]+[ \t]*\\([ \t]*PlayerbotAI[ \t]*\\*[ \t]*[A-Za-z_0-9]*[ \t]*\\)[ \t\n]*[{][ \t\n]*return new ActionNode" plain)
    if(NOT creators EQUAL plain)
      set(errs "${errs}${rel}: every ActionNode creator must start with return new ActionNode (${plain}/${creators})\n")
    endif()
  endif()
  string(FIND "${text}" "NamedObjectFactory<ActionNode>" is_factory)
  if(NOT is_factory EQUAL -1)
    forbid_text(errs "${text}" "](PlayerbotAI" "${rel}: lambda ActionNode creator")
  endif()
  set(${out} "${errs}" PARENT_SCOPE)
endfunction()

read_source("strategy/Engine.cpp" engine_cpp)
read_source("strategy/Engine.h" engine_h)
read_source("strategy/Strategy.cpp" strategy_cpp)
read_source("strategy/NamedObjectContext.h" noc_h)
read_source("strategy/AiObject.h" aiobject_h)
read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)

check_engine(e_engine "${engine_cpp}" "${engine_h}")
check_config(e_config "${config_h}" "${config_cpp}" "${conf_dist}")
check_invariants(e_inv "${strategy_cpp}" "${noc_h}" "${aiobject_h}")
set(e_files "")
file(GLOB_RECURSE pb_sources RELATIVE "${PB_SOURCE_DIR}" "${PB_SOURCE_DIR}/*.h" "${PB_SOURCE_DIR}/*.cpp")
list(LENGTH pb_sources pb_source_count)
if(pb_source_count LESS 100)
  message(FATAL_ERROR "#541 A08: expected the playerbot source tree under PB_SOURCE_DIR (found ${pb_source_count} files)")
endif()
foreach(rel IN LISTS pb_sources)
  read_source("${rel}" src)
  check_source_file(e_one "${rel}" "${src}")
  string(APPEND e_files "${e_one}")
endforeach()
set(all "${e_engine}${e_config}${e_inv}${e_files}")
if(NOT all STREQUAL "")
  message(FATAL_ERROR "#541 A08 action node owner cache:\n${all}")
endif()

# ---- Negative probes: each mutation must be caught by the checks above.
function(mutate text from to out_var)
  string(FIND "${text}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "#541 A08: probe anchor missing: ${from}")
  endif()
  string(REPLACE "${from}" "${to}" mutated "${text}")
  set(${out_var} "${mutated}" PARENT_SCOPE)
endfunction()

function(mutate_first text from to out_var)
  string(FIND "${text}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "#541 A08: probe anchor missing: ${from}")
  endif()
  string(SUBSTRING "${text}" 0 ${at} head)
  string(LENGTH "${from}" from_len)
  math(EXPR tail_at "${at} + ${from_len}")
  string(SUBSTRING "${text}" ${tail_at} -1 tail)
  set(${out_var} "${head}${to}${tail}" PARENT_SCOPE)
endfunction()

function(expect_caught label errors)
  if(errors STREQUAL "")
    message(FATAL_ERROR "#541 A08: negative probe not caught: ${label}")
  endif()
endfunction()

# P1 erase without invalidation
mutate("${engine_cpp}" "strategies.erase(i);\n    ClearActionNodeOwners();\n" "strategies.erase(i);\n" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("removeStrategy without ClearActionNodeOwners" "${e}")
# P2 cache always on (gate removed)
mutate("${engine_cpp}" "${gate}" "    return CreateActionNodeCached(name);\n" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("switch gate removed" "${e}")
# P3 invalidation gated by the switch (stale after reload 1->0->1)
mutate("${engine_cpp}" "    ClearActionNodeOwners();\n    Init();" "    if (sPlayerbotAIConfig.perfActionNodeOwnerCache)\n        ClearActionNodeOwners();\n    Init();" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("gated invalidation" "${e}")
# P4 a new strategies mutation without a clear
check_engine(e "${engine_cpp}\nvoid Engine::Probe() { strategies.erase(\"x\"); }\n" "${engine_h}")
expect_caught("new strategies mutation without clear" "${e}")
# P5 two subscript mutations with only one clear (bracket list semantics, review 2)
check_engine(e "${engine_cpp}\nvoid Engine::Probe() { strategies[\"x\"] = nullptr; strategies[\"y\"] = nullptr; ClearActionNodeOwners(); }\n" "${engine_h}")
expect_caught("two subscript mutations, one clear" "${e}")
# P6 node cached instead of owner
check_engine(e "${engine_cpp}" "${engine_h}\nstd::unordered_map<std::string, ActionNode*> actionNodeCache;\n")
expect_caught("ActionNode* cache" "${e}")
# P7 owner asked with the base name
mutate("${engine_cpp}" "owner->GetAction(name)" "owner->GetAction(baseName)" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("GetAction(baseName)" "${e}")
# P8 switch-off walk dropped from CreateActionNode (first copy of the walk line is the original one)
mutate_first("${engine_cpp}" "        actionNode = strategy->GetAction(name);\n" "" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("original walk removed" "${e}")
# P9 ExecuteAction (foreign-thread entry) uses the cache
mutate("${engine_cpp}" "    ActionResult actionResult = ACTION_RESULT_UNKNOWN;\n    ActionNode* actionNode = CreateActionNode(name);" "    ActionResult actionResult = ACTION_RESULT_UNKNOWN;\n    ActionNode* actionNode = CreateActionNode(name, true);" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("ExecuteAction with ownerCache" "${e}")
# P10 Init (strategy changes from any thread) uses the cache
mutate("${engine_cpp}" "        strategy->InitTriggers(triggers, state);\n        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), \"default\");" "        strategy->InitTriggers(triggers, state);\n        MultiplyAndPush(strategy->getDefaultActions(state), 0.0f, false, Event(), \"default\", true);" p)
check_engine(e "${p}" "${engine_h}")
expect_caught("Init with ownerCache" "${e}")
# P11 ownerCache defaults to true
mutate("${engine_h}" "ActionNode* CreateActionNode(const std::string& name, bool ownerCache = false);" "ActionNode* CreateActionNode(const std::string& name, bool ownerCache = true);" p)
check_engine(e "${engine_cpp}" "${p}")
expect_caught("ownerCache default true" "${e}")
# P12 config default on
mutate("${config_cpp}" "\"AiPlayerbot.Perf.ActionNodeOwnerCache\", false)" "\"AiPlayerbot.Perf.ActionNodeOwnerCache\", true)" p)
check_config(e "${config_h}" "${p}" "${conf_dist}")
expect_caught("config default true" "${e}")
# P13 a strategy overriding GetAction (whitespace variant)
check_source_file(e "strategy/generic/ProbeStrategy.h" "class ProbeStrategy : public Strategy\n{\n    ActionNode *GetAction (std::string name) override;\n};\n")
expect_caught("GetAction override" "${e}")
# P14 a conditional ActionNode creator (whitespace and parameter-name variant)
check_source_file(e "strategy/ProbeFactory.cpp" "class F : public NamedObjectFactory<ActionNode>\n{\n    static ActionNode *x(PlayerbotAI* bot)\n    {\n        if (!bot)\n            return nullptr;\n        return new ActionNode(\"x\", NULL, NULL, NULL);\n    }\n};\n")
expect_caught("conditional creator" "${e}")
# P15 factory key no longer the part before the FIRST ::
mutate("${noc_h}" "if (size_t pos = nameView.find(\"::\");" "if (size_t pos = nameView.rfind(\"::\");" p)
check_invariants(e "${strategy_cpp}" "${p}" "${aiobject_h}")
expect_caught("factory key rule changed" "${e}")

message(STATUS "action_node_owner_cache source contract passed (15 negative probes)")
