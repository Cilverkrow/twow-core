function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AcceptQuestAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/DropQuestAction.cpp" drop)

# Train 8b: roster bots on their own skip red quests, with the red rule of the quest log cleanup.
require_text("${accept}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();" "roster bots on their own only")
require_text("${accept}" "ai::quest_accept::SkipForRosterBot(rosterOnItsOwn, bot->GetLevel(), bot->GetQuestLevelForPlayer(quest)," "one accept rule")
require_text("${accept}" "MaNGOS::XP::GetGrayLevel(bot->GetLevel())" "grey by the XP grey level, as the drop rule")
# Hotfix 8.11: all three accept paths (accept all, accept by packet, quest-giver dialog).
string(REGEX MATCHALL "if \\(SkipQuestForRosterBot\\(ai, bot, (quest|qInfo)\\)\\)" uses "${accept}")
list(LENGTH uses use_count)
if (NOT use_count EQUAL 3)
  message(FATAL_ERROR "all three accept paths must use the accept rule (found ${use_count})")
endif()
require_text("${drop}" "uint32 const grayLevel = MaNGOS::XP::GetGrayLevel(bot->GetLevel());" "drop rule the accept filter mirrors")
require_text("${drop}" "if (bot->GetLevel() + 5 > bot->GetQuestLevelForPlayer(quest)) //Quest is not red" "cleanup red rule the accept filter mirrors")
