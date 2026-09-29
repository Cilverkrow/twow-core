function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/values/GrindTargetValue.cpp" grind)

# #422: death series -> cautious mode; environmental gathering deaths -> purpose suppressed.
require_text("${ai_cpp}" "RecordDeathForSeries();" "death series recorded at death")
require_text("${ai_cpp}" "gatherDeaths.RecordEnvironmentalDeath(purpose, now)" "environmental gathering deaths")
require_text("${choose}" "ai->IsGatherPurposeSuppressed(uint32(destination->GetPurpose()) & gatherPurposes)" "suppressed purpose skipped")
require_text("${choose}" "ai::death_series::RouteMargin(ai->IsCautious())" "stricter zone level while cautious")
require_text("${grind}" "ai::death_series::GrindMargin(rosterOnItsOwn && ai->IsCautious()," "no grinding above the level while cautious")
