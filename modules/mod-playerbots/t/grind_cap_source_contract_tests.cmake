function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/values/GrindTargetValue.cpp" grind)
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_source)
file(READ "${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_source)
file(READ "${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" config_template)

# #307: roster bots on their own only; cap replaces the fixed +4.
require_text("${grind}" "IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster()" "roster bots on their own")
require_text("${grind}" "(int)unit->GetLevel() - (int)bot->GetLevel() > maxLevelsAbove" "configurable level cap")
require_text("${grind}" "grind_cap::Avoids().IsAvoided(bot->GetGUIDLow(), unit->GetEntry()" "avoided killers skipped (bounded store)")
require_text("${ai_source}" "RecordGrindDeath(this, bot, AI_VALUE(Unit*, \"current target\"))" "deaths recorded per killer entry")
require_text("${ai_source}" "[GrindCap] state=avoid" "visible avoidance")
require_text("${config_source}" "\"AiPlayerbot.GrindCap.LowLevelBelow\", 0" "cap off by default")
require_text("${config_source}" "\"AiPlayerbot.GrindAvoid.MaxDeaths\", 0" "avoidance off by default")
require_text("${config_template}" "AiPlayerbot.GrindCap.LowLevelBelow = 0" "documented cap")
require_text("${config_template}" "AiPlayerbot.GrindAvoid.MaxDeaths = 0" "documented avoidance")

# #307: deaths are only attributed to a target the bot is still travelling to or working at.
file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
require_text("${travel_mgr}" "return m_status == TravelStatus::TRAVEL_STATUS_TRAVEL || m_status == TravelStatus::TRAVEL_STATUS_WORK;" "active-target rule")
require_text("${ai_source}" "travelTarget->IsActiveForDeathAttribution()" "deaths.csv title only for active targets")

# Train 6 tick regression: no per-entry AI context values for grind avoidance.
foreach(forbidden "\"grind avoid \"" "\"grind window \"" "\"grind deaths \"")
  string(FIND "${grind}${ai_source}" "${forbidden}" forbidden_offset)
  if(NOT forbidden_offset EQUAL -1)
    message(FATAL_ERROR "Grind avoidance must not create per-entry context values: ${forbidden}")
  endif()
endforeach()
require_text("${ai_source}" "grind_cap::Avoids().RecordDeath(bot->GetGUIDLow(), killer->GetEntry()" "deaths recorded in the bounded store")

# #351 review: striped plain mutexes, no reader-preferring shared_mutex that
# let busy readers starve death records in the stress test.
file(READ "${PB_SOURCE_DIR}/GrindCapPolicy.h" policy)
require_text("${policy}" "std::array<Stripe, Stripes> stripes;" "striped store")
require_text("${policy}" "std::lock_guard<std::mutex> lock(stripe.mutex);" "plain mutex per stripe")
foreach(forbidden "std::shared_lock" "std::shared_mutex mutex")
  string(FIND "${policy}" "${forbidden}" shared_offset)
  if(NOT shared_offset EQUAL -1)
    message(FATAL_ERROR "AvoidStore must not use a reader-preferring shared_mutex: ${forbidden}")
  endif()
endforeach()
