cmake_policy(VERSION 3.16)
# twow-repo#627/#628 (s7r 26.10.2026, STOP-HARM at the scan guard; OB-00 go 27.10.): the world-thread group managers' tick
# budget. (a) QuestGroups.OneHeavyStep: one heavy step per S7 update. (b) BotGroups.WorldThreadDefer: the accept tail and the
# event / membership log lines of a join or leave on the bot's own thread. (c) QuestGroups.ScanBudgetUs /
# BotGroups.AdHoc.ScanBudgetUs: a slice ends after its time budget. (d) tend_us_max, accept/join/leave core times and
# stall_us_max in the minute lines. All keys default 0 = the behaviour before; (d) is measurement only.

if(NOT DEFINED PB_SOURCE_DIR)
  message(FATAL_ERROR "s7 tick budget: PB_SOURCE_DIR is required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "s7 tick budget: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "s7 tick budget: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("strategy/actions/AcceptInvitationAction.h" accept)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("BotGroupDiagnostics.cpp" diag)
read_source("QuestGroupWorld.cpp" qgw)
read_source("AdhocGroupWorld.cpp" agw)
read_source("GroupAiRefresh.h" refresh)

# 1. Keys: default 0, documented with 0, echoed in sorted position.
foreach(member "bool botGroupsWorldThreadDefer = false;" "uint32 botGroupsAdhocScanBudgetUs = 0;"
               "uint32 questGroupsScanBudgetUs = 0;" "bool questGroupsOneHeavyStep = false;")
  require_text("${config_h}" "${member}" "member default")
endforeach()
require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.BotGroups.WorldThreadDefer\", false)" "WorldThreadDefer default 0")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.BotGroups.AdHoc.ScanBudgetUs\", 0)" "AdHoc.ScanBudgetUs default 0")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.QuestGroups.ScanBudgetUs\", 0)" "QuestGroups.ScanBudgetUs default 0")
require_text("${config_cpp}" "config.GetBoolDefault(\"AiPlayerbot.QuestGroups.OneHeavyStep\", false)" "OneHeavyStep default 0")
foreach(line "AiPlayerbot.BotGroups.WorldThreadDefer = 0" "AiPlayerbot.BotGroups.AdHoc.ScanBudgetUs = 0"
             "AiPlayerbot.QuestGroups.ScanBudgetUs = 0" "AiPlayerbot.QuestGroups.OneHeavyStep = 0")
  require_text("${conf_dist}" "${line}" "documented off")
endforeach()
require_order("${config_cpp}" "{\"BotGroups.AdHoc.HelpUntilDone\"" "{\"BotGroups.AdHoc.ScanBudgetUs\"" "echo order")
require_order("${config_cpp}" "{\"BotGroups.AdHoc.ScanBudgetUs\"" "{\"BotGroups.AdHoc.ScanSlices\"" "echo order")
require_order("${config_cpp}" "{\"BotGroups.Enabled\"" "{\"BotGroups.WorldThreadDefer\"" "echo order")
require_order("${config_cpp}" "{\"QuestGroups.Enable\"" "{\"QuestGroups.OneHeavyStep\"" "echo order")
require_order("${config_cpp}" "{\"QuestGroups.OneHeavyStep\"" "{\"QuestGroups.ScanBudgetUs\"" "echo order")
require_order("${config_cpp}" "{\"QuestGroups.ScanBudgetUs\"" "{\"QuestGroups.ScanSlices\"" "echo order")

