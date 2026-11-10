cmake_policy(VERSION 3.16)
# twow-repo#707 (HARM-RACE, OB-00 go 10.11.2026; design note Y:\tw\evidence\ws-00\563p4\design-707-share-quest.md; same class as #697 / #563 part 4):
# Continents are split into regions; every region is its own Map object (same map id, own instance id) updated on its own pool thread. A bot
# that reads the quest map, quest log, AiObjectContext, trade data or group of ANOTHER player/bot from its own thread races with that thread
# when the other one stands on another Map object. ai->IsSafe(player) and GetMapId() compare only map id (and instance id): a neighbour in the
# next region passes them. Every site below tests SameMapObject(bot, other) BEFORE the first read.
#
# Neighbour table (OB-00: each named, changed or deliberately left):
#   CHANGED   AutoShareQuestAction::Execute    (ShareQuestAction.cpp)       member loop: quest map / log / PAI_VALUE / packet post
#   CHANGED   TalkToQuestGiverAction::ProcessQuest                          requester's quest map read (syncs) and CompleteQuest(requester) write
#   CHANGED   AdhocGroupAction::Execute        (AdhocGroupAction.cpp:238)   CLI-GRP side finding of #708: "nearest friendly players" cache, map-id-only filter
#   CHANGED   ListQuestsActions                                              group member loop (quest slots / status), map-id-only filter
#   CHANGED   ChooseTravelTargetAction (quest search, group members loop)    group member quest status map, map-id-only filter
#   CHANGED   RpgSubActions RpgTradeUsefulAction / RpgEnchantAction                 rpg target player: trade data, session handler, GetBotAI(player)->DoSpecificAction
#   LEFT      RpgSubActions RpgDuelAction                                    formatWorldobject(player) + "cast": scalars (guid / name) only
#   LEFT      AdhocGroupWorld.cpp (CurrentObjective, ObjectiveCounter/Done, CandidateFacts, GroupLevels): world-thread managers, safe by
#             construction (tick barrier, twow-repo#708 answered; pinned by t/check_tick_order_708_contract.cmake in the core tree)
#   LEFT      reads of the bot's OWN quest map (TravelMgr.cpp, ItemUsageValue.cpp, ChooseTravelTargetAction quest map of `bot`): own thread
#   OPEN      InviteToGroupAction.cpp:345, 520, 524 (GetMapId() != bot->GetMapId() level/BG filters on invitees): read scalars (level, map id)
#             of the invitee first; not a container read - listed for the follow-up sweep, not changed here
# Negative probes: the same checkers must reject the old (map-id-only / IsSafe-only) texts and a reordered guard.

