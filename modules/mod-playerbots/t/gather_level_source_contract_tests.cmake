function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/TravelMgr.cpp" travel_mgr)
file(READ "${PB_SOURCE_DIR}/strategy/actions/FishAction.cpp" fish)
file(READ "${PB_SOURCE_DIR}/strategy/actions/ChooseTravelTargetAction.cpp" choose)

# #333 (owner test video): gathering targets open world only, plain bot level,
# parent-zone level for sub-areas without one; the elite/boss bonus does not apply.
require_text("${travel_mgr}" "if ((purposeFlag & gatherPurposes) && !(purposeFlag & ~gatherPurposes))" "gather-only purposes")
require_text("${travel_mgr}" "if (!position.isOverworld())" "no gathering in instances")
require_text("${travel_mgr}" "int32 const gatherAreaLevel = position.getAreaLevelOrParent();" "parent-zone level for gathering")
require_text("${travel_mgr}" "!homebind::IsZoneClearlyAboveLevel(uint32(gatherAreaLevel), info.GetLevel())" "plain bot level, no elite bonus")
require_text("${fish}" "if (!spot.isOverworld())" "no fishing spot in an instance")
# Profession trips are not announced in party or raid chat.
require_text("${choose}" "if (gatherTrip)\n        ai->TellDebug(requester, out.str(), \"debug travel\");" "gather trips only via debug travel")