# 2. (b) The accept: the posted tail first; the core join timed alone; a deferred world-manager accept returns before its tail.
require_order("${accept}" "if (event.getSource() == group_ai_refresh::JoinTailSource)" "Group* grp = bot->GetGroupInvite();" "tail branch first")
require_order("${accept}" "auto const coreStart = std::chrono::steady_clock::now();
            bot->GetSession()->HandleGroupAcceptOpcode(p);
            group_op_timing::lastAcceptCoreUs = group_op_timing::UsSince(coreStart);" "if (!bot->GetGroup() || !bot->GetGroup()->IsMember(inviter->GetObjectGuid()))" "core join timed alone")
require_order("${accept}" "                if (sPlayerbotAIConfig.botGroupsWorldThreadDefer)
                {
                    ai->RequestJoinTail(inviter->GetGUIDLow());   // JoinReset + JoinTail, the inviter before the bits
                    return true;
                }" "            JoinTail(grp, inviter, event);" "deferred accept returns before the tail")
require_order("${accept}" "        void JoinTail(Group* grp, Player* inviter, Event& event, bool onBotThread = false)" "            ai->DoSpecificAction(\"update gear\", event, true);" "update gear is part of the tail")
require_text("${refresh}" "        pending &= ~std::uint32_t(JoinReset | JoinTail | Follower | TurnIn | ForceTravel);" "a leave drops a pending tail")

# The bot thread: the tail after the reset and before the S7 set; the deferred lines right after the requests.
require_order("${ai_cpp}" "        Reset();
    }
    // s7r (BotGroups.WorldThreadDefer): the accept tail" "    if (plan.follower || plan.turnIn)" "tail after the reset, before the set")
require_order("${ai_cpp}" "    DrainDeferredLogs();
    ApplyGroupAiRequests();" "    // #420: close a finished [GroupBuff] window." "deferred lines drained before the requests (older first)")
require_text("${ai_cpp}" "        if (inviter && bot->GetGroup()->IsMember(inviter->GetObjectGuid()))" "the tail only for the inviter, still in the group")
require_text("${ai_cpp}" "    if (!hasDeferredLogs.exchange(false))" "the drain is free without lines")
# Logs: inside a scope the line goes to the bot - before the shared mutex and the fflush.
require_order("${config_cpp}" "    if (PlayerbotAI* target = ai::deferred_log::Target())" "    std::lock_guard<std::mutex> guard(m_logMtx);" "log() defers before the lock")
require_text("${diag}" "        target->PostDeferredLog(\"\", line);" "membership line deferred")
foreach(src qgw agw)
  require_order("${${src}}" "deferred_log::Scope const deferLogs(ai, sPlayerbotAIConfig.botGroupsWorldThreadDefer);" "->GetSession()->HandleGroupDisbandOpcode(p);" "${src}: leave lines in a scope before the core leave")
  require_order("${${src}}" "auto const coreStart = std::chrono::steady_clock::now();" "group_op_timing::lastLeaveCoreUs = group_op_timing::UsSince(coreStart);" "${src}: core leave timed")
endforeach()

# 3. (a) One heavy step: carried dissolutions not tended again; dissolve, operation and scan each behind HeavyStepTaken.
require_order("${qgw}" "    ended.swap(dissolveLater);" "        if (carried.count(id))" "carried dissolutions first, not tended")
require_order("${qgw}" "        if (HeavyStepTaken(oneStep, heavy))
        {
            dissolveLater.emplace_back(id, d);" "        DissolveGroup(bots, it->second, d, now);" "one dissolution per update")
require_text("${qgw}" "    if (!HeavyStepTaken(oneStep, heavy) && (!forming.empty() || !releasing.empty()))
        heavy = StepOne(bots, now);" "no operation after a dissolution")
require_text("${qgw}" "    if (round.active && !HeavyStepTaken(oneStep, heavy))" "no scan slice after a heavy step")

# 4. (c) Scan budget at the head of both slice loops (at least one bot per update: BudgetSpent).
require_order("${qgw}" "    for (; round.next < end; ++round.next)
    {
        if (scan_slice::BudgetSpent(budgetUs, budgetUs ? UsSince(sliceStart) : 0u, round.next - first))
            break;" "        std::uint32_t const guid = round.guids[round.next];" "S7 slice budget")
require_order("${agw}" "    for (; pass.next < end; ++pass.next)
    {
        if (scan_slice::BudgetSpent(budgetUs, budgetUs ? UsSince(sliceStart) : 0u, pass.next - first))
            break;" "        std::uint32_t const guid = pass.guids[pass.next];" "ad-hoc slice budget")

# 5. (d) Measurement in the minute lines.
require_text("${qgw}" " tend_us_max=%u accept_core_us_max=%u leave_core_us_max=%u stall_us_max=%u one_step=%u scan_budget_us=%u defer=%u" "S7 minute fields")
require_text("${agw}" " join_core_us_max=%u leave_core_us_max=%u stall_us_max=%u scan_budget_us=%u defer=%u" "ad-hoc minute fields")
require_text("${qgw}" "    minute.stallUsMax = std::max(minute.stallUsMax, thread_cpu::StallUs(wallUs, cpuStart, thread_cpu::NowUs()));" "S7 stall")
require_text("${agw}" "    minute.stallUsMax = std::max(minute.stallUsMax, thread_cpu::StallUs(wallUs, cpuStart, thread_cpu::NowUs()));" "ad-hoc stall")
# Pre-review: on the bot thread the master's formation is copied only from the same map (the same map thread).
require_order("${accept}" "                if (!onBotThread || master->FindMap() == bot->FindMap())" "                    Formation* masterFormation = MAI_VALUE(Formation*, \"formation\");" "formation copy same map only")
