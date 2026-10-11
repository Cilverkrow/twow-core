# twow-repo#563 phase 2 (X4a, OB-00 go 10.10.2026, stability like #379): the shared value context
# (sSharedObjectContext, GAI_VALUE) is one object for all region threads. Lookups of existing values take
# a shared lock and never insert; creating takes a unique lock (double-checked) and only then builds the
# PlayerbotAI dummy. SingleCalculatedValue computes once under its own mutex and publishes with release /
# acquire, so no thread copies a value that is still being written.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#563 X4a: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#563 X4a: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(function_body text signature out_var)
  string(FIND "${text}" "${signature}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#563 X4a: not found: ${signature}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "\n        }\n" end)
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

read_source("strategy/values/SharedValueContext.h" shared_h)
read_source("strategy/Value.h" value_h)
read_source("strategy/NamedObjectContext.h" named_h)

# Thread cache in front of the lock (OB-30 X4a acceptance: lock_shared on one mutex cost A800 ~+11 %):
# a hit returns without the lock; only found values are cached; the cache is bounded.
function_body("${shared_h}" "virtual UntypedValue* GetUntypedValue(const std::string& name)" front_body)
require_order("${front_body}" "thread_local std::unordered_map<std::string, UntypedValue*> threadCache;" "UntypedValue* value = LockedLookup(name);" "thread cache before the lock")
require_order("${front_body}" "if (cached != threadCache.end())\n                return cached->second;" "UntypedValue* value = LockedLookup(name);" "hit returns without the lock")
require_order("${front_body}" "if (threadCache.size() >= ThreadCacheLimit)" "threadCache.emplace(name, value);" "bounded cache")
string(FIND "${front_body}" "valuesMutex" front_lock)
if(NOT front_lock EQUAL -1)
  message(FATAL_ERROR "#563 X4a: the front path must not touch the mutex")
endif()

# Pointer stability the thread cache relies on (OB-00 10.10.2026): the shared context never erases, clears,
# resets or evicts a value - SharedObjectContext offers no such method, and the module only ever calls
# sSharedObjectContext.GetValue. The cache holds the heap object T*, not a map node, so a rehash or rebalance
# of the map cannot move it; the objects are freed only when the singleton is destroyed at shutdown.
string(FIND "${shared_h}" "class SharedObjectContext" shared_class_at)
string(SUBSTRING "${shared_h}" ${shared_class_at} -1 shared_class)
string(FIND "${shared_class}" "#define sSharedObjectContext" shared_class_end)
string(SUBSTRING "${shared_class}" 0 ${shared_class_end} shared_class)
foreach(forbidden "Erase" "Clear(" "Reset(" "EraseIf" "->Update(" "valueContexts.Update")
  string(FIND "${shared_class}" "${forbidden}" found_forbidden)
  if(NOT found_forbidden EQUAL -1)
    message(FATAL_ERROR "#563 X4a: SharedObjectContext must not offer or call ${forbidden} (thread cache pointer stability)")
  endif()
endforeach()
require_text("${shared_class}" "protected:\n        NamedObjectContextList<UntypedValue> valueContexts;" "valueContexts not public")
file(GLOB_RECURSE module_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
foreach(source ${module_sources})
  file(READ "${source}" source_text)
  string(REGEX MATCHALL "sSharedObjectContext\\.[A-Za-z_]+" uses "${source_text}")
  foreach(use ${uses})
    if(NOT use STREQUAL "sSharedObjectContext.GetValue")
      message(FATAL_ERROR "#563 X4a: only sSharedObjectContext.GetValue may be used (found ${use} in ${source})")
    endif()
  endforeach()
endforeach()

# Shared context: shared lookup, then unique create; the dummy only on create.
function_body("${shared_h}" "UntypedValue* LockedLookup(const std::string& name)" get_body)
require_order("${get_body}" "std::shared_lock<std::shared_mutex> lock(valuesMutex);" "if (UntypedValue* existing = valueContexts.Find(name))" "shared lookup")
require_order("${get_body}" "std::shared_lock<std::shared_mutex> lock(valuesMutex);" "std::unique_lock<std::shared_mutex> lock(valuesMutex);" "shared before unique")
require_order("${get_body}" "std::unique_lock<std::shared_mutex> lock(valuesMutex);" "PlayerbotAI* ai = new PlayerbotAI();" "dummy only under the unique lock")
require_order("${get_body}" "std::unique_lock<std::shared_mutex> lock(valuesMutex);\n            if (UntypedValue* existing = valueContexts.Find(name))" "valueContexts.GetObject(name, ai);" "double-checked before create")
require_text("${shared_h}" "std::shared_mutex valuesMutex;" "mutex member")

# Find never inserts.
require_text("${named_h}" "T* Find(const std::string& name) const\n        {\n            typename std::map<std::string, T*>::const_iterator const it = created.find(name);" "const Find on one context")
require_text("${named_h}" "if (T* object = (*i)->Find(name))" "Find over the context list")

# SingleCalculatedValue: compute once under the mutex, publish after Calculate.
string(FIND "${value_h}" "template <class T> class SingleCalculatedValue : public CalculatedValue<T>" single_at)
string(SUBSTRING "${value_h}" ${single_at} 2400 single)
require_order("${single}" "if (!ready.load(std::memory_order_acquire))" "std::lock_guard<std::mutex> lock(calcMutex);" "fast path, then lock")
require_order("${single}" "std::lock_guard<std::mutex> lock(calcMutex);" "if (!ready.load(std::memory_order_relaxed))" "double-checked")
require_order("${single}" "this->value = this->Calculate();" "ready.store(true, std::memory_order_release);" "publish after Calculate")
require_order("${single}" "CalculatedValue<T>::Reset();" "ready.store(false, std::memory_order_release);" "Reset clears the flag")

message(STATUS "shared_context_lock source contract passed")