set(TAG "cross-region share quest (#707)")

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "${TAG}: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(region out text start_marker end_marker)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "${TAG}: cannot find ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# `guard` must be present and come before every `reads` entry that is present; each of `reads` named in `required_reads` must be present.
function(guard_before_reads errors_var text guard required_reads optional_reads description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${guard}" guard_at)
  if(guard_at EQUAL -1)
    list(APPEND errors "${description}: missing guard '${guard}'")
  endif()
  foreach(read ${required_reads} ${optional_reads})
    string(FIND "${text}" "${read}" read_at)
    list(FIND required_reads "${read}" is_required)
    if(read_at EQUAL -1)
      if(NOT is_required EQUAL -1)
        list(APPEND errors "${description}: expected read '${read}' not found (the checker lost the site)")
      endif()
    elseif(NOT guard_at EQUAL -1 AND NOT guard_at LESS read_at)
      list(APPEND errors "${description}: the guard '${guard}' must come before the read '${read}'")
    endif()
  endforeach()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

function(forbid errors_var text needle description)
  set(errors "${${errors_var}}")
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    list(APPEND errors "${description}: must not contain '${needle}'")
  endif()
  set(${errors_var} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the checkers (each returns the list of violations, empty = ok) ----
function(share_loop_violations out fn_text)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "if (!SameMapObject(bot, player))"
    "SatisfyQuestStatus;GetQuestStatus(logQuest);CanTakeQuest;SatisfyQuestLog;GetDividerGuid;PAI_VALUE(;HandleMasterIncomingPacket(" ""
    "AutoShareQuestAction member loop")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(talk_violations out fn_text)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "(requester == bot || SameMapObject(bot, requester))"
    "syncPlayer->GetQuestStatus(;CompleteQuest(syncPlayer" "" "TalkToQuestGiverAction::ProcessQuest sync")
  forbid(errors "${fn_text}" "requester->GetQuestStatus(" "TalkToQuestGiverAction::ProcessQuest")
  forbid(errors "${fn_text}" "CompleteQuest(requester" "TalkToQuestGiverAction::ProcessQuest")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(adhoc_violations out fn_text)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "!SameMapObject(bot, other)" "IsCandidateBot(other);CurrentObjective(other);GetBotAI(other)" "" "AdhocGroupAction neighbour loop")
  forbid(errors "${fn_text}" "other->GetMapId() != bot->GetMapId()" "AdhocGroupAction neighbour loop (map id alone is not the Map object)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(member_loop_violations out fn_text what read)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "if (!SameMapObject(bot, player))" "${read}" "" "${what}")
  forbid(errors "${fn_text}" "if (player->GetMapId() != bot->GetMapId())" "${what} (map id alone is not the Map object)")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

function(rpg_violations out fn_text what read)
  set(errors "")
  guard_before_reads(errors "${fn_text}" "if (!SameMapObject(bot, player))" "${read}" "" "${what}")
  set(${out} "${errors}" PARENT_SCOPE)
endfunction()

# ---- the real sources ----
read_source("strategy/actions/ShareQuestAction.cpp" share)
region(share_fn "${share}" "bool AutoShareQuestAction::Execute(Event& event)" "bool AutoShareQuestAction::isUseful()")
share_loop_violations(share_errors "${share_fn}")

read_source("strategy/actions/TalkToQuestGiverAction.cpp" talk)
region(talk_fn "${talk}" "bool TalkToQuestGiverAction::ProcessQuest(" "    switch (status)")
talk_violations(talk_errors "${talk_fn}")

read_source("strategy/actions/AdhocGroupAction.cpp" adhoc)
region(adhoc_fn "${adhoc}" "for (ObjectGuid const& guid : AI_VALUE(std::list<ObjectGuid>, \"nearest friendly players\"))" "adhoc_group::LevelWindowOk(")
adhoc_violations(adhoc_errors "${adhoc_fn}")

read_source("strategy/actions/ListQuestsActions.cpp" list_quests)
region(list_fn "${list_quests}" "for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, \"group members\"))" "bool isCompletedQuest")
member_loop_violations(list_errors "${list_fn}" "ListQuestsActions group member loop" "GetQuestSlotQuestId(")

read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)
region(choose_fn "${choose}" "for (ObjectGuid guid : AI_VALUE(std::list<ObjectGuid>, \"group members\"))" "QuestStatusMap& questMap = player->getQuestStatusMap();")
member_loop_violations(choose_errors "${choose_fn}" "ChooseTravelTargetAction quest search group member loop" "GetBotAI(player)")

read_source("strategy/actions/RpgSubActions.cpp" rpg)
region(give_fn "${rpg}" "bool RpgTradeUsefulAction::Execute" "bool RpgEnchantAction::Execute")
rpg_violations(give_errors "${give_fn}" "RpgTradeUsefulAction rpg target" "GetSession()->HandleInitiateTradeOpcode")
region(enchant_fn "${rpg}" "bool RpgEnchantAction::Execute" "bool RpgDuelAction::Execute")
rpg_violations(enchant_errors "${enchant_fn}" "RpgEnchantAction rpg target" "GetTradeData()")

foreach(set_name share talk adhoc list choose give enchant)
  if(${set_name}_errors)
    string(REPLACE ";" "\n  - " listed "${${set_name}_errors}")
    message(FATAL_ERROR "${TAG}: ${set_name}:\n  - ${listed}")
  endif()
