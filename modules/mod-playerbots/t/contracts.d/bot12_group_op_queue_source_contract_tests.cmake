cmake_policy(VERSION 3.16)
# BOT-12 (OB-00 04.11.2026; twow-repo#677, #628; design cli-grp/crossmap/DESIGN-GROUPOPS-WORLDTHREAD-20261104.md 3 and 5, PR1):
# AiPlayerbot.BotGroups.OpQueue (0 off = as before, 1 all, 2 cohort B, 3 cohort A). ObjectMgr::m_GroupMap is an unlocked map
# and group ids come from a plain counter; with the switch on for a bot, its ad-hoc leaves and joins are queued (plain ops) and
# run on the world thread with the maps idle. Pinned here:
#   (a) the ad-hoc action path calls no group mutation when OpQueue applies - it enqueues (leave: CheckLeave -> EnqueueLeave
#       before the old "leave" call; join: Scan -> EnqueueJoin before SendInvite), the clocks are not reset on enqueue, and a
#       pending op stops the action before anything else (Execute);
#   (b) the drain sits in PlayerbotWorldScript::OnUpdate right after sRandomPlayerbotMgr.UpdateAI (both branches), nowhere else
#       (not in RandomPlayerbotMgr::UpdateAIInternal);
#   (c) at most OpsPerTick ops per tick (default 1), none in a tick the ad-hoc manager or S7 already changed a group in;
#   (d) the AI tail is deferred: the queue's accept uses the group-op source, whose tail always goes to the bot (no config);
#       the leave posts LeaveReset and writes its lines through a forced DeferredLog scope; no strategy work in GroupOps.cpp;
#   (e) switch-0 wiring: the old action path is still there behind the switch, the keys default to 0/1/2048/60 with clamps,
#       echoed sorted, documented; the minute line and the off-thread observer (GroupScript, no core hook).
# Pure rules: t/group_op_queue_tests.cpp. Every check_* returns "" when it holds, else the reason; the negative probes at the
# end reuse them on mutated text.
if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "bot12 group_op_queue: PB_SOURCE_DIR is required")
endif()

set(TAG "bot12 group_op_queue")

function(read_source path out_var)
  if(NOT EXISTS "${PB_SOURCE_DIR}/${path}")
    message(FATAL_ERROR "${TAG}: missing source ${path}")
  endif()
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(fail_if reason)
  if(NOT "${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: ${reason}")
  endif()
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

# The text from the first `start` up to the next `stop` after it ("" when start is missing).
function(region text start stop out_var)
  string(FIND "${text}" "${start}" at)
  if(at EQUAL -1)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  string(SUBSTRING "${text}" ${at} -1 rest)
  string(LENGTH "${start}" start_len)
  string(SUBSTRING "${rest}" ${start_len} -1 after)
  string(FIND "${after}" "${stop}" end)
  if(end EQUAL -1)
    set(${out_var} "${rest}" PARENT_SCOPE)
  else()
    math(EXPR len "${start_len} + ${end}")
    string(SUBSTRING "${rest}" 0 ${len} body)
    set(${out_var} "${body}" PARENT_SCOPE)
  endif()
endfunction()

