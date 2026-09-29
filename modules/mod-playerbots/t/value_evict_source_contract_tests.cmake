function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/NamedObjectContext.h" noc)
file(READ "${PB_SOURCE_DIR}/strategy/AiObjectContext.cpp" aoc)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/actions/RpgSubActions.cpp" rpg_actions)
file(READ "${PB_SOURCE_DIR}/strategy/triggers/RpgTriggers.cpp" rpg_triggers)

# #416 (7.5): F1 idle eviction (own contexts, calculated values only), F2 prefix
# search in ClearValues, F3 stable "can use item on" target key.
require_text("${noc}" "erased += context->EraseIf(pred);" "eviction per context")
require_text("${noc}" "shared ones are other" "shared contexts untouched (EraseOwnIf)")
require_text("${aoc}" "ai::value_evict::Evictable(value->Protected(), value->Expired(idleSeconds));" "only unprotected expired values")
require_text("${aoc}" "for (std::string const& name : valueContexts.CreatedWithPrefix(findName))" "prefix search in ClearValues")
require_text("${ai_cpp}" "aiObjectContext->ClearIdleOwnValues(ai::value_evict::EvictIdleSeconds)" "eviction on the bot thread")
require_text("${rpg_actions}" "ai::value_evict::StableTargetQualifier(guidP.getMapId(), guidP.GetRawValue())" "stable key (action)")
require_text("${rpg_triggers}" "ai::value_evict::StableTargetQualifier(guidP.getMapId(), guidP.GetRawValue())" "stable key (trigger)")