endforeach()

# ---- negative probes ----
function(expect_rejected name errs)
  if(NOT errs)
    message(FATAL_ERROR "${TAG}: negative probe '${name}' was accepted - the checker no longer sees it")
  endif()
endfunction()

# 1. a member in the NEIGHBOURING region: the loop guarded only by IsSafe (the old text: no Map object test)
string(REPLACE "            if (!SameMapObject(bot, player))\n                continue;\n" "" probe_share_old "${share_fn}")
if("${probe_share_old}" STREQUAL "${share_fn}")
  message(FATAL_ERROR "${TAG}: probe 1 did not change the text (layout moved?)")
endif()
share_loop_violations(e1 "${probe_share_old}")
expect_rejected("share loop guarded by IsSafe only (member in the neighbouring region)" "${e1}")

# 2. the guard moved behind the first read of the member
string(REPLACE "            if (!SameMapObject(bot, player))\n                continue;\n" "" probe_tmp2 "${share_fn}")
string(REPLACE "            if (!player->SatisfyQuestStatus(quest, false))" "            if (!player->SatisfyQuestStatus(quest, false))\n                continue;\n            if (!SameMapObject(bot, player))\n                continue;\n            if (false)" probe_share_late "${probe_tmp2}")
share_loop_violations(e2 "${probe_share_late}")
expect_rejected("guard behind SatisfyQuestStatus" "${e2}")

# 3. TalkToQuestGiverAction: the old unguarded sync on the raw requester
string(REPLACE "syncPlayer" "requester" probe_talk_old "${talk_fn}")
talk_violations(e3 "${probe_talk_old}")
expect_rejected("ProcessQuest syncs the raw requester" "${e3}")

# 4. AdhocGroupAction: the old map-id-only filter
string(REPLACE "!SameMapObject(bot, other) || other->GetTeam() != bot->GetTeam()" "other->GetTeam() != bot->GetTeam() || other->GetMapId() != bot->GetMapId()" probe_adhoc_old "${adhoc_fn}")
if("${probe_adhoc_old}" STREQUAL "${adhoc_fn}")
  message(FATAL_ERROR "${TAG}: probe 4 did not change the text (layout moved?)")
endif()
adhoc_violations(e4 "${probe_adhoc_old}")
expect_rejected("adhoc neighbour filter on map id only" "${e4}")

# 5. the group member loops: the old map-id-only filter
string(REPLACE "if (!SameMapObject(bot, player))" "if (player->GetMapId() != bot->GetMapId())" probe_list_old "${list_fn}")
member_loop_violations(e5 "${probe_list_old}" "ListQuestsActions group member loop" "GetQuestSlotQuestId(")
expect_rejected("ListQuests group member loop on map id only" "${e5}")
string(REPLACE "if (!SameMapObject(bot, player))" "if (player->GetMapId() != bot->GetMapId())" probe_choose_old "${choose_fn}")
member_loop_violations(e6 "${probe_choose_old}" "ChooseTravelTargetAction quest search group member loop" "GetBotAI(player)")
expect_rejected("ChooseTravelTarget group member loop on map id only" "${e6}")

# 6. the rpg actions without the Map object test
string(REPLACE "if (!SameMapObject(bot, player))" "if (false)" probe_enchant_old "${enchant_fn}")
rpg_violations(e7 "${probe_enchant_old}" "RpgEnchantAction rpg target" "GetTradeData()")
expect_rejected("RpgEnchantAction without the Map object test" "${e7}")
string(REPLACE "if (!SameMapObject(bot, player))" "if (false)" probe_give_old "${give_fn}")
rpg_violations(e8 "${probe_give_old}" "RpgTradeUsefulAction rpg target" "GetSession()->HandleInitiateTradeOpcode")
expect_rejected("RpgTradeUsefulAction without the Map object test" "${e8}")

message(STATUS "${TAG} contract passed")
