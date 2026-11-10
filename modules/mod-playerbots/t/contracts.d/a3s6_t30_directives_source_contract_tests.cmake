cmake_policy(VERSION 3.16)
# A3 step 6 T30-1 "Direktiven ADHOC/QUEST" (design ws-10/cli-grp/a3-schritt6/DESIGN-T30-MANAGER-DIREKTIVEN-20261109.md v1.1,
# owner decisions F1-F16 = (a); interface A3-S3-T30-SCHNITTSTELLE.md R10). Everything new sits behind
# AiPlayerbot.BotGroups.Manager.Types (default 0) and BotGroups.Manager. This contract pins:
#   (a) the switch: default 0 in the member, the loader (read straight into the member, as config_echo wants; the sign check
#       derived from it), the dist file and the echo (sorted); negative and reserved bits masked; Types acts as 0 without
#       StatusSeconds (F9 (a));
#   (b) Types 0 is a no-op: the directive pass returns before any work, the call site runs only with Manager or held groups,
#       the read view answers from one atomic without a lock while it is empty, Blocks = OwnsBot || IsManaged, the minute
#       line only with a type on or a held group, RunJoin's manager branch only for manager joins;
#   (c) the one directive post: exactly one ai::supervisor::Post( in the module, in GroupManager.cpp, behind the R10 gate
#       (supervisor on and Applies), a false return counted as dir_dropped / directive_dropped; PostDirectiveAck has no caller;
#   (d) the manager's ops: QueueManagerJoin / QueueManagerLeave / TakeManagerOutcomes called only from GroupManager.cpp; the
#       manager reads map keys only (no Player* dereference); the engine header and the view header are pure;
#   (e) the gates G1-G12 (design 4; G9 = no gate by F15 (a)) in their places, with a census of every Blocks / IsManaged call;
#   (f) the S7 mirrors (design 3.6) at every listed place;
#   (g) the pass order (design 2.2, M15/M16) and the op budget;
#   (h) the [GroupMgr] minute lines with fixed fields and the line cap, and the 5.2 formed / gap event fields;
#   (h2) the cohort filter (Owns) in front of the candidates and the foreign leaves;
#   (h3) after a resume the inbox pass drains the whole ring (bounded by its capacity) and stores none of it (OB-10 10.11.);
#   (h4) the directive seq and the group serial: process-wide monotonic, never reset (only NextSeq / NextSerial move them,
#       the engine is never re-made), the seq never 0, the serial's low 16 bits never 0 nor those of a group still held; an
#       ack matches only its own directive, any other is ack_stale and nothing else;
#   (h5) the T02-GROUP read IsManaged(guid, serial): a live member under that wire serial, the atomic fast path, no log, no
#       allocation, the lock only for the lookup (both IsManaged overloads); no caller in this pair; kOrphanGraceMs;
#   (i) the unit tests registered and run.
# Pure rules: t/group_directive_policy_tests.cpp, t/group_manager_policy_tests.cpp, t/adhoc_group_policy_tests.cpp. Every
# check_* reads the source texts from the variables below and returns "" when it holds, else the reason; the negative probes
# at the end mutate one variable, rerun the check and restore it.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "a3s6 t30 directives: PB_SOURCE_DIR is required")
endif()
if(NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "a3s6 t30 directives: PB_MODULE_DIR is required")
endif()

set(TAG "a3s6 t30 directives")

