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
require_text("${grind}" "\"grind avoid \" + std::to_string(unit->GetEntry())" "avoided killers skipped")
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
