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

# Shared context: shared lookup, then unique create; the dummy only on create.
function_body("${shared_h}" "virtual UntypedValue* GetUntypedValue(const std::string& name)" get_body)
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