# "" when `first` occurs in text and before every occurrence of `later` (later may be absent), else the reason.
function(check_before text first later what out)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${later}" b)
  if(a EQUAL -1)
    set(${out} "${what}: '${first}' missing" PARENT_SCOPE)
  elseif(NOT b EQUAL -1 AND NOT a LESS b)
    set(${out} "${what}: '${first}' is not before '${later}'" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()

# "" when none of the needles occurs in text.
function(check_none text what out)
  foreach(needle IN LISTS ARGN)
    string(FIND "${text}" "${needle}" at)
    if(NOT at EQUAL -1)
      set(${out} "${what}: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# Every call that changes a group (or a group invite) on the calling thread.
set(MUTATIONS "HandleGroup" "DoSpecificAction(" "SendInvite(" "RemoveMember(" "RemoveFromGroup(" "Disband(" "AddMember("
  "UninviteFromGroup(" "SetLootMethod(" "ConvertToRaid(" "ChangeLeader(" "Groups().Register(" "Groups().Forget(")

set(LEAVE_GATE "    if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, bot->GetGUIDLow()))\n        return EnqueueLeave(group, entry, reason, now);")
set(JOIN_GATE "        if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, invitee->GetGUIDLow()))")
set(PENDING_GATE "    if (group_ops::StillPending(ai, bot->GetGUIDLow()))\n        return false;")

# ---- (a) the action path enqueues, never mutates, with OpQueue on --------------------------------------------------
function(check_action action out)
  region("${action}" "bool AdhocGroupAction::CheckLeave(Group* group, uint32 now)" "\n}\n" leave)
  if(leave STREQUAL "")
    set(${out} "AdhocGroupAction::CheckLeave not found" PARENT_SCOPE)
    return()
  endif()
  check_before("${leave}" "adhoc_group::DecideLeave(facts);" "${LEAVE_GATE}" "the decision before the gate" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  # The gate comes before every mutation and before the clock reset of the old path.
  foreach(later "ai->DoSpecificAction(\"leave\"" "adhoc_group::Groups().Forget(" "trackedGroup = 0;" "[BotGroup] kind=adhoc event=leave")
    check_before("${leave}" "${LEAVE_GATE}" "${later}" "CheckLeave: the OpQueue gate before ${later}" r)
    if(NOT r STREQUAL "")
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  region("${action}" "bool AdhocGroupAction::EnqueueLeave(" "\n}\n" enq_leave)
  region("${action}" "bool AdhocGroupAction::EnqueueJoin(" "\n}\n" enq_join)
  if(enq_leave STREQUAL "" OR enq_join STREQUAL "")
    set(${out} "EnqueueLeave / EnqueueJoin not found" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "op.kind = group_ops::OpKind::Leave;" "op.groupId = group->GetId();" "op.reason = uint8(reason);"
      "if (group_ops::Enqueue(op, ai, nullptr) != group_ops::Pushed::Queued)"
      "opRetryAt = now + std::max<uint32>(1, sPlayerbotAIConfig.botGroupsAdhocScanIntervalSeconds);"
      "reason=%s members=%u via=queue\"")
    string(FIND "${enq_leave}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "EnqueueLeave: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "op.kind = group_ops::OpKind::Join;" "op.bot = invitee->GetGUIDLow();" "op.other = inviter->GetGUIDLow();"
      "return group_ops::Enqueue(op, GetBotAI(invitee), GetBotAI(inviter)) == group_ops::Pushed::Queued;")
    string(FIND "${enq_join}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "EnqueueJoin: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # No mutation and no clock reset on the producer side (trackedGroup = 0 only once the bot is out of the group).
  check_none("${enq_leave}" "EnqueueLeave mutates or resets" r ${MUTATIONS} "trackedGroup = 0")
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_none("${enq_join}" "EnqueueJoin mutates" r ${MUTATIONS} "pendingKey")
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()

  region("${action}" "void AdhocGroupAction::Scan(uint32 now)" "\n}\n" scan)
  check_before("${scan}" "${JOIN_GATE}" "SendInvite(" "Scan: the OpQueue gate before the map-thread invite" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_before("${scan}" "adhoc_group::PairCooldowns().Block(" "${JOIN_GATE}" "Scan: the pair cooldown at enqueue" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  region("${scan}" "${JOIN_GATE}" "return;  // at most one invite per scan (queued or not)" branch)
  string(FIND "${branch}" "if (EnqueueJoin(inviter, invitee, key)" at)
  if(at EQUAL -1)
    set(${out} "Scan: the queued branch does not enqueue" PARENT_SCOPE)
    return()
  endif()
  check_none("${branch}" "Scan: the queued branch mutates" r ${MUTATIONS} "pendingKey")
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${scan}" "if (group_ops::StillPending(otherAi, other->GetGUIDLow()))" at)
  if(at EQUAL -1)
    set(${out} "Scan: a neighbour with a pending op is still a candidate" PARENT_SCOPE)
    return()
  endif()

  region("${action}" "bool AdhocGroupAction::Execute(Event& /*event*/)" "\n}\n" exec)
  foreach(later "RegisterPending(" "CheckLeave(" "Scan(now);")
    check_before("${exec}" "${PENDING_GATE}" "${later}" "Execute: the pending gate before ${later}" r)
    if(NOT r STREQUAL "")
      set(${out} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # (e) switch 0: the old path behind the gates, unchanged.
  foreach(needle "ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);" "            SendInvite(bot, other);"
      "            SendInvite(other, bot);" "        pendingKey = key;"
      "\"[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u\",")
    string(FIND "${action}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "switch 0: the old action path lost ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (b) the drain: PlayerbotWorldScript::OnUpdate only ------------------------------------------------------------
function(check_drain_site scripts all_sources out)
  region("${scripts}" "        void OnUpdate(uint32 diff) override" "\n        }\n" on_update)
  string(FIND "${on_update}" "sRandomPlayerbotMgr.RunQueuedRemoteCommands();" is_world)
  if(on_update STREQUAL "" OR is_world EQUAL -1)
    set(${out} "PlayerbotWorldScript::OnUpdate not found" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "                sRandomPlayerbotMgr.UpdateAI(diff);\n                ai::group_ops::Drain();   // BOT-12: the world-thread group queue\n                auctionbot.Update();\n                return;"
      "            sRandomPlayerbotMgr.UpdateAI(diff);\n            ai::group_ops::Drain();   // BOT-12: inside hook_ms, outside remote_ms and ahbot_ms\n            uint64 const ahbotStartUs")
    string(FIND "${on_update}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "OnUpdate: the drain is not right after the managers: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # Exactly these two calls in the whole module source.
  string(REGEX MATCHALL "group_ops::Drain\\(\\)" calls "${all_sources}")
  list(LENGTH calls n)
  if(NOT n EQUAL 2)
    set(${out} "group_ops::Drain() called ${n} times in the module (2 expected, both in PlayerbotWorldScript::OnUpdate)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

# ---- (c) one op per tick, none in a heavy tick; (d) the core paths and the deferred tail ----------------------------
function(check_drain ops out)
  region("${ops}" "\nvoid Drain()\n{" "\n}\n" drain)
  if(drain STREQUAL "")
    set(${out} "group_ops::Drain not found" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "    t_worldThread = true;"
      "    bool const heavy = w.heavyTick;\n    w.heavyTick = false;"
      "std::uint32_t const budget = OpsThisTick(sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick, heavy);"
      "std::uint32_t looks = LooksThisTick(budget);"
      "while (ran < budget && looks)"
      "                if (v == Verdict::Execute)\n                {\n                    ++ran;"
      "queue.Requeue(op);"
      "ClearPending(op);\n                queue.Finish(op);")
    string(FIND "${drain}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "Drain: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(REGEX MATCHALL "\\+\\+ran" incs "${drain}")   # no ';' in the pattern: a match with ';' splits the list
  list(LENGTH incs n)
  if(NOT n EQUAL 1)
    set(${out} "Drain: ran is counted ${n} times (only an executed op counts)" PARENT_SCOPE)
    return()
  endif()

  region("${ops}" "Verdict RunLeave(Op const& op, std::uint32_t ageMs, DrainMinute& m)" "\n}\n" run_leave)
  check_before("${run_leave}" "Verdict const v = DecideLeaveOp(f);\n    if (v != Verdict::Execute)\n        return v;"
    "HandleGroupDisbandOpcode" "RunLeave: the staleness rules before the core leave" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_before("${run_leave}" "deferred_log::Scope const deferLogs(ai, true);" "bot->GetSession()->HandleGroupDisbandOpcode(p);"
    "RunLeave: the leave lines go to the bot (forced scope)" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${run_leave}" "ai->RequestGroupAi(group_ai_refresh::LeaveReset);" at)
  if(at EQUAL -1)
    set(${out} "RunLeave: the leave reset is not posted to the bot" PARENT_SCOPE)
    return()
  endif()

  region("${ops}" "Verdict RunJoin(Op const& op, std::uint32_t ageMs, DrainMinute& m)" "\n}\n" run_join)
  check_before("${run_join}" "Verdict const v = DecideJoinOp(f);\n    if (v != Verdict::Execute)\n        return v;"
    "HandleGroupInviteOpcode" "RunJoin: the staleness rules before the core invite" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "inviter->GetSession()->HandleGroupInviteOpcode(p);"
      "inviteeAi->DoSpecificAction(\"accept invitation\", Event(group_ai_refresh::GroupOpSource, \"\", inviter), true);"
      "inviteeAi->PostDeferredLog(\"\", line);")
    string(FIND "${run_join}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "RunJoin: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  # No AI work on the world thread here, and no log line per op (only the minute line and the ALERT line).
  check_none("${ops}" "GroupOps.cpp does AI work on the world thread" r "ResetStrategies(" "ChangeStrategy(" "->Reset()"
    "DoSpecificAction(\"leave\"" "Event(\"adhoc group\"")
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  string(REGEX MATCHALL "sLog\\.out" logs "${ops}")
  list(LENGTH logs n)
  if(NOT n EQUAL 2)
    set(${out} "GroupOps.cpp: ${n} sLog calls (2 expected: the minute line and the ALERT line)" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_tail accept refresh out)
  check_before("${accept}"
    "                if (group_ai_refresh::AlwaysDeferTail(event.getSource()))\n                {\n                    ai->RequestJoinTail(inviter->GetGUIDLow());   // JoinReset + JoinTail, the inviter before the bits\n                    return true;\n                }"
    "                if (sPlayerbotAIConfig.botGroupsWorldThreadDefer)" "accept: the group-op tail deferred before the config check" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_before("${accept}" "if (group_ai_refresh::AlwaysDeferTail(event.getSource()))" "            JoinTail(grp, inviter, event);"
    "accept: the group-op accept returns before its tail" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  foreach(needle "    return source == \"adhoc group\" || source == \"quest group\" || source == GroupOpSource;"
      "inline bool AlwaysDeferTail(std::string const& source)\n{\n    return source == GroupOpSource;\n}")
    string(FIND "${refresh}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "GroupAiRefresh.h: missing ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out} "" PARENT_SCOPE)
endfunction()

function(check_heavy adhoc s7 policy out)
  foreach(src adhoc s7)
    string(FIND "${${src}}" "group_ops::MarkHeavyTick();" at)
    if(at EQUAL -1)
      set(${out} "${src}: no heavy-tick mark" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(needle "    if (minute.invites + minute.leaves != groupOpsBefore)\n        group_ops::MarkHeavyTick();")
    string(FIND "${adhoc}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "ad-hoc manager: the mark is not tied to its invites/leaves" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  string(FIND "${s7}" "    if (heavy)\n        group_ops::MarkHeavyTick();" at)
  if(at EQUAL -1)
    set(${out} "S7: the mark is not tied to its heavy step" PARENT_SCOPE)
    return()
  endif()
  string(FIND "${policy}" "    return heavyTaken ? 0u : ClampOpsPerTick(opsPerTick);" at)
  if(at EQUAL -1)
    set(${out} "OpsThisTick: not 0 in a heavy tick" PARENT_SCOPE)
    return()
  endif()
  set(${out} "" PARENT_SCOPE)
endfunction()

read_source("strategy/actions/AdhocGroupAction.cpp" action)
read_source("strategy/actions/AdhocGroupAction.h" action_h)
read_source("strategy/actions/AcceptInvitationAction.h" accept)
read_source("PlayerbotScripts.cpp" scripts)
read_source("GroupOps.cpp" ops)
read_source("GroupOps.h" ops_h)
read_source("GroupOpPolicy.h" policy)
read_source("GroupOpQueue.h" queue_h)
read_source("GroupAiRefresh.h" refresh)
read_source("AdhocGroupWorld.cpp" adhoc)
read_source("QuestGroupWorld.cpp" s7)
read_source("RandomPlayerbotMgr.cpp" rpm)
read_source("strategy/Engine.cpp" engine)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("PlayerbotAIConfig.h" config_h)
read_source("aiplayerbot.conf.dist.in" conf_dist)

file(GLOB_RECURSE module_sources "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
set(all_sources "")
foreach(f IN LISTS module_sources)
  file(READ "${f}" t)
  string(FIND "${t}" "group_ops::Drain()" has)
  if(NOT has EQUAL -1)
    string(APPEND all_sources "${t}")
  endif()
endforeach()

check_action("${action}" r)
fail_if("${r}")
check_drain_site("${scripts}" "${all_sources}" r)
fail_if("${r}")
check_drain("${ops}" r)
fail_if("${r}")
check_tail("${accept}" "${refresh}" r)
fail_if("${r}")
check_heavy("${adhoc}" "${s7}" "${policy}" r)
fail_if("${r}")

# (b) not in the throttled manager update.
string(FIND "${rpm}" "group_ops::Drain" at)
if(NOT at EQUAL -1)
  message(FATAL_ERROR "${TAG}: the drain in RandomPlayerbotMgr (throttled by RandomBotUpdateInterval)")
endif()

# The queue carries plain ops: no pointer crosses (the WorldThreadCommandQueue.h rule), no game header.
region("${queue_h}" "struct Op\n{" "\n};\n" op_struct)
string(FIND "${op_struct}" "*" star)
if(op_struct STREQUAL "" OR NOT star EQUAL -1)
  message(FATAL_ERROR "${TAG}: group_ops::Op must hold ids only, no pointer")
endif()
foreach(src queue_h policy)
  string(REGEX MATCH "#include \"[^\"]*(Player|Group/|playerbot\\.h|ObjectMgr|PlayerbotAI)[^\"]*\"" bad "${${src}}")
  if(bad)
    message(FATAL_ERROR "${TAG}: ${src} names a game header (${bad})")
  endif()
endforeach()

# The pending mark: set before the push (never left set after the op ran), cleared by seq on the world thread.
require_text("${ops}" "std::uint32_t const oldBot = botAi ? botAi->ExchangeGroupOpPending(op.seq) : 0u;" "pending mark before the push")
require_text("${ops}" "Pushed const r = Ops().Push(op, sPlayerbotAIConfig.botGroupsOpQueueMaxDepth);" "the depth limit on the push")
require_text("${ai_h}" "void ClearGroupOpPending(uint32 seq) { uint32 expected = seq; groupOpPending.compare_exchange_strong(expected, 0); }" "clear by seq")
require_text("${ai_h}" "std::atomic<uint32> groupOpPending{ 0 };" "the pending mark, 0 = none")
require_text("${action_h}" "uint32 opRetryAt = 0;" "the retry time after a refused push")

# Off-thread observer: a module GroupScript (no core hook), counters only, the world thread marked in Drain; the outermost
# action from Engine::ListenAndExecute.
foreach(needle "class GroupOpsObserver : public GroupScript"
    "if (group && !group->isBGGroup())\n            Observe(EvCreate);"
    "if (group && !group->isBGGroup())\n            Observe(EvDisband);"
    "    if (t_worldThread)\n"
    "OffSource const src = t_outerAction ? OffSourceOf(true, t_outerAction->getName()) : OffSource::Other;"
    "    return sPlayerbotAIConfig.botGroupsDiagnostics || sPlayerbotAIConfig.botGroupsOpQueue;")
  require_text("${ops}" "${needle}" "observer")
endforeach()
require_text("${scripts}" "    ai::group_ops::AddGroupOpsObserver();\n}" "observer registered in AddSC_playerbot_hooks")
check_before("${engine}" "ai::group_ops::ActionScope const groupOpScope(ai, action);" "action->Execute(event)"
  "the action scope around Execute" r)
fail_if("${r}")
require_text("${ops_h}" "    ActionScope(PlayerbotAI* ai, Action* action) : outer(t_outerAction == nullptr)" "the outermost action wins")

# The minute and ALERT lines: the effect lines the kits grep (ConfigEcho is cut at ~4093 chars).
foreach(needle "std::string line = \"[GroupOps] minute\";" "AddField(line, \"mode\", sPlayerbotAIConfig.botGroupsOpQueue);"
    "AddField(line, \"drain_us_max\", m.drainUsMax);" "AddField(line, \"depth_max\", pc.depthMax);"
    "AddField(line, \"age_ms_max\", m.ageMsMax);" "AddField(line, \"dropped_full_leave\", pc.droppedFull[L]);"
    "AddField(line, \"off_create\", off[EvCreate]);" "AddField(line, \"off_disband\", off[EvDisband]);"
    "\"[GroupOps] ALERT depth=%u oldest_age_ms=%u ops_per_tick=%u")
  require_text("${ops}" "${needle}" "minute/ALERT line")
endforeach()

# (e) config: keys, defaults, clamps, echo order, documented.
foreach(needle
    "botGroupsOpQueue = uint32(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueue\", 0));"
    "if (!ai::group_ops::ValidMode(botGroupsOpQueue))"
    "botGroupsOpQueueOpsPerTick = ai::group_ops::ClampOpsPerTick(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueOpsPerTick\", 1));"
    "botGroupsOpQueueMaxDepth = ai::group_ops::ClampMaxDepth(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueMaxDepth\", 2048));"
    "botGroupsOpQueueAlertAgeSeconds = ai::group_ops::ClampAlertAgeSeconds(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueAlertAgeSeconds\", 60));"
    "            {\"BotGroups.LeaderOtherMapSeconds\", Value(botGroupsLeaderOtherMapSeconds)},\n            {\"BotGroups.OpQueue\", Value(botGroupsOpQueue)},\n            {\"BotGroups.OpQueueAlertAgeSeconds\", Value(botGroupsOpQueueAlertAgeSeconds)},\n            {\"BotGroups.OpQueueMaxDepth\", Value(botGroupsOpQueueMaxDepth)},\n            {\"BotGroups.OpQueueOpsPerTick\", Value(botGroupsOpQueueOpsPerTick)},\n            {\"BotGroups.WorldThreadDefer\"")
  require_text("${config_cpp}" "${needle}" "config")
endforeach()
foreach(needle "uint32 botGroupsOpQueue = 0;" "uint32 botGroupsOpQueueOpsPerTick = 1;" "uint32 botGroupsOpQueueMaxDepth = 2048;"
    "uint32 botGroupsOpQueueAlertAgeSeconds = 60;")
  require_text("${config_h}" "${needle}" "config member default")
endforeach()
foreach(needle "\nAiPlayerbot.BotGroups.OpQueue = 0\n" "\nAiPlayerbot.BotGroups.OpQueueOpsPerTick = 1\n"
    "\nAiPlayerbot.BotGroups.OpQueueMaxDepth = 2048\n" "\nAiPlayerbot.BotGroups.OpQueueAlertAgeSeconds = 60\n"
    "# BOT-12 (twow-repo#677, #628; OB-00 04.11.2026): the world-thread group queue.")
  require_text("${conf_dist}" "${needle}" "conf.dist")
endforeach()
foreach(needle "constexpr std::uint32_t kOpsPerTickMin = 1;" "constexpr std::uint32_t kOpsPerTickMax = 16;"
    "    return mode <= cohort::ModeOnlyA;" "    return cohort::Applies(SanitizeMode(mode), guidLow);")
  require_text("${policy}" "${needle}" "policy")
endforeach()

# ---- negative probes: each check catches its defect -------------------------------------------------------------------
function(expect_caught what reason)
  if("${reason}" STREQUAL "")
    message(FATAL_ERROR "${TAG}: negative probe not caught: ${what}")
  endif()
endfunction()

# The leave gate dropped: the old map-thread path runs for queue bots.
string(REPLACE "${LEAVE_GATE}\n" "" probe "${action}")
check_action("${probe}" r)
expect_caught("leave gate removed" "${r}")
# The gate moved behind the old leave call.
string(REPLACE "${LEAVE_GATE}\n" "" probe "${action}")
string(REPLACE "    ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);\n" "    ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);\n${LEAVE_GATE}\n" probe "${probe}")
check_action("${probe}" r)
expect_caught("leave gate after the map-thread leave" "${r}")
# A mutation in the producer: the old core leave inside EnqueueLeave.
string(REPLACE "    if (sPlayerbotAIConfig.botGroupsDiagnostics)\n        sLog.outBasic(\"[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u via=queue\","
  "    bot->GetSession()->HandleGroupDisbandOpcode(p);\n    if (sPlayerbotAIConfig.botGroupsDiagnostics)\n        sLog.outBasic(\"[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u via=queue\"," probe "${action}")
check_action("${probe}" r)
expect_caught("core leave in EnqueueLeave" "${r}")
# The clock reset on enqueue (the CAUSE doc's repeated decisions).
string(REPLACE "        return false;\n    }\n\n    if (sPlayerbotAIConfig.botGroupsDiagnostics)\n        sLog.outBasic(\"[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u via=queue\","
  "        return false;\n    }\n    trackedGroup = 0;\n\n    if (sPlayerbotAIConfig.botGroupsDiagnostics)\n        sLog.outBasic(\"[BotGroup] kind=adhoc event=leave bot=%u group=%u quest=%u objective=%u reason=%s members=%u via=queue\"," probe "${action}")
check_action("${probe}" r)
expect_caught("trackedGroup reset in EnqueueLeave" "${r}")
# The queued join branch sending the invite itself.
string(REPLACE "            if (EnqueueJoin(inviter, invitee, key)" "            SendInvite(inviter, invitee);\n            if (EnqueueJoin(inviter, invitee, key)" probe "${action}")
check_action("${probe}" r)
expect_caught("map-thread invite in the queued branch" "${r}")
# The pending gate behind the scan.
string(REPLACE "${PENDING_GATE}\n" "" probe "${action}")
string(REPLACE "    Scan(now);\n" "${PENDING_GATE}\n    Scan(now);\n" probe "${probe}")
check_action("${probe}" r)
expect_caught("pending gate after RegisterPending/CheckLeave" "${r}")
# A third drain call.
check_drain_site("${scripts}" "${all_sources}\nai::group_ops::Drain();" r)
expect_caught("drain called elsewhere" "${r}")
# The drain before the managers.
string(REPLACE "                sRandomPlayerbotMgr.UpdateAI(diff);\n                ai::group_ops::Drain();" "                ai::group_ops::Drain();\n                sRandomPlayerbotMgr.UpdateAI(diff);" probe "${scripts}")
check_drain_site("${probe}" "${all_sources}" r)
expect_caught("drain without the managers before it" "${r}")
# The heavy flag ignored.
string(REPLACE "OpsThisTick(sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick, heavy)" "OpsThisTick(sPlayerbotAIConfig.botGroupsOpQueueOpsPerTick, false)" probe "${ops}")
check_drain("${probe}" r)
expect_caught("drain ignores the heavy tick" "${r}")
# Every popped entry counted as a run op (no op cap).
string(REPLACE "                if (popped == Popped::Superseded)\n                {\n" "                if (popped == Popped::Superseded)\n                {\n                    ++ran;\n" probe "${ops}")
check_drain("${probe}" r)
expect_caught("ran counted for skipped entries" "${r}")
# The leave lines written on the world thread (scope off).
string(REPLACE "deferred_log::Scope const deferLogs(ai, true);" "deferred_log::Scope const deferLogs(ai, sPlayerbotAIConfig.botGroupsWorldThreadDefer);" probe "${ops}")
check_drain("${probe}" r)
expect_caught("leave lines not forced to the bot" "${r}")
# A strategy reset on the world thread.
string(REPLACE "    ai->RequestGroupAi(group_ai_refresh::LeaveReset);" "    ai->ResetStrategies();\n    ai->RequestGroupAi(group_ai_refresh::LeaveReset);" probe "${ops}")
check_drain("${probe}" r)
expect_caught("strategy reset in GroupOps.cpp" "${r}")
# The queue's accept with a manager source (the tail would follow WorldThreadDefer).
string(REPLACE "Event(group_ai_refresh::GroupOpSource, \"\", inviter)" "Event(\"adhoc group\", \"\", inviter)" probe "${ops}")
check_drain("${probe}" r)
expect_caught("queue accept without the group-op source" "${r}")
# A log line per op.
string(REPLACE "            inviteeAi->PostDeferredLog(\"\", line);" "            sLog.outBasic(\"%s\", line);" probe "${ops}")
check_drain("${probe}" r)
expect_caught("a log line per op on the world thread" "${r}")
# The forced tail removed from the accept.
string(REPLACE "                if (group_ai_refresh::AlwaysDeferTail(event.getSource()))" "                if (false)" probe "${accept}")
check_tail("${probe}" "${refresh}" r)
expect_caught("accept tail not forced" "${r}")
string(REPLACE "    return source == GroupOpSource;\n}" "    return false;\n}" probe "${refresh}")
check_tail("${accept}" "${probe}" r)
expect_caught("AlwaysDeferTail always false" "${r}")
# The heavy marks gone.
string(REPLACE "    if (heavy)\n        group_ops::MarkHeavyTick();" "" probe "${s7}")
check_heavy("${adhoc}" "${probe}" "${policy}" r)
expect_caught("S7 without the heavy mark" "${r}")
string(REPLACE "    return heavyTaken ? 0u : ClampOpsPerTick(opsPerTick);" "    return ClampOpsPerTick(opsPerTick);" probe "${policy}")
check_heavy("${adhoc}" "${s7}" "${probe}" r)
expect_caught("OpsThisTick ignores the heavy tick" "${r}")

message(STATUS "BOT12_GROUP_OP_QUEUE_SOURCE_CONTRACT=PASS")
