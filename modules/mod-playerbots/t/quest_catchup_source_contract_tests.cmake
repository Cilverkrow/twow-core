if(NOT DEFINED PB_MODULE_DIR OR NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "PB_MODULE_DIR and TW_CORE_ROOT are required")
endif()

file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/ShareQuestAction.cpp" action)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/ShareQuestAction.h" action_header)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/ChatActionContext.h" action_context)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.cpp" player)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Player.h" player_header)

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

function(function_region text signature next_signature output)
  string(FIND "${text}" "${signature}" begin)
  if(begin EQUAL -1)
    message(FATAL_ERROR "Missing function ${signature}")
  endif()
  string(SUBSTRING "${text}" ${begin} -1 tail)
  string(FIND "${tail}" "${next_signature}" end)
  if(end EQUAL -1)
    set(${output} "${tail}" PARENT_SCOPE)
  else()
    string(SUBSTRING "${tail}" 0 ${end} region)
    set(${output} "${region}" PARENT_SCOPE)
  endif()
endfunction()

require_text("${action_header}" "class CatchupQuestAction" "catch-up action declaration")
require_text("${action_context}" "creators[\"catchup quest\"]" "chat command registration")
require_text("${player_header}" "CanTakeQuestForCatchup" "narrow Core validator declaration")

function_region("${player}" "bool Player::CanTakeQuestForCatchup" "bool Player::SatisfyQuestChallenges" catchup_validator)
foreach(required
    "SatisfyQuestStatus(pQuest, msg)"
    "SatisfyQuestExclusiveGroup(pQuest, msg)"
    "SatisfyQuestClass(pQuest, msg)"
    "SatisfyQuestRace(pQuest, msg)"
    "SatisfyQuestSkill(pQuest, msg)"
    "SatisfyQuestCondition(pQuest, msg)"
    "SatisfyQuestReputation(pQuest, msg)"
    "SatisfyQuestNegativePreviousQuest(pQuest, msg)"
    "SatisfyQuestTimed(pQuest, msg)"
    "SatisfyQuestNextChain(pQuest, msg)"
    "SatisfyQuestPrevChain(pQuest, msg)"
    "pQuest->IsActive()"
    "SatisfyQuestChallenges(pQuest, msg)")
  require_text("${catchup_validator}" "${required}" "preserved Core gate ${required}")
endforeach()
forbid_text("${catchup_validator}" "SatisfyQuestLevel" "minimum-level gate in catch-up validator")
forbid_text("${catchup_validator}" "SatisfyQuestPreviousQuest" "positive-predecessor gate in catch-up validator")

function_region("${player}" "bool Player::SatisfyQuestNegativePreviousQuest" "bool Player::SatisfyQuestClass" negative_validator)
foreach(required
    "bool hasNegativePreviousQuest = false;"
    "if (prevQuest >= 0)"
    "hasNegativePreviousQuest = true;"
    "if (!prev || !IsCurrentQuest(prevId))"
    "if (prev->GetExclusiveGroup() >= 0)"
    "ExclusiveQuestGroupsMapBounds"
    "if (hasNegativePreviousQuest && msg)"
    "return !hasNegativePreviousQuest;")
  require_text("${negative_validator}" "${required}" "negative predecessor contract ${required}")
endforeach()

function_region("${action}" "bool CatchupQuestAction::Execute" "bool AutoShareQuestAction::Execute" catchup_action)
foreach(required
    "requester != master"
    "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow())"
    "master->IsCurrentQuest(questId)"
    "master->CanShareQuest(questId)"
    "group != master->GetGroup()"
    "group->IsRaidGroup() && !quest->HasQuestFlag(QUEST_FLAGS_RAID)"
    "!bot->IsAtGroupRewardDistance(master)"
    "master->GetLevel() < bot->GetLevel()"
    "master->GetLevel() - bot->GetLevel() > 8"
    "bot->GetQuestStatus(questId) != QUEST_STATUS_NONE"
    "bot->CanTakeQuestForCatchup(quest, false)"
    "bot->CanAddQuest(quest, false)"
    "bot->AddQuest(quest, nullptr)")
  require_text("${catchup_action}" "${required}" "catch-up action gate ${required}")
endforeach()
foreach(forbidden
    "CompleteQuest("
    "RewardQuest("
    "SetQuestStatus("
    "CharacterDatabase"
    "LoginDatabase"
    "SatisfyQuestPreviousQuest")
  forbid_text("${catchup_action}" "${forbidden}" "catch-up action side effect ${forbidden}")
endforeach()

function_region("${player}" "bool Player::CanAddQuest" "bool Player::CanCompleteQuest" can_add)
require_text("${can_add}" "SatisfyQuestLog(msg)" "quest-log capacity check")
require_text("${can_add}" "CanGiveQuestSourceItemIfNeed(pQuest)" "source-item capacity check")


# #340: the command must be reachable from chat. core#102 registered only the
# action; without a trigger and a `supported` entry the text never fired.
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/triggers/ChatTriggerContext.h" trigger_context)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/generic/ChatCommandHandlerStrategy.cpp" chat_strategy)
file(READ "${PB_MODULE_DIR}/src/playerbot/strategy/actions/AcceptQuestAction.cpp" accept_action)
require_text("${trigger_context}" "ChatCommandTrigger(ai, \"catchup quest\")" "catch-up chat trigger")
require_text("${chat_strategy}" "supported.push_back(\"catchup quest\");" "catch-up accepted as a command")

# #340: the normal share button uses the same bounded admission, only when Core
# refused the member (no divider), only for a roster bot of the sharing master.
function_region("${accept_action}" "bool AcceptQuestShareAction::Execute" "bool ConfirmQuestAction::Execute" share_accept)
string(FIND "${share_accept}" "if (!bot->GetDividerGuid())" no_divider)
string(FIND "${share_accept}" "CatchupQuestAction::Admit(bot, master, requester, qInfo->GetQuestId())" gui_admit)
if(no_divider EQUAL -1 OR gui_admit EQUAL -1 OR NOT no_divider LESS gui_admit)
  message(FATAL_ERROR "share button catch-up must run only in the refused (no divider) branch")
endif()
require_text("${share_accept}" "IsPersistentRosterMember(bot->GetGUIDLow())" "share button catch-up only for roster bots")
require_text("${share_accept}" "requester != master" "share button catch-up only from the master")
require_text("${share_accept}" "[QuestShare] path=gui" "share button diagnostics")
require_text("${action}" "[QuestShare] path=chat" "chat diagnostics")
function_region("${share_accept}" "if (!bot->GetDividerGuid())" "quest = qInfo->GetQuestId();" gui_branch)
foreach(forbidden "CompleteQuest(" "RewardQuest(" "SetQuestStatus(")
  forbid_text("${gui_branch}" "${forbidden}" "share button catch-up side effect ${forbidden}")
endforeach()

message(STATUS "QUEST_CATCHUP_SOURCE_CONTRACT=PASS")
