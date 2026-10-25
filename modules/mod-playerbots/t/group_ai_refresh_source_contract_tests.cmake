# twow-repo#628 fix (GroupAiRefresh.h, short test #628 on C 24.10.2026 FAIL: pass_us_max up to 81 ms): the world-thread group
# managers (ad-hoc AdhocGroupWorld, S7 QuestGroupWorld) only change the group on the world thread. The AI part - strategy
# rebuild, reset, the S7 role sets - is posted to the bot and runs in its next UpdateAI on its own map thread.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#628 group AI refresh: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#628 group AI refresh: unexpected ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#628 group AI refresh: ${description}: '${first}' before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(n 0)
  set(rest "${text}")
  string(LENGTH "${needle}" len)
  string(FIND "${rest}" "${needle}" at)
  while(NOT at EQUAL -1)
    math(EXPR n "${n} + 1")
    math(EXPR at "${at} + ${len}")
    string(SUBSTRING "${rest}" ${at} -1 rest)
    string(FIND "${rest}" "${needle}" at)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "#628 group AI refresh: ${description}: '${needle}' ${n} times, expected ${expected}")
  endif()
endfunction()

read_source("GroupAiRefresh.h" policy)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("AdhocGroupWorld.cpp" adhoc)
read_source("QuestGroupWorld.cpp" s7)
read_source("strategy/actions/AcceptInvitationAction.h" accept)

# The two manager sources, and nothing else, take the deferred accept.
require_text("${policy}" "return source == \"adhoc group\" || source == \"quest group\";" "manager sources")
require_text("${adhoc}" "inviteeAi->DoSpecificAction(\"accept invitation\", Event(\"adhoc group\", \"\", inviter), true);" "ad-hoc accept source")
require_text("${s7}" "memberAi->DoSpecificAction(\"accept invitation\", Event(\"quest group\", \"\", leader), true);" "S7 accept source")

# Accept: the group change and the master stay; the strategy rebuild and the reset go to the bot for the managers only.
require_text("${accept}" "bool const worldManager = group_ai_refresh::IsWorldManagerSource(event.getSource());" "manager check in accept")
require_text("${accept}" "            if (worldManager)
                ai->RequestGroupAi(group_ai_refresh::JoinReset);
            else
            {
                ai->ResetStrategies();" "deferred join reset, inline otherwise")
require_order("${accept}" "bot->GetSession()->HandleGroupAcceptOpcode(p);" "ai->RequestGroupAi(group_ai_refresh::JoinReset);" "the core accept first")
require_order("${accept}" "ai->SetMaster(inviter);" "ai->RequestGroupAi(group_ai_refresh::JoinReset);" "the master at once (S7 FollowerIntact reads it)")

# The managers never rebuild strategies on the world thread.
forbid_text("${adhoc}" "ResetStrategies(" "strategy rebuild in the ad-hoc manager")
forbid_text("${adhoc}" "ChangeStrategy(" "strategy change in the ad-hoc manager")
require_text("${adhoc}" "ai->RequestGroupAi(group_ai_refresh::LeaveReset);" "ad-hoc leave posts the reset")
forbid_text("${s7}" "ResetStrategies(" "strategy rebuild in the S7 manager")
count_text("${s7}" "ChangeStrategy(" 2 "S7 strategy changes only in ApplyRoleSet (bot thread)")
require_text("${s7}" "void ApplyRoleSet(PlayerbotAI* ai, bool turnIn)
{
    if (!ai || ai->GetQuestGroupRole() == 0)
        return;
    ai->ChangeStrategy(turnIn ? TurnInSet : FollowerSet, BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy(\"-group\", BotState::BOT_STATE_DEAD);
}" "role sets on the bot thread, never for a released bot")
require_text("${s7}" "    ai->RequestGroupAi(group_ai_refresh::Follower);" "S7 follower set posted")
require_text("${s7}" "    ai->RequestGroupAi(group_ai_refresh::TurnIn);" "S7 turn-in set posted")
require_text("${s7}" "    ai->RequestGroupAi(group_ai_refresh::LeaveReset);" "S7 release posts the reset")

# The bot thread: requests taken once per UpdateAI, after the park gate and before this update's AI; reset before the set.
require_text("${ai_h}" "std::atomic<uint32> groupAiRequests{ 0 };" "request atomic")
string(FIND "${ai_cpp}" "void PlayerbotAI::UpdateAI(uint32 elapsed, bool minimal)\n{" ua)
string(FIND "${ai_cpp}" "void PlayerbotAI::UpdateAIInternal(" uai)
string(SUBSTRING "${ai_cpp}" ${ua} 6000 update_ai)
require_order("${update_ai}" "if (parked)" "    ApplyGroupAiRequests();" "after the park gate")
require_order("${update_ai}" "lastUpdateTime = uint32(time(nullptr));" "    ApplyGroupAiRequests();" "inside the bot update")
count_text("${ai_cpp}" "ApplyGroupAiRequests();" 1 "one call site")
require_text("${ai_cpp}" "while (!groupAiRequests.compare_exchange_weak(pending, ai::group_ai_refresh::Merge(pending, request)))" "lock-free merge")
require_text("${ai_cpp}" "uint32 const requests = groupAiRequests.exchange(0);" "taken once")
require_order("${ai_cpp}" "        ResetStrategies();
        if (plan.joinStrategies)
            ChangeStrategy(\"-lfg,-bg\", BotState::BOT_STATE_NON_COMBAT);
        Reset();" "ai::quest_groups::ApplyRoleSet(this, plan.turnIn);" "the reset before the role set")

# Measurement (the retest attributes the pass time): sub-times in both minute lines.
require_text("${adhoc}" "pass_us_max=%u join_us_max=%u leave_us_max=%u scan_us_max=%u bots=%u" "ad-hoc minute sub-times")
require_text("${s7}" "scan_us_max=%u passes=%u mode=%u form_us_max=%u dissolve_us_max=%u scan_only_us_max=%u bots=%u" "S7 minute sub-times")
