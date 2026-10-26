cmake_policy(VERSION 3.16)

# OB-10 26.10.2026 (use after free): SmartDestroyItemAction, grey-only branch, destroys by item id (every stack of the id).
# The set it loops over keeps one stack per id (DestroyItemPolicy.h FirstOfEachId, unit test destroy_item_policy), so no
# Item* of an already destroyed id is read. No switch: a bug fix.

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "smart destroy one stack per id: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "smart destroy one stack per id: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "smart destroy one stack per id: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("DestroyItemPolicy.h" policy)
read_source("strategy/actions/DestroyItemAction.cpp" cpp)

forbid_text("${policy}" "#include \"playerbot/" "game headers in the pure policy")
require_text("${policy}" "if (seen.insert(ids[i]).second)\n            first.push_back(i);" "the first stack of each id only")

require_text("${cpp}" "#include \"playerbot/DestroyItemPolicy.h\"" "the policy include")
forbid_text("${cpp}" "items.insert(visitor.GetResult().begin(), visitor.GetResult().end());" "every found stack in the grey set")
require_text("${cpp}" "for (std::size_t i : ai::destroy_item::FirstOfEachId(foundIds))\n            items.insert(found[i]);" "one stack per id in the grey set")
require_order("${cpp}" "ai::destroy_item::FirstOfEachId(foundIds)" "        for (auto& item : items)\n        {\n            if (HAS_AI_VALUE2(\"force item usage\", item->GetProto()->ItemId))" "the set is deduplicated before the destroy loop")

message(STATUS "smart destroy one stack per id source contract passed")
