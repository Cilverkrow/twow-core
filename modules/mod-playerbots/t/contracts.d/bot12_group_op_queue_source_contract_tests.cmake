cmake_policy(VERSION 3.16)
# BOT-12 (OB-00 04.11.2026; twow-repo#677, #628; design cli-grp/crossmap/DESIGN-GROUPOPS-WORLDTHREAD-20261104.md 3 and 5, PR1):
# AiPlayerbot.BotGroups.OpQueue (0 off = rollback, 1 all = default since owner 08.11.2026, 2 cohort B, 3 cohort A).
# ObjectMgr::m_GroupMap is an unlocked map
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

# A decided leave ends the Execute whether the push was queued or refused (no scan for invites by a bot that wants to leave).
set(LEAVE_GATE "    if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, bot->GetGUIDLow()))\n    {\n        EnqueueLeave(group, entry, reason, now);\n        return true;\n    }")
# OB-00 04.11.: a join is queued when the mode applies to either bot (inviter or invitee).
set(JOIN_GATE "        if (group_ops::JoinQueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, inviter->GetGUIDLow(), invitee->GetGUIDLow()))")
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
  string(FIND "${scan}" "if (group_ops::PendingHint(otherAi, other->GetGUIDLow()))" at)
  if(at EQUAL -1)
    set(${out} "Scan: a neighbour with a pending op is still a candidate" PARENT_SCOPE)
    return()
  endif()
  # A neighbour's scan reads the mark only; clearing a stale mark is the owner's job (StillPending in its own Execute).
  string(FIND "${scan}" "StillPending(" at)
  if(NOT at EQUAL -1)
    set(${out} "Scan: StillPending (which clears marks) on a neighbour" PARENT_SCOPE)
    return()
  endif()
  # The old path tags the invitee before its invite, so its accept (the group create) is attributed to the ad-hoc path.
  region("${action}" "void AdhocGroupAction::SendInvite(Player* inviter, Player* invitee) const" "\n}\n" send)
  check_before("${send}" "group_ops::NoteAdhocInvite(GetBotAI(invitee), inviter->GetGUIDLow());" "HandleGroupInviteOpcode"
    "SendInvite: the ad-hoc invite tag before the invite" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
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
  # Exactly these two calls in the whole module source - qualified, or unqualified inside namespace group_ops (any Drain()
  # call that is not a definition `void Drain()` or another class's `X::Drain()`).
  string(REGEX MATCHALL "group_ops::Drain\\(\\)" calls "${all_sources}")
  list(LENGTH calls n)
  if(NOT n EQUAL 2)
    set(${out} "group_ops::Drain() called ${n} times in the module (2 expected, both in PlayerbotWorldScript::OnUpdate)" PARENT_SCOPE)
    return()
  endif()
  # ops_sources: GroupOps.cpp + GroupOps.h (set before the calls; CMake functions see the caller's variables).
  string(REPLACE "void Drain()" "" ops_calls "${ops_sources}")
  string(REGEX MATCHALL "(^|[^A-Za-z_:])Drain\\(\\)" bare "${ops_calls}")
  list(LENGTH bare nb)
  if(NOT nb EQUAL 0)
    set(${out} "GroupOps.cpp/.h: ${nb} unqualified Drain() call(s) inside namespace group_ops (only the two in OnUpdate)" PARENT_SCOPE)
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
  # The switch-0 fast path (no lock, no clock) comes after the heavy flag is consumed and the world thread is marked.
  check_before("${drain}" "    bool const heavy = w.heavyTick;\n    w.heavyTick = false;"
    "    if (!queue.DepthHint() && !ObserverOn())\n        return;" "Drain: the heavy flag consumed before the fast path" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  foreach(needle
      "    t_worldThread = true;"
      "    bool const heavy = w.heavyTick;\n    w.heavyTick = false;"
      "    if (!queue.DepthHint() && !ObserverOn())\n        return;"
      "                    if (!op.inTransit)\n                    {\n                        op.inTransit = 1;\n                        op.transitSinceMs = nowMs;\n                    }"
      "std::uint32_t const transitMs = op.inTransit ? nowMs - op.transitSinceMs : 0u;"
      "++m.ageHist[AgeBucket(ageMs)];"
      "if (!w.alertChecked || nowMs - w.lastAlertCheckMs >= kAlertCheckIntervalMs)"
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

  region("${ops}" "Verdict RunLeave(Op const& op, std::uint32_t transitMs, DrainMinute& m)" "\n}\n" run_leave)
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
  # The registry forgets the group only once the bot is out (a refused core leave keeps the group ad-hoc).
  check_before("${run_leave}" "bot->GetSession()->HandleGroupDisbandOpcode(p);" "adhoc_group::Groups().Forget(groupId);"
    "RunLeave: Forget only after the core leave" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()
  check_before("${run_leave}" "        ++m.failed[std::size_t(OpKind::Leave)];\n        return v;\n    }\n    if (lastPair)\n        adhoc_group::Groups().Forget(groupId);"
    "adhoc_group::Groups().Forget(groupId);" "RunLeave: a failed leave returns before Forget" r)
  if(NOT r STREQUAL "")
    set(${out} "${r}" PARENT_SCOPE)
    return()
  endif()

  region("${ops}" "Verdict RunJoin(Op const& op, std::uint32_t transitMs, DrainMinute& m)" "\n}\n" run_join)
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
  # A3 lever 2 (BotGroups.WritePath): + the [GroupWrite] minute line (only with that key on; a3l2 contract).
  string(REGEX MATCHALL "sLog\\.out" logs "${ops}")
  list(LENGTH logs n)
  if(NOT n EQUAL 3)
    set(${out} "GroupOps.cpp: ${n} sLog calls (3 expected: the minute line, the [GroupWrite] minute line and the ALERT line)" PARENT_SCOPE)
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
  # Design 3.2: the ad-hoc manager marks every update that ran a scan slice (unconditionally, at the end of its Update).
  string(FIND "${adhoc}" "    // world tick's heavy group step; the group queue runs no op in the same tick.\n    group_ops::MarkHeavyTick();\n}" at)
  if(at EQUAL -1)
    set(${out} "ad-hoc manager: the mark is not unconditional at the end of its update (a scan slice is heavy)" PARENT_SCOPE)
    return()
  endif()
  # S7: a dissolution, a group operation or a scan slice.
  foreach(needle "    if (heavy || scanned)\n        group_ops::MarkHeavyTick();"
      "    if (round.active && !HeavyStepTaken(oneStep, heavy))\n    {\n        scanned = true;")
    string(FIND "${s7}" "${needle}" at)
    if(at EQUAL -1)
      set(${out} "S7: the mark is not tied to its heavy step and its scan slice: ${needle}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
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
set(ops_sources "${ops}\n${ops_h}")

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
# The mark is a hint: the owner clears a stale mark by the seq it read (a newer mark survives); a neighbour only reads it.
region("${ops}" "bool StillPending(PlayerbotAI* ai, std::uint32_t guidLow)\n{" "\n}\n" still_pending)
region("${ops}" "bool PendingHint(PlayerbotAI* ai, std::uint32_t guidLow)\n{" "\n}\n" pending_hint)
foreach(needle "    std::uint32_t const seq = ai->GroupOpPendingSeq();" "    ai->ClearGroupOpPending(seq);")
  require_text("${still_pending}" "${needle}" "StillPending clears by the seq it read")
endforeach()
require_text("${pending_hint}" "    return ai->GroupOpPending() && Ops().IsPending(guidLow);" "PendingHint reads only")
foreach(src ops ai_h)
  string(FIND "${${src}}" "ClearGroupOpPendingAny" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "${TAG}: ${src}: ClearGroupOpPendingAny (an unconditional clear can drop a newer mark)")
  endif()
endforeach()

# Off-thread observer: a module GroupScript (no core hook), counters only, the world thread marked in Drain; the outermost
# action from Engine::ListenAndExecute; the old path's ad-hoc accept counted as the ad-hoc path; queue bots per path.
foreach(needle "class GroupOpsObserver : public GroupScript"
    "if (group && !group->isBGGroup())\n            Observe(EvCreate, group);"
    "if (group && !group->isBGGroup())\n            Observe(EvDisband, group);"
    "if (group && !group->isBGGroup())\n            Observe(EvAdd, group, guid.GetCounter());"
    "if (group && !group->isBGGroup())\n            Observe(EvLeader, group);"
    "if (group && !group->isBGGroup())\n            Observe(EvInvite, group);"
    "    if (t_worldThread)\n"
    "        adhocInvite = tag && tag == group->GetLeaderGuid().GetCounter();"
    "OffSource const src = OffSourceOf(t_outerAction != nullptr, action, adhocInvite);"
    "    if (actorBot && QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, actorBot->GetGUIDLow()))\n        offCounts.byQueueBot[std::size_t(src)].fetch_add(1, std::memory_order_relaxed);"
    "    return sPlayerbotAIConfig.botGroupsDiagnostics || sPlayerbotAIConfig.botGroupsOpQueue;"
    "    if (inviteeAi && ObserverOn())\n        inviteeAi->SetAdhocInviteFrom(inviterGuidLow);")
  require_text("${ops}" "${needle}" "observer")
endforeach()
require_text("${policy}" "        return adhocInvite ? OffSource::Adhoc : OffSource::Accept;" "the ad-hoc accept is the ad-hoc path")
require_text("${policy}" "    return QueueApplies(mode, inviterLow) || QueueApplies(mode, inviteeLow);" "a join queued when the mode applies to either bot")
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
    "AddField(line, \"age_ms_p99\", AgeP99Ms(m.ageHist, m.ageMsMax));" "AddField(line, \"off_leader\", off[EvLeader]);"
    "AddField(line, \"off_invite\", off[EvInvite]);" "AddField(line, \"off_map_real\", real);"
    "AddField(line, (\"off_map_\" + std::string(OffSourceName(OffSource(i)))).c_str(), bySource[i]);"
    "AddField(line, (\"off_q_\" + std::string(OffSourceName(OffSource(i)))).c_str(), byQueueBot[i]);"
    "\"[GroupOps] ALERT depth=%u oldest_age_ms=%u ops_per_tick=%u")
  require_text("${ops}" "${needle}" "minute/ALERT line")
endforeach()

# (e) config: keys, defaults, clamps, echo order, documented.
# Owner 08.11.2026 (BOT-12 v2 RESULT 24b75ea3): code default 1, 0 = rollback, an unknown value = 1 (one error line).
set(OQ_LOAD "botGroupsOpQueue = uint32(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueue\", 1));\n    if (!ai::group_ops::ValidMode(botGroupsOpQueue))\n    {\n        sLog.outError(\"[GroupOps] config key=OpQueue value=%u result=1 (0 off = rollback, 1 all, 2 cohort B, 3 cohort A)\", botGroupsOpQueue);\n        botGroupsOpQueue = 1;\n    }")
function(check_oq_load cpp out)
  string(FIND "${cpp}" "${OQ_LOAD}" at)
  if(at EQUAL -1)
    set(${out} "OpQueue load: default 1 / unknown = 1 missing" PARENT_SCOPE)
  else()
    set(${out} "" PARENT_SCOPE)
  endif()
endfunction()
check_oq_load("${config_cpp}" r)
fail_if("${r}")
# The sorted [ConfigEcho] run of the BotGroups keys (T33-0 adds BotGroups.Manager.Gather in its sorted place).
foreach(needle
    "botGroupsOpQueueOpsPerTick = ai::group_ops::ClampOpsPerTick(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueOpsPerTick\", 1));"
    "botGroupsOpQueueMaxDepth = ai::group_ops::ClampMaxDepth(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueMaxDepth\", 2048));"
    "botGroupsOpQueueAlertAgeSeconds = ai::group_ops::ClampAlertAgeSeconds(config.GetIntDefault(\"AiPlayerbot.BotGroups.OpQueueAlertAgeSeconds\", 60));"
    "            {\"BotGroups.LeaderOtherMapSeconds\", Value(botGroupsLeaderOtherMapSeconds)},\n            {\"BotGroups.Manager\", Value(botGroupsManager)},\n            {\"BotGroups.Manager.AckTimeoutSeconds\", Value(botGroupsManagerAckTimeoutSeconds)},\n            {\"BotGroups.Manager.Gather\", Value(botGroupsManagerGather)},\n            {\"BotGroups.Manager.StatusSeconds\", Value(botGroupsManagerStatusSeconds)},\n            {\"BotGroups.Manager.Types\", Value(botGroupsManagerTypes)},\n            {\"BotGroups.OpQueue\", Value(botGroupsOpQueue)},\n            {\"BotGroups.OpQueueAlertAgeSeconds\", Value(botGroupsOpQueueAlertAgeSeconds)},\n            {\"BotGroups.OpQueueMaxDepth\", Value(botGroupsOpQueueMaxDepth)},\n            {\"BotGroups.OpQueueOpsPerTick\", Value(botGroupsOpQueueOpsPerTick)},\n            {\"BotGroups.ResetRaidsRealInviterOnly\", Value(botGroupsResetRaidsRealInviterOnly)},\n            {\"BotGroups.WorldThreadDefer\"")
  require_text("${config_cpp}" "${needle}" "config")
endforeach()
foreach(needle "uint32 botGroupsOpQueue = 1;" "uint32 botGroupsOpQueueOpsPerTick = 1;" "uint32 botGroupsOpQueueMaxDepth = 2048;"
    "uint32 botGroupsOpQueueAlertAgeSeconds = 60;")
  require_text("${config_h}" "${needle}" "config member default")
endforeach()
# Owner 08.11.2026: the queue is on by default (BEHAVIOUR CHANGE text, 0 = rollback, unknown = 1).
foreach(needle "\nAiPlayerbot.BotGroups.OpQueue = 1\n" "\nAiPlayerbot.BotGroups.OpQueueOpsPerTick = 1\n"
    "# BEHAVIOUR CHANGE, ON by default (owner 08.11.2026; BOT-12 v2 RESULT 24b75ea3"
    "# ms), 0 = rollback. Mode (in-realm A/B" "Any other value acts like 1 (one error line\n# at load).\nAiPlayerbot.BotGroups.OpQueue = 1\n"
    "\nAiPlayerbot.BotGroups.OpQueueMaxDepth = 2048\n" "\nAiPlayerbot.BotGroups.OpQueueAlertAgeSeconds = 60\n"
    "# BOT-12 (twow-repo#677, #628; OB-00 04.11.2026): the world-thread group queue."
    "# Scope: only the ad-hoc ACTION path (AdHoc.WorldThread = 0)."
    "a pair is queued when the mode applies to either bot"
    "kits set both keys")
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
string(REPLACE "    if (heavy || scanned)\n        group_ops::MarkHeavyTick();" "" probe "${s7}")
check_heavy("${adhoc}" "${probe}" "${policy}" r)
expect_caught("S7 without the heavy mark" "${r}")
string(REPLACE "    if (heavy || scanned)\n" "    if (heavy)\n" probe "${s7}")
check_heavy("${adhoc}" "${probe}" "${policy}" r)
expect_caught("S7 scan slice not heavy" "${r}")
string(REPLACE "    // world tick's heavy group step; the group queue runs no op in the same tick.\n    group_ops::MarkHeavyTick();\n}"
  "    // world tick's heavy group step; the group queue runs no op in the same tick.\n    if (minute.invites)\n        group_ops::MarkHeavyTick();\n}" probe "${adhoc}")
check_heavy("${probe}" "${s7}" "${policy}" r)
expect_caught("ad-hoc manager mark conditional (scan slice not heavy)" "${r}")
# The fast path before the heavy flag is consumed (a stale heavy flag would skip a later tick).
string(REPLACE "    bool const heavy = w.heavyTick;\n    w.heavyTick = false;\n" "" probe "${ops}")
string(REPLACE "    Queue& queue = Ops();\n    // Switch 0" "    Queue& queue = Ops();\n    if (!queue.DepthHint() && !ObserverOn())\n        return;\n    bool const heavy = w.heavyTick;\n    w.heavyTick = false;\n    // Switch 0" probe "${probe}")
check_drain("${probe}" r)
expect_caught("fast path before the heavy flag is consumed" "${r}")
# The transit limit from the enqueue time again.
string(REPLACE "std::uint32_t const transitMs = op.inTransit ? nowMs - op.transitSinceMs : 0u;" "std::uint32_t const transitMs = ageMs;" probe "${ops}")
check_drain("${probe}" r)
expect_caught("transit limit counted from the enqueue" "${r}")
# Forget before the core leave (a refused leave would drop the ad-hoc registry entry).
string(REPLACE "    if (lastPair)\n        adhoc_group::Groups().Forget(groupId);\n" "" probe "${ops}")
string(REPLACE "    bool const lastPair = group->GetMembersCount() <= 2;\n" "    bool const lastPair = group->GetMembersCount() <= 2;\n    if (lastPair)\n        adhoc_group::Groups().Forget(groupId);\n" probe "${probe}")
check_drain("${probe}" r)
expect_caught("Forget before the core leave" "${r}")
# Switch 0: the old action path deleted (each of its needles).
foreach(old "            SendInvite(other, bot);\n" "            SendInvite(bot, other);\n" "        pendingKey = key;\n"
    "    ai->DoSpecificAction(\"leave\", Event(\"adhoc group\", \"\", bot), true);\n")
  string(REPLACE "${old}" "" probe "${action}")
  check_action("${probe}" r)
  expect_caught("switch 0: old path lost ${old}" "${r}")
endforeach()
# The join gate back to "the invitee's cohort decides" (a queue-cohort inviter would invite on its map thread).
string(REPLACE "${JOIN_GATE}" "        if (group_ops::QueueApplies(sPlayerbotAIConfig.botGroupsOpQueue, invitee->GetGUIDLow()))" probe "${action}")
check_action("${probe}" r)
expect_caught("join gate on the invitee only" "${r}")
# A refused leave falling through to the scan.
string(REPLACE "        EnqueueLeave(group, entry, reason, now);\n        return true;" "        if (!EnqueueLeave(group, entry, reason, now))\n            return false;\n        return true;" probe "${action}")
check_action("${probe}" r)
expect_caught("refused leave falls through to Scan" "${r}")
# A neighbour's scan clearing marks.
string(REPLACE "group_ops::PendingHint(otherAi, other->GetGUIDLow())" "group_ops::StillPending(otherAi, other->GetGUIDLow())" probe "${action}")
check_action("${probe}" r)
expect_caught("neighbour scan clears marks" "${r}")
# The ad-hoc invite tag dropped (its accept would count as a PR2/PR3 accept).
string(REPLACE "    group_ops::NoteAdhocInvite(GetBotAI(invitee), inviter->GetGUIDLow());\n" "" probe "${action}")
check_action("${probe}" r)
expect_caught("ad-hoc invite tag missing" "${r}")
# An unqualified Drain() call inside namespace group_ops.
set(saved_ops_sources "${ops_sources}")
set(ops_sources "${ops_sources}\nvoid MarkHeavyTick()\n{\n    Drain();\n}\n")
check_drain_site("${scripts}" "${all_sources}" r)
expect_caught("unqualified Drain() call" "${r}")
set(ops_sources "${saved_ops_sources}")
check_drain_site("${scripts}" "${all_sources}" r)
fail_if("${r}")
string(REPLACE "    return heavyTaken ? 0u : ClampOpsPerTick(opsPerTick);" "    return ClampOpsPerTick(opsPerTick);" probe "${policy}")
check_heavy("${adhoc}" "${s7}" "${probe}" r)
expect_caught("OpsThisTick ignores the heavy tick" "${r}")
# Owner default 1 (08.11.2026): the code default back to 0, or an unknown value off again.
string(REPLACE "AiPlayerbot.BotGroups.OpQueue\", 1));" "AiPlayerbot.BotGroups.OpQueue\", 0));" probe "${config_cpp}")
check_oq_load("${probe}" r)
expect_caught("OpQueue code default 0" "${r}")
string(REPLACE "        botGroupsOpQueue = 1;\n    }" "        botGroupsOpQueue = 0;\n    }" probe "${config_cpp}")
check_oq_load("${probe}" r)
expect_caught("OpQueue unknown value = 0" "${r}")

message(STATUS "BOT12_GROUP_OP_QUEUE_SOURCE_CONTRACT=PASS")