function(read_file path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${TAG}: missing file ${path}")
  endif()
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

# A probe must be caught by the check it aims at, not by a needle it broke on the way.
function(expect_reason what reason pattern)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
  if(NOT "${reason}" MATCHES "${pattern}")
    message(FATAL_ERROR "${TAG}: negative probe ${what} caught by another check: ${reason}")
  endif()
endfunction()

function(count_text text needle out)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" nl)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${nl}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
  endwhile()
  set(${out} ${n} PARENT_SCOPE)
endfunction()

# The text from the first `start` up to and including the first `stop` after it ("" when start or stop is missing).
function(region text start stop out)
  set(${out} "" PARENT_SCOPE)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(FIND "${rest}" "${stop}" end)
  if(end EQUAL -1)
    return()
  endif()
  string(LENGTH "${stop}" sl)
  math(EXPR end "${end} + ${sl}")
  string(SUBSTRING "${rest}" 0 ${end} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

# Code without // comments (the comments name what the code must not do).
function(strip_comments text out)
  string(REGEX REPLACE "//[^\n]*" "" code "${text}")
  set(${out} "${code}" PARENT_SCOPE)
endfunction()

set(SRC "${PB_SOURCE_DIR}")
set(ACT "${PB_SOURCE_DIR}/strategy/actions")
read_file("${SRC}/PlayerbotAIConfig.h" config_h)
read_file("${SRC}/PlayerbotAIConfig.cpp" config_cpp)
read_file("${SRC}/aiplayerbot.conf.dist.in" conf_dist)
read_file("${SRC}/GroupManagerPolicy.h" gm_policy)
read_file("${SRC}/GroupDirectivePolicy.h" dpol)
read_file("${SRC}/GroupManagerView.h" view_h)
read_file("${SRC}/GroupManager.h" mgr_h)
read_file("${SRC}/GroupManager.cpp" mgr_cpp)
read_file("${SRC}/GroupOps.cpp" ops_cpp)
read_file("${SRC}/RandomPlayerbotMgr.cpp" rpm)
read_file("${SRC}/PlayerbotAI.cpp" ai_cpp)
read_file("${SRC}/AdhocGroupPolicy.h" adhoc_policy)
read_file("${SRC}/AdhocGroupWorld.cpp" adhoc_world)
read_file("${SRC}/QuestGroupWorld.cpp" s7_world)
read_file("${SRC}/QuestBand.cpp" questband)
read_file("${SRC}/TravelMgr.cpp" travel)
read_file("${SRC}/strategy/triggers/StuckTriggers.h" stuck)
read_file("${ACT}/AdhocGroupAction.cpp" adhoc_action)
read_file("${ACT}/LeaveGroupAction.cpp" leave)
read_file("${ACT}/InviteToGroupAction.cpp" invite)
read_file("${ACT}/AcceptInvitationAction.h" accept)
read_file("${ACT}/BattleGroundJoinAction.cpp" bgjoin)
read_file("${ACT}/ReleaseSpiritAction.h" repop)
read_file("${ACT}/ChooseTravelTargetAction.cpp" choose_travel)
read_file("${ACT}/ShareQuestAction.cpp" share)
read_file("${PB_MODULE_DIR}/tests.cmake" tests_cmake)
read_file("${PB_MODULE_DIR}/t/group_directive_policy_tests.cpp" unit_test)
read_file("${PB_MODULE_DIR}/t/group_manager_policy_tests.cpp" gm_test)
read_file("${PB_MODULE_DIR}/t/adhoc_group_policy_tests.cpp" adhoc_test)

file(GLOB_RECURSE all_files "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(SORT all_files)
set(all_sources "")
set(other_sources "")   # every file but GroupManager.cpp, GroupOps.{h,cpp}
foreach(f IN LISTS all_files)
  file(READ "${f}" t)
  string(REPLACE "\r" "" t "${t}")
  string(APPEND all_sources "${t}\n")
  get_filename_component(fname "${f}" NAME)
  if(NOT fname MATCHES "^(GroupManager\\.cpp|GroupOps\\.h|GroupOps\\.cpp)$")
    string(APPEND other_sources "${t}\n")
  endif()
endforeach()
set(extra "")   # probes add a use here

# ---- (a) the switch ---------------------------------------------------------------------------------------------------------
function(check_switch out)
  foreach(pair "config_h|    uint32 botGroupsManagerTypes = 0;"
      "config_cpp|    botGroupsManagerTypes = uint32(config.GetIntDefault(\"AiPlayerbot.BotGroups.Manager.Types\", 0));\n    int32 const typesRaw = int32(botGroupsManagerTypes);"
      "config_cpp|        uint32 const masked = typesRaw < 0 ? 0u : ai::group_mgr::SanitizeTypes(botGroupsManagerTypes);"
      "config_cpp|        botGroupsManagerTypes = ai::group_mgr::EffectiveTypes(botGroupsManagerTypes, botGroupsManagerStatusSeconds);"
      "conf_dist|\nAiPlayerbot.BotGroups.Manager.Types = 0\n"
      "gm_policy|    return statusSeconds ? SanitizeTypes(types) : 0u;"
      "gm_policy|    return SanitizeTypes(types) != 0 && Applies(mode, guidLow);"
      "gm_policy|constexpr std::uint32_t kTypesActive = kTypeAdhoc | kTypeQuest;")
    string(FIND "${pair}" "|" bar)
    string(SUBSTRING "${pair}" 0 ${bar} var)
    math(EXPR bar "${bar} + 1")
    string(SUBSTRING "${pair}" ${bar} -1 needle)
    string(FIND "${${var}}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "switch: ${var} missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The echo: one entry, sorted between StatusSeconds and OpQueue.
  string(FIND "${config_cpp}" "            {\"BotGroups.Manager.StatusSeconds\", Value(botGroupsManagerStatusSeconds)},\n            {\"BotGroups.Manager.Types\", Value(botGroupsManagerTypes)},\n            {\"BotGroups.OpQueue\", Value(botGroupsOpQueue)}," at)
  count_text("${config_cpp}" "\"BotGroups.Manager.Types\"" n)
  if(at EQUAL -1 OR NOT n EQUAL 1)
    set(${out} "switch: the echo entry BotGroups.Manager.Types is missing, doubled or out of order" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) Types 0 is a no-op -------------------------------------------------------------------------------------------------
function(check_noop out)
  region("${mgr_cpp}" "void WorldThreadManager::RunDirectives(" "\n}\n" run)
  string(FIND "${run}" "    std::uint32_t const types = EffectiveTypes(sPlayerbotAIConfig.botGroupsManagerTypes, sPlayerbotAIConfig.botGroupsManagerStatusSeconds);\n    if (!types && !engine_.HasGroups())\n    {\n        acks_.clear();\n        return;\n    }\n" early)
  string(FIND "${run}" "group_ops::TakeManagerOutcomes(" take)
  if(early EQUAL -1 OR take EQUAL -1 OR NOT early LESS take)
    set(${out} "no-op: RunDirectives does not return before any work while Types is 0 and nothing is held" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${rpm}" "        if (sPlayerbotAIConfig.botGroupsManager || ai::group_mgr::WorldManager().HasGroups())\n            ai::group_mgr::WorldManager().Update(GetAllBots(), uint32(time(nullptr)));" at)
  if(at EQUAL -1)
    set(${out} "no-op: the call site is not gated on Manager || HasGroups()" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "    bool Any() const { return any_.load(std::memory_order_acquire); }"
      "        if (!Any())\n            return false;\n        std::lock_guard<std::mutex> lock(mutex_);\n        auto const it = bots_.find(low);"
      "        if (!Any() || !coreGroupId)\n            return false;"
      "        bool const any = !bots.empty();"
      "    std::atomic<bool> any_{ false };"
      "        return queueUsed_.load(std::memory_order_acquire) ? questQueue_.QueuedAge(low, questId, nowSec) : 0u;"
      "        if (r.types || HasGroups())")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "no-op: GroupDirectivePolicy.h fast path missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle
      "bool OwnsBot(std::uint32_t guidLow)\n{\n    return Owns(sPlayerbotAIConfig.botGroupsManager,\n        EffectiveTypes(sPlayerbotAIConfig.botGroupsManagerTypes, sPlayerbotAIConfig.botGroupsManagerStatusSeconds), guidLow);\n}"
      "bool IsManaged(std::uint32_t guidLow)\n{\n    ViewEntry v;\n    return WorldManager().Directives().View().Find(guidLow, v);\n}"
      "bool Blocks(std::uint32_t guidLow)\n{\n    return OwnsBot(guidLow) || IsManaged(guidLow);\n}"
      "    return WorldManager().Directives().View().Find(guidLow, v) ? v.questRole : std::uint8_t(0);"
      "    if (!WorldManager().Directives().View().KeyOf(coreGroupId, v))\n        return false;")
    string(FIND "${mgr_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "no-op: GroupManager.cpp read view missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # RunJoin: the manager branch only for a manager join (the ad-hoc action's joins keep reason 0).
  foreach(needle "        bool const questJoin = (op.reason & kJoinManager) && (op.reason & kJoinQuest);"
      "    if (op.reason & kJoinManager)\n    {\n        if (group->GetMembersCount() == 2)")
    string(FIND "${ops_cpp}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "no-op: RunJoin missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) the one directive post (R10) -----------------------------------------------------------------------------------------
set(R10_POST "        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, low))\n            return ai::supervisor::Post(low, event);\n        return false;")
function(check_post out)
  strip_comments("${all_sources}${extra}" code)
  count_text("${code}" "supervisor::Post(" n_all)
  count_text("${mgr_cpp}" "${R10_POST}" n_r10)
  if(NOT n_all EQUAL 1 OR NOT n_r10 EQUAL 1)
    set(${out} "post: supervisor::Post( called ${n_all} times in the module, the R10-gated post ${n_r10} times in GroupManager.cpp (1 and 1 expected)" PARENT_SCOPE)
    return()
  endif()
  # A dropped post fails closed: counted and the member ended with directive_dropped.
  foreach(needle "Cnt::DirDropped" "EndReason::DirectiveDropped" "io.PostDirective(")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "post: GroupDirectivePolicy.h missing ${needle} (fail closed on a dropped post)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # PostDirectiveAck gets no caller in T30-1 (declaration and definition only).
  count_text("${code}" "PostDirectiveAck(" n_ack)
  if(NOT n_ack EQUAL 2)
    set(${out} "post: PostDirectiveAck( appears ${n_ack} times (2 expected: declaration and definition, no caller in T30-1)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (d) the manager's ops and the pure headers ----------------------------------------------------------------------------------
function(check_ops out)
  strip_comments("${other_sources}${extra}" others)
  foreach(fn "QueueManagerJoin(" "QueueManagerLeave(" "TakeManagerOutcomes(")
    count_text("${others}" "${fn}" n_other)
    count_text("${mgr_cpp}" "group_ops::${fn}" n_mgr)
    if(NOT n_other EQUAL 0 OR NOT n_mgr EQUAL 1)
      set(${out} "ops: ${fn} called ${n_other} times outside GroupManager.cpp / GroupOps, ${n_mgr} in GroupManager.cpp (0 and 1 expected)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Map keys only: no Player* is dereferenced, no map value read (m2: the op path looks the bots up itself).
  foreach(bad "->second" ".second" "Player*>::" "bot->" "player->" "->Get" "->Is" "FindPlayer" "GetBotAI(" "ObjectAccessor")
    string(FIND "${mgr_cpp}" "${bad}" at)
    if(NOT at EQUAL -1)
      set(${out} "ops: GroupManager.cpp reads beyond the map keys (${bad})" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_pure out)
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" includes "${dpol}")
  foreach(inc IN LISTS includes)
    if(NOT inc MATCHES "^#include (\"(GroupManagerPolicy|ManagerInboxPolicy|AdhocGroupPolicy|QuestGroupPolicy|SupervisorPolicy|GroupOpPolicy)\\.h\"|<(algorithm|atomic|cstddef|cstdint|cstdio|iterator|mutex|unordered_map|vector)>)$")
      set(${out} "pure: GroupDirectivePolicy.h has a non-pure include: ${inc}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  strip_comments("${dpol}" dcode)
  string(REPLACE "ManagedGroup*" "" dcode "${dcode}")   # the engine's own record, not the game's Group*
  foreach(word "sPlayerbotAIConfig" "sObjectMgr" "sLog" "Player*" "Group*" "PlayerbotAI" "ObjectAccessor" "WorldTimer" "time(")
    string(FIND "${dcode}" "${word}" at)
    if(NOT at EQUAL -1)
      set(${out} "pure: GroupDirectivePolicy.h names ${word}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" vincludes "${view_h}")
  if(NOT "${vincludes}" STREQUAL "#include <cstdint>")
    set(${out} "pure: GroupManagerView.h includes more than <cstdint> (${vincludes})" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (e) the gates G1-G12 ----------------------------------------------------------------------------------------------------
# Each gate: the source variable, the function it sits in (searched after it), the gate text.
set(G1_VAR adhoc_action)
set(G1_AT "bool AdhocGroupAction::isUseful()")
set(G1 "        !group_mgr::Blocks(bot->GetGUIDLow());   // G1 (T30-1)")
set(G2_VAR adhoc_world)
set(G2_AT "bool IsCandidateBot(Player* player)\n{")
set(G2 "    return CandidateFacts(player) && !group_mgr::Blocks(player->GetGUIDLow());")
set(G3_VAR adhoc_world)
set(G3_AT "bool Eligible(Player* bot)\n{")
set(G3 "!bot->InBattleGround() && !bot->InBattleGroundQueue() && !group_mgr::Blocks(bot->GetGUIDLow());   // G3 (T30-1)")
set(G4_VAR adhoc_world)
set(G4_AT "void WorldThreadManager::SweepHelpers(")
set(G4 "        facts.managerOwns = group_mgr::Blocks(it->first);   // G4 (T30-1)\n        HelperExit const exit = DecideHelperExit(facts);")
set(G4P_VAR adhoc_policy)
set(G4P_AT "inline HelperExit DecideHelperExit(")
set(G4P "    if (f.transient || (f.helping && !f.managerOwns))")
set(G5_VAR s7_world)
set(G5_AT "bool Eligible(Player* bot)\n{")
set(G5 "        !group_mgr::Blocks(bot->GetGUIDLow());   // G5 (T30-1)")
set(G6_VAR leave)
set(G6_AT "    bool LeaveFarAwayAction::isUseful()")
set(G6 "        if (group_mgr::Blocks(bot->GetGUIDLow()))\n            return false;")
set(G7_VAR invite)
set(G7_AT "    bool InviteNearbyToGroupAction::isUseful()")
set(G7 "        if (group_mgr::Blocks(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())\n            return false;")
set(G8_VAR accept)
set(G8_AT "            Player* inviter = sObjectMgr.GetPlayer(grp->GetLeaderGuid());")
set(G8 "            if (event.getSource() != group_ai_refresh::GroupOpSource && group_mgr::Blocks(bot->GetGUIDLow()) && !::IsRealPlayer(inviter))\n            {")
# M5: G8 declines through the op queue whatever BotGroups.WritePath says; the direct decline only when the push is refused.
set(G8Q_VAR accept)
set(G8Q_AT "            if (event.getSource() != group_ai_refresh::GroupOpSource && group_mgr::Blocks(bot->GetGUIDLow()) && !::IsRealPlayer(inviter))")
set(G8Q "                if (!group_ai_refresh::IsWorldManagerSource(event.getSource()) && group_ops::QueueAccept(ai, inviter->GetGUIDLow(), true))\n                    return false;\n                WorldPacket data(SMSG_GROUP_DECLINE, 10);")
set(G10_VAR invite)
set(G10_AT "    bool JoinGroupAction::Execute(Event& event)")
set(G10 "        if (master && group_mgr::Blocks(bot->GetGUIDLow()) && GetBotAI(master) && !GetBotAI(master)->IsRealPlayer())\n            return false;")
set(G10L_VAR invite)
set(G10L_AT "    bool LfgAction::Execute(Event& event)")
set(G10L "        if (requester && group_mgr::Blocks(bot->GetGUIDLow()) && GetBotAI(requester) && !GetBotAI(requester)->IsRealPlayer())\n            return false;")
set(G11_VAR leave)
set(G11_AT "    bool LeaveGroupAction::Execute(Event& event)")
set(G11 "        if (master && master != bot && group_mgr::IsManaged(bot->GetGUIDLow()))\n        {\n            PlayerbotAI* const masterAi = GetBotAI(master);\n            if (masterAi && !masterAi->IsRealPlayer())\n                return false;")
set(G12_VAR bgjoin)
set(G12_AT "bool BGJoinAction::gatherArenaTeam(ArenaType type)\n{")
set(G12 "    if (ai::group_mgr::IsManaged(bot->GetGUIDLow()))\n        return false;")
set(G12M_VAR bgjoin)
set(G12M_AT "bool BGJoinAction::gatherArenaTeam(ArenaType type)\n{")
set(G12M "            if (ai::group_mgr::IsManaged(member->GetGUIDLow()))\n                continue;")
set(GATES G1 G2 G3 G4 G4P G5 G6 G7 G8 G8Q G10 G10L G11 G12 G12M)
# The census: every Blocks / IsManaged call in the module (code only). A new or a dropped caller changes the count: add it to
# the gate list above (design 4) or explain it here. Blocks: G1, G2, G3, G4, G5, G6, G7, G8, G10 x2 = 10. IsManaged: the
# status hook, G11, G12 x2 = 4.
set(N_BLOCKS 10)
set(N_MANAGED 4)

function(check_gates out)
  foreach(g IN LISTS GATES)
    set(text "${${${g}_VAR}}")
    string(FIND "${text}" "${${g}_AT}" fn_at)
    if(fn_at EQUAL -1)
      set(${out} "gates: ${g}: ${${g}_VAR} lacks ${${g}_AT}" PARENT_SCOPE)
      return()
    endif()
    string(SUBSTRING "${text}" ${fn_at} -1 rest)
    string(FIND "${rest}" "${${g}}" at)
    if(at EQUAL -1)
      set(${out} "gates: ${g} missing in ${${g}_VAR} after ${${g}_AT}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # G9 (F15 (a)): the repop leave stays as it is, no gate.
  string(FIND "${repop}" "group_mgr::" at)
  if(NOT at EQUAL -1)
    set(${out} "gates: G9 is no gate by F15 (a); ReleaseSpiritAction.h must not ask the group manager" PARENT_SCOPE)
    return()
  endif()
  strip_comments("${all_sources}${extra}" code)
  count_text("${code}" "group_mgr::Blocks(" nb)
  count_text("${code}" "group_mgr::IsManaged(" nm)
  if(NOT nb EQUAL N_BLOCKS OR NOT nm EQUAL N_MANAGED)
    set(${out} "gates: census: group_mgr::Blocks( ${nb} times, group_mgr::IsManaged( ${nm} times (${N_BLOCKS} and ${N_MANAGED} expected)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (f) the S7 mirrors (design 3.6) -------------------------------------------------------------------------------------------
set(MIRROR_LEADER_S7 "        quest_groups::NoteLeaderStuck(ai);\n        posStuck.Reset();\n        return false;\n    }")
set(MIRROR_LEADER "    if (group_mgr::QuestRoleOf(bot->GetGUIDLow()) == 2)\n    {\n        ai->NoteManagerLeaderStuck();\n        posStuck.Reset();\n        return false;\n    }")
function(check_mirrors out)
  foreach(pair
      "questband|qb::QueuedFree(std::max(ai::quest_groups::QueuedAge(bot->GetGUIDLow(), questId),\n                    ai::group_mgr::QueuedAge(bot->GetGUIDLow(), questId)), queuedFreeMinutes))"
      "choose_travel|(ai::quest_groups::QueuedAge(bot->GetGUIDLow(), questId) ||\n                                ai::group_mgr::QueuedAge(bot->GetGUIDLow(), questId)))"
      "travel|    if (quest_groups::IsMember(ai) || group_mgr::IsQuestMember(ai->GetBot()->GetGUIDLow()))"
      "travel|!quest_groups::IsMember(ai) &&\n            !group_mgr::IsQuestMember(bot->GetGUIDLow()))"
      "travel|        quest_groups::IsFollower(ai) ||   // twow-repo#627 S7: a follower goes where its leader goes (the leader stays watched)\n        group_mgr::IsQuestFollower(bot->GetGUIDLow());"
      "share|!quest_groups::IsMember(ai) &&\n        !group_mgr::IsQuestMember(bot->GetGUIDLow());"
      "stuck|        uint8 const role = std::max<uint8>(ai->GetQuestGroupRole(), group_mgr::QuestRoleOf(bot->GetGUIDLow()));"
      "ai_cpp|    if (managerPosStuck && ai::group_mgr::QuestRoleOf(bot->GetGUIDLow()) != 2)\n        managerPosStuck = false;\n    edge.posStuck = managerPosStuck;")
    string(FIND "${pair}" "|" bar)
    string(SUBSTRING "${pair}" 0 ${bar} var)
    math(EXPR bar "${bar} + 1")
    string(SUBSTRING "${pair}" ${bar} -1 needle)
    string(FIND "${${var}}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "mirrors: ${var} missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The manager QUEST leader's PosStuck mark: right after the S7 leader block, before the episode logic.
  string(FIND "${travel}" "${MIRROR_LEADER_S7}" s7)
  string(FIND "${travel}" "${MIRROR_LEADER}" mgr)
  if(s7 EQUAL -1 OR mgr EQUAL -1 OR NOT s7 LESS mgr)
    set(${out} "mirrors: TravelMgr.cpp: the manager leader-stuck block is missing or not after the S7 one" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (g) the pass order (2.2, M15/M16) and the op budget -------------------------------------------------------------------------
function(check_order out)
  string(FIND "${mgr_cpp}" "void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)\n{\n    ++passes_;\n    std::uint32_t const nowMs = WorldTimer::getMSTime();\n    mgrMs_ = AdvanceClock(mgrMs_, clockLastMs_, nowMs, sPlayerbotAIConfig.randomBotUpdateInterval);\n    DrainInbox(bots, nowMs);\n    RunDirectives(bots, nowMs, now);\n    RunGather(nowMs);\n}" at)
  if(at EQUAL -1)
    # T33-0 (design T33 v1.1 2.5): the manager clock before the drain, the measurement-only meeting shadow after the directive pass.
    set(${out} "order: Update is not clock, drain, the directive pass, then the T33-0 meeting shadow" PARENT_SCOPE)
    return()
  endif()
  region("${mgr_cpp}" "void WorldThreadManager::RunDirectives(" "\n}\n" run)
  string(FIND "${run}" "    group_ops::TakeManagerOutcomes(outcomes_);" take)
  string(FIND "${run}" "    if (!resumedPass_)\n        engine_.ForgetGone(" forget)
  string(FIND "${run}" "    engine_.Pass(in, rules, io);" pass)
  if(take EQUAL -1 OR forget EQUAL -1 OR pass EQUAL -1 OR NOT take LESS pass OR NOT forget LESS pass)
    set(${out} "order: RunDirectives does not take the outcomes and forget (not after a resume) before the pass" PARENT_SCOPE)
    return()
  endif()
  region("${dpol}" "    void Pass(PassInput const& in, Rules const& r, Io& io)\n    {" "\n    }\n" body)
  set(last -1)
  foreach(step "        ApplyOutcomes(in, io);" "        ApplyAcks(in, r, io);" "        GapRule(in, r, io);" "        ConfirmEdges(in, r, io);"
      "        if (!in.resumed)\n            Deadlines(r, io);" "        if (!in.resumed)\n            EndRules(in, r, io);" "        Form(in, r, io);"
      "        RunOps(in, io);" "        Sweep(in);" "        MinuteLine(in, r, io);")
    string(FIND "${body}" "${step}" at)
    if(at EQUAL -1 OR NOT at GREATER last)
      set(${out} "order: Engine::Pass step missing or out of order: ${step}" PARENT_SCOPE)
      return()
    endif()
    set(last ${at})
  endforeach()
  foreach(needle "constexpr std::uint32_t kOpsPerPass = 1;" "constexpr std::uint32_t kMaxManagerOpsInFlight = 8;"
      "constexpr std::uint32_t kExitTries = 2;" "constexpr std::uint32_t kAckTimeoutMs = 10000;" "constexpr std::uint32_t kJoinConfirmMs = 20000;")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "order: GroupDirectivePolicy.h missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h) the minute lines --------------------------------------------------------------------------------------------------------
function(check_minute out)
  foreach(needle
      "\"[GroupMgr] minute types=%u passes=%llu cand_adhoc=%llu cand_quest=%llu cand_busy=%llu\"\n        \" cand_zone_hold=%llu wishes=%llu no_supervisor=%llu stale_snap=%llu key_overflow=%llu\""
      "\" gap_ms_max=%u pass_us_max=%u groups=%llu members=%llu silent=%llu owned=%llu\""
      "    int n = std::snprintf(buf, size, \"[GroupMgr] minute2\");"
      "\" leave_%s=%llu\""
      "constexpr std::uint32_t kLinesPerMinute = 300;"
      "        case Cnt::DirDropped: return \"dir_dropped\";"
      "        case Cnt::LinesDropped: return \"lines_dropped\";"
      "        case EndReason::DirectiveDropped: return \"directive_dropped\";"
      # 5.2 event lines the A/B evaluation reads per arm and per hitch.
      "\" levels=%u-%u map=%u cohort=%c\""
      "\"[GroupMgr] event=gap ms=%u action=%s\""
      "        GapLine(io, gap, \"suspend\");"
      "            GapLine(io, passed_ ? now_ - lastPassMs_ : 0u, \"resume\");")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "minute: GroupDirectivePolicy.h missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # The line text lives in the pure header; GroupManager.cpp only hands it to the log.
  string(FIND "${mgr_cpp}" "[GroupMgr]" at)
  if(NOT at EQUAL -1)
    set(${out} "minute: GroupManager.cpp formats a [GroupMgr] line itself" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h2) the cohort (2.1, A/B with Manager 2/3): only owned bots become candidates or get a foreign leave ----------------------
function(check_cohort out)
  region("${dpol}" "    void Form(PassInput const& in, Rules const& r, Io& io)" "\n    }\n" form)
  region("${dpol}" "    void Strays(PassInput const& in, Rules const& r, Io& io)" "\n    }\n" strays)
  set(own "            if (!Owns(r.mode, r.types, low))\n                continue;")
  string(FIND "${form}" "${own}\n            ++stands_.owned;" a)
  string(FIND "${strays}" "${own}" b)
  if(a EQUAL -1 OR b EQUAL -1)
    set(${out} "cohort: Form or Strays lacks the Owns(r.mode, r.types, low) filter" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h3) the resume drain (OB-10 10.11.) --------------------------------------------------------------------------------------
# One kDrainMaxPerPass pass would leave up to 3072 old events that a later pass stores with receivedMs = now (fresh). After a
# resume the pass loops Drain until it returns 0, at most Capacity events (no spin while bots post), and stores none.
set(RESUME_DRAIN "    inboxBuf_.clear();\n    for (;;)\n    {\n        std::size_t const taken = manager_inbox::Drain(inboxBuf_, manager_inbox::kDrainMaxPerPass);\n        if (!resumed || !taken || inboxBuf_.size() >= manager_inbox::Capacity)\n            break;\n    }\n    for (manager_inbox::Event const& event : inboxBuf_)\n    {\n        ++kindTotals_[manager_inbox::KindIndex(event.kind)];")
function(check_resume_drain out)
  region("${mgr_cpp}" "void WorldThreadManager::DrainInbox(" "\n}\n" body)
  count_text("${body}" "${RESUME_DRAIN}" n)
  string(FIND "${body}" "        if (!resumed)\n            StoreEvent(event, nowMs);" store)
  if(NOT n EQUAL 1 OR store EQUAL -1)
    set(${out} "resume drain: DrainInbox does not drain the whole ring after a resume (bounded by Capacity) or stores it" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h3b) the wish of the latest period (OB-10 note on #984) -----------------------------------------------------------------
# A Status lost to a full ring leaves the old wish behind until the next Status. The manager uses a wish only while it is
# younger than WishFreshMs (1.5 status periods), with the stale-anchor credit, and drops it before the candidate test.
function(check_wish out)
  foreach(needle "    std::uint64_t const ms = std::uint64_t(statusSeconds) * 1500u;" "        wishMs_ = WishFreshMs(r.statusSeconds);"
      "        return AgeMs(receivedMs) <= wishMs_;"
      "            manager_inbox::WishSnapshot const* w = FindWish(in.wishes, low);
            if (w && !FreshWish(w->receivedMs))
                w = nullptr;")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "wish: GroupDirectivePolicy.h missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  count_text("${dpol}" "FindWish(in.wishes, " n)
  if(NOT n EQUAL 1)
    set(${out} "wish: FindWish(in.wishes, ...) appears ${n} times (one lookup, filtered by FreshWish)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h4) seq, serial and the ack match (9b R3/R4) --------------------------------------------------------------------------------
function(check_serial out)
  foreach(needle
      "    std::uint32_t NextSeq()\n    {\n        if (!++nextSeq_)\n            ++nextSeq_;   // the wrap skips 0\n        return nextSeq_;\n    }"
      "        nextSerial_ = NextGroupSerial(nextSerial_, [this](std::uint16_t low16) { return Low16InUse(low16); });"
      "        ++last;\n        std::uint16_t const low16 = std::uint16_t(last);\n        if (low16 && !inUse(low16))\n            return last;"
      "        for (auto const& entry : groups_)\n            if (std::uint16_t(entry.first) == low16)\n                return true;"
      "            if (!exitAck && !enterAck)\n            {\n                Count(Cnt::AckStale);   // superseded or unknown (R4), any state: counted, nothing else\n                continue;\n            }\n            if (a.state == manager_inbox::kAckReceived)\n            {\n                Count(Cnt::AckRcv);")
    string(FIND "${dpol}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "serial: GroupDirectivePolicy.h missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Only NextSeq / NextSerial move the counters: one assignment each besides the declaration, nothing resets them.
  strip_comments("${dpol}" dcode)
  count_text("${dcode}" "nextSeq_ =" n_seq)
  count_text("${dcode}" "nextSerial_ =" n_serial)
  count_text("${dcode}" "std::uint32_t const seq = NextSeq();" n_next)
  if(NOT n_seq EQUAL 1 OR NOT n_serial EQUAL 2 OR NOT n_next EQUAL 2)
    set(${out} "serial: a reset or another writer of the counters (nextSeq_ = ${n_seq}, nextSerial_ = ${n_serial}, NextSeq() users ${n_next}; 1, 2 and 2 expected)" PARENT_SCOPE)
    return()
  endif()
  # The engine lives as long as the process: one static manager, never assigned or re-made.
  strip_comments("${mgr_h}\n${mgr_cpp}" mcode)
  string(FIND "${mcode}" "engine_ =" assign)
  string(FIND "${mcode}" "Engine()" remake)
  count_text("${mcode}" "    static WorldThreadManager manager;" n_static)
  if(NOT assign EQUAL -1 OR NOT remake EQUAL -1 OR NOT n_static EQUAL 1)
    set(${out} "serial: the engine is re-made or assigned (its counters would restart)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (h5) the T02-GROUP read view -----------------------------------------------------------------------------------------------
# The fast path first (one atomic, no lock while the registry is empty), then the lock for the lookup only (the body ends with
# the lookup's return; the body checks below catch a log or an allocation anywhere in it).
set(VIEW_MEMBER "    bool IsMember(std::uint32_t low, std::uint16_t serial) const\n    {\n        if (!Any() || !serial)\n            return false;\n        std::lock_guard<std::mutex> lock(mutex_);\n        auto const it = bots_.find(low);")
set(VIEW_MEMBER_END "        return it != bots_.end() && it->second.serial == serial;\n    }")
set(VIEW_FIND "    bool Find(std::uint32_t low, ViewEntry& out) const\n    {\n        if (!Any())\n            return false;\n        std::lock_guard<std::mutex> lock(mutex_);")
function(check_view out)
  foreach(pair "view_h|bool IsManaged(std::uint32_t guidLow);" "view_h|bool IsManaged(std::uint32_t guidLow, std::uint16_t serial);"
      "mgr_cpp|bool IsManaged(std::uint32_t guidLow, std::uint16_t serial)\n{\n    return WorldManager().Directives().View().IsMember(guidLow, serial);"
      "dpol|${VIEW_MEMBER}" "dpol|${VIEW_MEMBER_END}" "dpol|${VIEW_FIND}"
      "dpol|                if (m.state != MemberState::Leaving)\n                    mv.serial = std::uint16_t(g.serial);"
      "gm_policy|constexpr std::uint32_t kOrphanGraceMs = 60000;")
    string(FIND "${pair}" "|" bar)
    string(SUBSTRING "${pair}" 0 ${bar} var)
    math(EXPR bar "${bar} + 1")
    string(SUBSTRING "${pair}" ${bar} -1 needle)
    string(FIND "${${var}}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "view: ${var} missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # No log, no allocation in the bodies a map thread runs (both overloads and the view lookups behind them).
  region("${mgr_cpp}" "bool IsManaged(std::uint32_t guidLow)\n{" "\n}\n" b1)
  region("${mgr_cpp}" "bool IsManaged(std::uint32_t guidLow, std::uint16_t serial)\n{" "\n}\n" b2)
  region("${dpol}" "    bool Find(std::uint32_t low, ViewEntry& out) const\n    {" "\n    }\n" b3)
  region("${dpol}" "    bool IsMember(std::uint32_t low, std::uint16_t serial) const\n    {" "\n    }\n" b4)
  foreach(b b1 b2 b3 b4)
    strip_comments("${${b}}" code)
    if("${code}" STREQUAL "")
      set(${out} "view: a read body not found (${b})" PARENT_SCOPE)
      return()
    endif()
    foreach(bad "sLog" "push_back" "std::string" "emplace" "insert(" "[")
      string(FIND "${code}" "${bad}" at)
      if(NOT at EQUAL -1)
        set(${out} "view: a read body logs or allocates (${b}: ${bad})" PARENT_SCOPE)
        return()
      endif()
    endforeach()
    if("${code}" MATCHES "(^|[^A-Za-z0-9_])new([^A-Za-z0-9_]|$)")
      set(${out} "view: a read body logs or allocates (${b}: new)" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # No caller of the two-argument overload in this pair (CLI-Projekt's T02-GROUP pair adds it): declaration + definition.
  strip_comments("${all_sources}${extra}" code)
  string(REGEX MATCHALL "IsManaged\\(([^(),]|\\([^()]*\\))*," calls "${code}")
  list(LENGTH calls n)
  if(NOT n EQUAL 2)
    set(${out} "view: IsManaged(guid, serial) appears ${n} times (2 expected: declaration and definition, no caller)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (i) the unit tests ----------------------------------------------------------------------------------------------------------
function(check_unit out)
  foreach(needle "add_executable(group_directive_policy_tests\n  \"\${PB_MODULE_DIR}/t/group_directive_policy_tests.cpp\")"
      "add_test(NAME group_directive_policy\n  COMMAND group_directive_policy_tests")
    string(FIND "${tests_cmake}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "unit: tests.cmake missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "    TestDirectiveBits();" "    TestTypesZero();" "    TestAckTimeoutFailClosed();" "    TestJoinClock();" "    TestGapRule();"
      "    TestResume();" "    TestDirectiveDropped();" "    TestExitRetry();" "    TestSwitchOff();" "    TestForeignGroup();"
      "    TestQuestAttempts();" "    TestWishLeftover();" "    TestMinuteLine();" "    TestWrap();" "    TestFormingNoAttempt();" "    TestQuestViewKey();"
      "    TestLeaveEdgeLag();" "    TestGapSameMs();" "    TestGapStaleAnchor();" "    TestExitNoSupervisor();" "    TestEndWhileJoinQueued();"
      "    TestCohort();" "    TestSilentMember();" "    TestRealPlayerLeaving();" "    TestSerialAcrossOffOn();" "    TestSerialLow16();"
      "    TestAckStale();" "    TestSwitchOffExits();" "    TestManagedSerialView();")
    string(FIND "${unit_test}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "unit: t/group_directive_policy_tests.cpp does not run ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(pair "gm_test|\"Types 0: the manager owns nobody (the default)\"" "gm_test|\"StatusSeconds 0: Types acts as 0\""
      "adhoc_test|managed.managerOwns = true;")
    string(FIND "${pair}" "|" bar)
    string(SUBSTRING "${pair}" 0 ${bar} var)
    math(EXPR bar "${bar} + 1")
    string(SUBSTRING "${pair}" ${bar} -1 needle)
    string(FIND "${${var}}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "unit: ${var} missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# By name (no cmake_language: the contract stays at the 3.16 feature set).
function(run_check check out)
  if(check STREQUAL "check_switch")
    check_switch(r)
  elseif(check STREQUAL "check_noop")
    check_noop(r)
  elseif(check STREQUAL "check_post")
    check_post(r)
  elseif(check STREQUAL "check_ops")
    check_ops(r)
  elseif(check STREQUAL "check_pure")
    check_pure(r)
  elseif(check STREQUAL "check_gates")
    check_gates(r)
  elseif(check STREQUAL "check_mirrors")
    check_mirrors(r)
  elseif(check STREQUAL "check_order")
    check_order(r)
  elseif(check STREQUAL "check_minute")
    check_minute(r)
  elseif(check STREQUAL "check_cohort")
    check_cohort(r)
  elseif(check STREQUAL "check_resume_drain")
    check_resume_drain(r)
  elseif(check STREQUAL "check_wish")
    check_wish(r)
  elseif(check STREQUAL "check_serial")
    check_serial(r)
  elseif(check STREQUAL "check_view")
    check_view(r)
  elseif(check STREQUAL "check_unit")
    check_unit(r)
  else()
    message(FATAL_ERROR "${TAG}: unknown check ${check}")
  endif()
  set(${out} "${r}" PARENT_SCOPE)
endfunction()

# ---- the real sources ----------------------------------------------------------------------------------------------------------
foreach(check check_switch check_noop check_post check_ops check_pure check_gates check_mirrors check_order check_minute check_cohort
    check_resume_drain check_wish check_serial check_view check_unit)
  run_check(${check} r)
  fail_if("${r}")
endforeach()

# ---- negative probes -----------------------------------------------------------------------------------------------------------
# probe(var old new check pattern what): replace old by new in the variable, rerun the check, restore the variable.
function(probe var old new check pattern what)
  string(FIND "${${var}}" "${old}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${TAG}: probe ${what}: anchor not found in ${var}")
  endif()
  string(REPLACE "${old}" "${new}" mutated "${${var}}")
  set(${var} "${mutated}")
  run_check(${check} r)
  expect_reason("${what}" "${r}" "${pattern}")
endfunction()

# (a) the switch.
probe(config_h "    uint32 botGroupsManagerTypes = 0;" "    uint32 botGroupsManagerTypes = 3;" check_switch "switch: config_h" "member default 3")
probe(config_cpp "\"AiPlayerbot.BotGroups.Manager.Types\", 0)" "\"AiPlayerbot.BotGroups.Manager.Types\", 1)" check_switch "switch: config_cpp" "loader default 1")
probe(conf_dist "AiPlayerbot.BotGroups.Manager.Types = 0" "AiPlayerbot.BotGroups.Manager.Types = 2" check_switch "switch: conf_dist" "dist default 2")
probe(config_cpp "            {\"BotGroups.Manager.Types\", Value(botGroupsManagerTypes)},\n" "" check_switch "echo entry" "echo entry dropped")
probe(config_cpp "            {\"BotGroups.Manager.StatusSeconds\", Value(botGroupsManagerStatusSeconds)},\n            {\"BotGroups.Manager.Types\", Value(botGroupsManagerTypes)},\n"
  "            {\"BotGroups.Manager.Types\", Value(botGroupsManagerTypes)},\n            {\"BotGroups.Manager.StatusSeconds\", Value(botGroupsManagerStatusSeconds)},\n" check_switch "echo entry" "echo out of order")
probe(gm_policy "    return statusSeconds ? SanitizeTypes(types) : 0u;" "    return SanitizeTypes(types);" check_switch "switch: gm_policy" "Types without StatusSeconds (F9)")
probe(config_cpp "        uint32 const masked = typesRaw < 0 ? 0u : ai::group_mgr::SanitizeTypes(botGroupsManagerTypes);"
  "        uint32 const masked = ai::group_mgr::SanitizeTypes(botGroupsManagerTypes);" check_switch "switch: config_cpp" "a negative Types masked to 3 instead of 0")
probe(config_cpp "    botGroupsManagerTypes = uint32(config.GetIntDefault(\"AiPlayerbot.BotGroups.Manager.Types\", 0));\n    int32 const typesRaw = int32(botGroupsManagerTypes);"
  "    int32 const typesRaw = int32(config.GetIntDefault(\"AiPlayerbot.BotGroups.Manager.Types\", 0));\n    botGroupsManagerTypes = typesRaw > 0 ? uint32(typesRaw) : 0u;" check_switch "switch: config_cpp" "the member not read straight from the key (config_echo)")
# (b) the no-op.
probe(mgr_cpp "    if (!types && !engine_.HasGroups())\n" "    if (false)\n" check_noop "no-op: RunDirectives" "the directive pass runs with Types 0")
probe(rpm "        if (sPlayerbotAIConfig.botGroupsManager || ai::group_mgr::WorldManager().HasGroups())\n" "        if (true)\n" check_noop "no-op: the call site" "the call site ungated")
probe(dpol "        if (!Any())\n            return false;\n        std::lock_guard<std::mutex> lock(mutex_);\n        auto const it = bots_.find(low);"
  "        std::lock_guard<std::mutex> lock(mutex_);\n        auto const it = bots_.find(low);" check_noop "no-op: GroupDirectivePolicy.h fast path" "a lock on every gate read")
probe(mgr_cpp "    return OwnsBot(guidLow) || IsManaged(guidLow);" "    return IsManaged(guidLow);" check_noop "no-op: GroupManager.cpp read view" "Blocks without OwnsBot")
probe(dpol "        if (r.types || HasGroups())" "        if (true)" check_noop "no-op: GroupDirectivePolicy.h fast path" "the minute line with Types 0")
probe(ops_cpp "    if (op.reason & kJoinManager)\n    {\n        if (group->GetMembersCount() == 2)" "    if (true)\n    {\n        if (group->GetMembersCount() == 2)" check_noop "no-op: RunJoin" "every join takes the manager branch")
# (c) the post.
probe(mgr_cpp "        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, low))\n            return ai::supervisor::Post(" "            return ai::supervisor::Post(" check_post "post: supervisor::Post" "the post without the R10 gate")
set(extra "\n    ai::supervisor::Post(low, ev);")
check_post(r)
expect_reason("a second directive post" "${r}" "post: supervisor::Post\\( called 2 times")
set(extra "\n    ai::manager_inbox::PostDirectiveAck(low, 1, 2, 0);")
check_post(r)
expect_reason("a caller of PostDirectiveAck" "${r}" "post: PostDirectiveAck")
set(extra "")
probe(dpol "Cnt::DirDropped" "Cnt::DirSent" check_post "post: GroupDirectivePolicy.h" "a dropped post not counted")
# (d) the ops and the pure headers.
set(extra "\n    ai::group_ops::QueueManagerJoin(1, 2, 0, ai::group_mgr::GroupType::Adhoc, 0, 0);")
check_ops(r)
expect_reason("a manager join from another file" "${r}" "ops: QueueManagerJoin\\( called 1 times outside")
set(extra "")
probe(mgr_cpp "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }" "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }\n    bool Alive(Player* p) { return p->IsAlive(); }" check_ops "ops: GroupManager.cpp reads beyond" "a Player* dereference in the manager")
probe(dpol "#include \"GroupOpPolicy.h\"" "#include \"GroupOpPolicy.h\"\n#include \"playerbot/playerbot.h\"" check_pure "pure: GroupDirectivePolicy.h has a non-pure include" "a game include in the engine header")
probe(dpol "constexpr std::uint32_t kOpsPerPass = 1;" "constexpr std::uint32_t kOpsPerPass = sPlayerbotAIConfig.x;" check_pure "pure: GroupDirectivePolicy.h names" "a config read in the engine header")
probe(view_h "#include <cstdint>" "#include <cstdint>\n#include \"Player.h\"" check_pure "pure: GroupManagerView.h" "a game include in the view header")
# (e) the gates.
probe(adhoc_action "        !group_mgr::Blocks(bot->GetGUIDLow());   // G1 (T30-1)" "        true;   // G1 (T30-1)" check_gates "gates: G1 missing" "G1 dropped")
probe(adhoc_world "    return CandidateFacts(player) && !group_mgr::Blocks(player->GetGUIDLow());" "    return CandidateFacts(player);" check_gates "gates: G2 missing" "G2 dropped")
probe(adhoc_policy "    if (f.transient || (f.helping && !f.managerOwns))" "    if (f.transient || f.helping)" check_gates "gates: G4P missing" "G4 policy keeps an owned helper")
probe(leave "        // G6 (T30-1, F14 (a)): a manager group is left by the manager's end rules; LeaveFarAway stays off for its bots.\n        if (group_mgr::Blocks(bot->GetGUIDLow()))\n            return false;\n" "" check_gates "gates: G6 missing" "G6 dropped")
probe(accept " && group_mgr::Blocks(bot->GetGUIDLow()) && !::IsRealPlayer(inviter))" " && !::IsRealPlayer(inviter))" check_gates "gates: G8 missing" "G8 dropped")
probe(accept "                if (!group_ai_refresh::IsWorldManagerSource(event.getSource()) && group_ops::QueueAccept(ai, inviter->GetGUIDLow(), true))"
  "                if (group_ops::WritePathRoutes(ai) && !group_ai_refresh::IsWorldManagerSource(event.getSource()) && group_ops::QueueAccept(ai, inviter->GetGUIDLow(), true))"
  check_gates "gates: G8Q missing" "G8 queues its decline only with WritePath (M5)")
probe(invite "        if (group_mgr::Blocks(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster())\n            return false;" "        if (group_mgr::Blocks(bot->GetGUIDLow()))\n            return false;" check_gates "gates: G7 missing" "G7 without the real-player-master exception")
probe(invite "        if (requester && group_mgr::Blocks(bot->GetGUIDLow()) && GetBotAI(requester) && !GetBotAI(requester)->IsRealPlayer())\n            return false;\n" "" check_gates "gates: G10L missing" "G10 (LFG) dropped")
probe(leave "        if (master && master != bot && group_mgr::IsManaged(bot->GetGUIDLow()))" "        if (master && master != bot && false)" check_gates "gates: G11 missing" "G11 dropped")
probe(bgjoin "            if (ai::group_mgr::IsManaged(member->GetGUIDLow()))\n                continue;\n" "" check_gates "gates: G12M missing" "G12 (members) dropped")
probe(repop "#pragma once" "#pragma once\n// x\nbool g9 = ai::group_mgr::Blocks(1);" check_gates "gates: G9 is no gate" "a G9 gate against F15 (a)")
set(extra "\n    if (ai::group_mgr::Blocks(bot->GetGUIDLow())) return false;")
check_gates(r)
expect_reason("a new Blocks caller outside the census" "${r}" "gates: census")
set(extra "")
# (f) the mirrors.
probe(questband "                    ai::group_mgr::QueuedAge(bot->GetGUIDLow(), questId)), queuedFreeMinutes))" "                    0u), queuedFreeMinutes))" check_mirrors "mirrors: questband" "QuestBand queue age without the manager")
probe(stuck "std::max<uint8>(ai->GetQuestGroupRole(), group_mgr::QuestRoleOf(bot->GetGUIDLow()))" "ai->GetQuestGroupRole()" check_mirrors "mirrors: stuck" "StuckTriggers without the manager role")
probe(travel "${MIRROR_LEADER}\n" "" check_mirrors "mirrors: TravelMgr.cpp: the manager leader-stuck block" "TravelMgr without the manager leader-stuck block")
probe(ai_cpp "    edge.posStuck = managerPosStuck;" "    edge.posStuck = false;" check_mirrors "mirrors: ai_cpp" "the PosStuck mark not posted")
# (g) the order.
probe(dpol "        GapRule(in, r, io);\n        ConfirmEdges(in, r, io);" "        ConfirmEdges(in, r, io);\n        GapRule(in, r, io);" check_order "order: Engine::Pass step" "the gap rule after the confirmations")
probe(dpol "        if (!in.resumed)\n            Deadlines(r, io);" "        Deadlines(r, io);" check_order "order: Engine::Pass step" "deadlines in a resumed pass (M16)")
probe(mgr_cpp "    if (!resumedPass_)\n        engine_.ForgetGone(" "    engine_.ForgetGone(" check_order "order: RunDirectives" "forget in a resumed pass (M16)")
probe(dpol "constexpr std::uint32_t kOpsPerPass = 1;" "constexpr std::uint32_t kOpsPerPass = 4;" check_order "order: GroupDirectivePolicy.h missing" "a larger op budget")
# (h) the minute lines.
probe(dpol "\" gap_ms_max=%u pass_us_max=%u groups=%llu members=%llu silent=%llu owned=%llu\"" "\" gap_ms_max=%u pass_us_max=%u groups=%llu members=%llu silent=%llu\"" check_minute "minute: GroupDirectivePolicy.h missing" "a minute field dropped")
probe(dpol "constexpr std::uint32_t kLinesPerMinute = 300;" "constexpr std::uint32_t kLinesPerMinute = 100000;" check_minute "minute: GroupDirectivePolicy.h missing" "the line cap lifted")
probe(dpol "\" levels=%u-%u map=%u cohort=%c\"" "\" map=%u\"" check_minute "minute: GroupDirectivePolicy.h missing" "the formed line without levels and cohort")
probe(dpol "            if (!Owns(r.mode, r.types, low))\n                continue;\n            ++stands_.owned;" "            ++stands_.owned;" check_cohort "cohort: Form or Strays" "a bot of the other arm as a candidate")
probe(dpol "            if (!Owns(r.mode, r.types, low))\n                continue;\n            auto const rep" "            auto const rep" check_cohort "cohort: Form or Strays" "a foreign leave for a bot of the other arm")
probe(mgr_cpp "    void Line(char const* line) override { sLog.outBasic(\"%s\", line); }" "    void Line(char const* line) override { sLog.outBasic(\"[GroupMgr] %s\", line); }" check_minute "minute: GroupManager.cpp formats" "a [GroupMgr] format in the manager")
# (h3) the resume drain.
probe(mgr_cpp "        if (!resumed || !taken || inboxBuf_.size() >= manager_inbox::Capacity)\n            break;" "        break;" check_resume_drain "resume drain:" "one kDrainMaxPerPass pass after a resume (old leftovers stored as fresh later)")
probe(mgr_cpp " || inboxBuf_.size() >= manager_inbox::Capacity)\n            break;" ")\n            break;" check_resume_drain "resume drain:" "the resume drain without the capacity bound (may spin)")
probe(mgr_cpp "        if (!resumed)\n            StoreEvent(event, nowMs);" "        StoreEvent(event, nowMs);" check_resume_drain "resume drain:" "the resumed pass stores its old events")
# (h3b) the wish of the latest period.
probe(dpol "            if (w && !FreshWish(w->receivedMs))
                w = nullptr;" "            if (false)
                w = nullptr;" check_wish "wish: GroupDirectivePolicy.h missing" "a leftover wish of an older period used")
probe(dpol "    std::uint64_t const ms = std::uint64_t(statusSeconds) * 1500u;" "    std::uint64_t const ms = std::uint64_t(statusSeconds) * 3000u;" check_wish "wish: GroupDirectivePolicy.h missing" "the wish window widened to kStaleMs")
# (h4) seq and serial.
probe(dpol "        if (low16 && !inUse(low16))" "        if (!inUse(low16))" check_serial "serial: GroupDirectivePolicy.h missing" "a serial with low 16 bits 0 handed out")
probe(dpol "        for (auto const& entry : groups_)\n            if (std::uint16_t(entry.first) == low16)" "        for (auto const& entry : groups_)\n            if (entry.first == low16)" check_serial "serial: GroupDirectivePolicy.h missing" "the collision check on the full 32-bit serial")
probe(dpol "    std::uint32_t nextSerial_ = 0;   // only NextSerial moves it (never reset)" "    std::uint32_t nextSerial_ = 0;   // only NextSerial moves it (never reset)\n    void Reset() { nextSerial_ = 0; nextSeq_ = 0; }" check_serial "serial: a reset" "a counter reset")
probe(mgr_cpp "    engine_.Pass(in, rules, io);" "    if (in.resumed)\n        engine_ = Engine();\n    engine_.Pass(in, rules, io);" check_serial "serial: the engine is re-made" "the engine re-made on a resume")
probe(dpol "                Count(Cnt::AckStale);   // superseded or unknown (R4), any state: counted, nothing else\n                continue;\n            }\n            if (a.state == manager_inbox::kAckReceived)"
  "                Count(Cnt::AckStale);   // superseded or unknown (R4), any state: counted, nothing else\n                continue;\n            }\n            if (a.state == manager_inbox::kAckReceived || a.state == manager_inbox::kAckApplied)" check_serial "serial: GroupDirectivePolicy.h missing" "an applied ack treated as received")
# (h5) the T02-GROUP read view.
probe(dpol "        if (!Any() || !serial)\n            return false;\n" "" check_view "view: dpol missing" "the two-argument read without the atomic fast path")
probe(mgr_cpp "    return WorldManager().Directives().View().IsMember(guidLow, serial);\n}" "    return WorldManager().Directives().View().IsMember(guidLow, serial);\n    sLog.outBasic(\"managed %u\", guidLow);\n}" check_view "view: a read body logs or allocates \\(b2: sLog\\)" "a log line in the two-argument read")
probe(mgr_cpp "    ViewEntry v;\n    return WorldManager().Directives().View().Find(guidLow, v);\n}\n\nbool IsManaged(" "    ViewEntry v;\n    std::string why;\n    return WorldManager().Directives().View().Find(guidLow, v);\n}\n\nbool IsManaged(" check_view "view: a read body logs or allocates \\(b1: std::string\\)" "a string in the one-argument read")
probe(dpol "        auto const it = bots_.find(low);\n        return it != bots_.end() && it->second.serial == serial;" "        auto const it = bots_.find(low);\n        int* p = new int(1);\n        delete p;\n        return it != bots_.end() && it->second.serial == serial;" check_view "view: a read body logs or allocates \\(b4: new\\)" "an allocation in the two-argument lookup")
probe(dpol "        out = it->second;\n        return true;" "        out = it->second;\n        static std::vector<std::uint32_t> seen;\n        seen.push_back(low);\n        return true;" check_view "view: a read body logs or allocates \\(b3: push_back\\)" "a push_back in the one-argument lookup")
probe(dpol "                if (m.state != MemberState::Leaving)\n                    mv.serial = std::uint16_t(g.serial);" "                mv.serial = std::uint16_t(g.serial);" check_view "view: dpol missing" "an ending member still live under its serial")
probe(gm_policy "constexpr std::uint32_t kOrphanGraceMs = 60000;" "constexpr std::uint32_t kOrphanGraceMs = 10000;" check_view "view: gm_policy missing" "a shorter orphan grace")
set(extra "\n    if (!ai::group_mgr::IsManaged(bot->GetGUIDLow(), serial)) LeaveGroupMode();")
check_view(r)
expect_reason("a caller of IsManaged(guid, serial) in this pair" "${r}" "view: IsManaged\\(guid, serial\\) appears 3 times")
set(extra "")
# (i) the unit tests.
probe(tests_cmake "add_test(NAME group_directive_policy\n" "add_test(NAME other\n" check_unit "unit: tests.cmake missing" "the unit test not registered")
probe(unit_test "    TestTypesZero();\n" "" check_unit "unit: t/group_directive_policy_tests.cpp does not run" "the Types 0 test not run")

message(STATUS "A3S6_T30_DIRECTIVES_SOURCE_CONTRACT=PASS")
