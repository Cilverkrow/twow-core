function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/values/TravelValues.cpp" travel)
file(READ "${PB_SOURCE_DIR}/strategy/actions/DropQuestAction.cpp" drop)

# Hotfix 8.5 (twow-repo#329): a gathering purpose never beats a turn-in of a roster bot
# on its own, and open grey quests no longer make grey creatures "needed for quest".
require_text("${travel}" "if (ai::quest_search::GatherYieldsToTurnIn(rosterOnItsOwn, finished))" "gathering yields to the turn-in")
string(FIND "${travel}" "if (ai::quest_search::GatherYieldsToTurnIn(rosterOnItsOwn, finished))" yield_at)
string(FIND "${travel}" "skill = gatheringSkills.at(purpose);" skill_at)
if (yield_at GREATER skill_at)
  message(FATAL_ERROR "the turn-in check must come before the gathering skill check")
endif()
require_text("${drop}" "ai::quest_search::DropGreyQuest(true, bot->GetLevel(), bot->GetQuestLevelForPlayer(quest), lowLevelDiff," "grey quest rule")
require_text("${drop}" "    DropGreyQuestsOnItsOwn(ai, bot);" "grey drop runs in CleanQuestLogAction")
# Declared profession purpose: start in TravelValues, observe and end in the progress watch,
# skill-ups count only while it runs (the 8.2 loop over all professions is gone).
file(READ "${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
require_text("${travel}" "switch (ai::gather_purpose::Decide(declared, rosterOnItsOwn, skill, value, target, now))" "declared purpose decides gathering")
require_text("${travel}" "[Purpose] state=start" "[Purpose] start line")
require_text("${ai_cpp}" "ai::gather_purpose::End const end = gatherPurpose.Observe(value, now);" "purpose observed in the progress watch")
require_text("${ai_cpp}" "[Purpose] state=end" "[Purpose] end line")
require_text("${ai_cpp}" "travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "gathering target left when the purpose ends")
string(FIND "${ai_cpp}" "SKILL_COOKING, SKILL_FIRST_AID" old_loop)
if (NOT old_loop EQUAL -1)
  message(FATAL_ERROR "undeclared skill-ups must not count as progress")
endif()
message(STATUS "HOTFIX85_CONTRACT=PASS")
