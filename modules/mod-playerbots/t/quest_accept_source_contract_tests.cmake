function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}: ${needle}")
  endif()
endfunction()

file(READ "${PB_SOURCE_DIR}/strategy/actions/AcceptQuestAction.cpp" accept)
file(READ "${PB_SOURCE_DIR}/strategy/actions/DropQuestAction.cpp" drop)

# Train 8b: roster bots on their own skip red quests, with the red rule of the quest log cleanup.
require_text("${accept}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster() &&" "roster bots on their own only")
require_text("${accept}" "ai::quest_accept::IsRed(bot->GetLevel(), bot->GetQuestLevelForPlayer(quest))" "red quests skipped")
require_text("${drop}" "if (bot->GetLevel() + 5 > bot->GetQuestLevelForPlayer(quest)) //Quest is not red" "cleanup red rule the accept filter mirrors")
