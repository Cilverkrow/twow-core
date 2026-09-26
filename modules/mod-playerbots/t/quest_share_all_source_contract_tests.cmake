if(NOT DEFINED PB_MODULE_DIR OR NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "PB_MODULE_DIR and TW_CORE_ROOT are required")
endif()

# twow-repo#290 (owner 2026-09-26): every quest can be shared, for all
# players, behind Funserver.Quests.AllSharable; a roster bot admitted by the
# catch-up gets no "not eligible" line first.
file(READ "${TW_CORE_ROOT}/src/game/World.cpp" world)
file(READ "${TW_CORE_ROOT}/src/game/ObjectMgr.cpp" objmgr)
file(READ "${TW_CORE_ROOT}/src/game/ScriptObjects.h" hooks)
file(READ "${TW_CORE_ROOT}/src/game/Handlers/QuestHandler.cpp" questhandler)
file(READ "${PB_MODULE_DIR}/src/playerbot/PlayerbotScripts.cpp" scripts)

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "Missing ${description}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "Forbidden ${description}")
  endif()
endfunction()

function(region text signature next_signature output)
  string(FIND "${text}" "${signature}" begin)
  if(begin EQUAL -1)
    message(FATAL_ERROR "Missing ${signature}")
  endif()
  string(SUBSTRING "${text}" ${begin} -1 tail)
  string(FIND "${tail}" "${next_signature}" end)
  if(end EQUAL -1)
    set(${output} "${tail}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${tail}" 0 ${end} r)
    set(${output} "${r}" PARENT_SCOPE)
  endif()
endfunction()

# 1. Opt-in key, vanilla by default; applied to the templates at load.
require_text("${world}" "\"Funserver.Quests.AllSharable\", false)" "opt-in key, default off")
region("${objmgr}" "void ObjectMgr::LoadQuests()" "\nvoid ObjectMgr::" load)
require_text("${load}" "if (sWorld.getConfig(CONFIG_BOOL_FUNSERVER_ALL_QUESTS_SHARABLE))" "flag only behind the key")
require_text("${load}" "quest->m_QuestFlags |= QUEST_FLAGS_SHARABLE;" "template made sharable")

# 2. Recipient checks unchanged: the push handler still runs CanTakeQuest,
#    and the hook sits only in its refusal branch.
region("${questhandler}" "void WorldSession::HandlePushQuestToParty" "\nvoid WorldSession::" push)
string(FIND "${push}" "if (!pPlayer->CanTakeQuest(pQuest, false))" can_take)
string(FIND "${push}" "OnQuestShareRefused(_player, pPlayer, pQuest)" hook)
string(FIND "${push}" "if (!admitted)" not_admitted)
string(FIND "${push}" "QUEST_PARTY_MSG_CANT_TAKE_QUEST" cant_take_msg)
if(can_take EQUAL -1 OR hook EQUAL -1 OR not_admitted EQUAL -1 OR cant_take_msg EQUAL -1 OR
   NOT can_take LESS hook OR NOT hook LESS not_admitted OR NOT not_admitted LESS cant_take_msg)
  message(FATAL_ERROR "the hook must run in the CanTakeQuest refusal branch before the not-eligible message")
endif()
require_text("${push}" "if (!pPlayer->SatisfyQuestLog(false))" "log-full check kept")
require_text("${hooks}" "PLAYERHOOK_ON_QUEST_SHARE_REFUSED," "hook registered")
require_text("${hooks}" "virtual bool OnQuestShareRefused(Player* /*sharer*/, Player* /*member*/, Quest const* /*quest*/) { return false; }" "hook defaults to the core behaviour")

# 3. The module admits only through the core#102 catch-up and answers "accepted".
region("${scripts}" "bool OnQuestShareRefused(Player* sharer, Player* member, Quest const* quest) override" "\n        // " module)
require_text("${module}" "ai->GetMaster() != sharer" "only the sharer's own bots")
require_text("${module}" "IsPersistentRosterMember(member->GetGUIDLow())" "only roster bots")
require_text("${module}" "ai::CatchupQuestAction::Admit(member, sharer, sharer, quest->GetQuestId())" "the bounded catch-up")
require_text("${module}" "QUEST_PARTY_MSG_ACCEPT_QUEST" "accepted answer")
foreach(forbidden "CompleteQuest(" "RewardQuest(" "SetQuestStatus(" "AddQuest(")
  forbid_text("${module}" "${forbidden}" "hook side effect ${forbidden} outside Admit")
endforeach()

message(STATUS "QUEST_SHARE_ALL_SOURCE_CONTRACT=PASS")
