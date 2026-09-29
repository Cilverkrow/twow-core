function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)
file(READ "${PB_SOURCE_DIR}/strategy/values/GrindTargetValue.cpp" grind)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
file(READ "${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" mgr)

# #421 A: rejection reasons; B: widening search; C: rescue teleport; grey targets.
require_text("${choose}" "\"no_active_route_candidate\", lastRejects.Format());" "rejection reasons logged")
require_text("${choose}" "ai::quest_search::GiverRadius(bot->GetLevel(), searchStage)" "widening giver radius")
require_text("${choose}" "SortQuestHubsFirst(list);" "hubs first, in the destination job")
require_text("${grind}" "ai::quest_search::SkipGreyTarget(rosterOnItsOwn, true, false, creature->GetVictim() == bot)" "grey targets skipped")
require_text("${ai_cpp}" "sPlayerbotAIConfig.questRescueIdleMinutes * 60))" "rescue after the configured idle time")
require_text("${mgr}" "questRescueLimiter.TryAcquire(now)" "global rescue limit")
require_text("${mgr}" "ai::quest_search::UsesRescueAnchors(race, level)" "goblin / high elf 11-20 hubs")

# #211: a rescue never waits for a running destination job - Reset(true)
# clears FutureDestinations, which parks a running job (TravelValues).
require_text("${mgr}" "ai->Reset(true);" "full reset after the rescue teleport")
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_reset)
require_text("${ai_reset}" "*AI_VALUE(FutureDestinations*, \"future travel destinations\") = FutureDestinations();" "Reset(full) clears the destination job")
file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.h" travel_values)
require_text("${travel_values}" "FutureDestinations& operator=(FutureDestinations&& other) { if (this != &other) { Park(); future = std::move(other.future); } return *this; }" "clearing parks a running job")
