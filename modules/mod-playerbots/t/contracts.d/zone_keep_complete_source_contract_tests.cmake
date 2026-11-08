cmake_policy(VERSION 3.16)
# twow-repo#601 owner rule R1, package "start-zone rule complete" (OB-00 08.11.2026; CLI-Deep, ZoneKeepPolicy.h, design
# ws-10/cli-quest-organic/r1/SZM-V2-RG-CAUSE-AND-GROUP-GATE-DESIGN-20261108.md): AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete
# (0 off default and rollback, 1 on) - G1 no follow of a bot leader outside a held bot's start zone, G2 the refused group copy logged,
# G3 the group leave after FollowLeaveSeconds of refused follow, V2-a the release push (first empty offer = band step in the window).
# Pins the key, the pure rules, the four sites with the switch first (nothing read at 0), the real-player exemption, the lines and
# the test registration.

if(NOT DEFINED PB_SOURCE_DIR OR NOT DEFINED PB_MODULE_DIR)
  message(FATAL_ERROR "zone keep complete: PB_SOURCE_DIR and PB_MODULE_DIR are required")
endif()

function(read_source path out_var)
  file(READ "${PB_SOURCE_DIR}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "zone keep complete: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "zone keep complete: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "zone keep complete: ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(between text first second out_var)
  string(FIND "${text}" "${first}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "zone keep complete: region not found: '${first}'")
  endif()
  string(SUBSTRING "${text}" ${a} -1 rest)
  string(FIND "${rest}" "${second}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "zone keep complete: region end not found: '${second}'")
  endif()
  string(SUBSTRING "${rest}" 0 ${b} region)
  set(${out_var} "${region}" PARENT_SCOPE)
endfunction()

read_source("PlayerbotAIConfig.h" config_h)
read_source("PlayerbotAIConfig.cpp" config_cpp)
read_source("aiplayerbot.conf.dist.in" conf_dist)
read_source("ZoneKeepPolicy.h" policy)
read_source("TravelMgr.h" travel_h)
read_source("TravelMgr.cpp" travel)
read_source("QuestBand.cpp" band)
read_source("strategy/actions/FollowActions.cpp" follow)
read_source("strategy/actions/LeaveGroupAction.cpp" leave)
read_source("strategy/actions/ChooseTravelTargetAction.cpp" choose)

# 1. Key: member 0, parse 0 default with only 1 on (an unknown value off, logged), echoed in sorted order, documented in the dist.
require_text("${config_h}" "uint32 questFirstProgressionStartZoneComplete = 0;" "Complete member off")
require_text("${config_cpp}" "config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete\", 0);" "Complete default 0")
require_text("${config_cpp}" "    questFirstProgressionStartZoneComplete = config.GetIntDefault(\"AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete\", 0);\n    if (questFirstProgressionStartZoneComplete != 0 && !ai::zone_keep::CompleteOn(int32(questFirstProgressionStartZoneComplete)))\n    {\n        sLog.outError(\"[ZoneKeep] config key=StartZoneMinLevel.Complete value=%u result=off (0 off, 1 on)\", questFirstProgressionStartZoneComplete);\n        questFirstProgressionStartZoneComplete = 0;\n    }" "only 1 is on, an unknown value off and logged")
require_text("${config_cpp}" "#include \"playerbot/ZoneKeepPolicy.h\"" "the policy include")
require_text("${config_cpp}" "            {\"QuestFirstProgression.StartZoneMinLevel.Cohort\", Value(questFirstProgressionStartZoneMinLevelCohort)},\n            {\"QuestFirstProgression.StartZoneMinLevel.Complete\", Value(questFirstProgressionStartZoneComplete)},\n            {\"QuestFirstProgression.T" "[ConfigEcho] entry, sorted (owner rule #615, #710)")
require_text("${conf_dist}" "\nAiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete = 0\n" "dist key 0")
require_order("${conf_dist}" "AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Cohort = 1" "AiPlayerbot.QuestFirstProgression.StartZoneMinLevel.Complete = 0" "dist key after .Cohort")
foreach(line follow_gate group_copy_gate group_leave release_push)
  require_text("${conf_dist}" "state=${line}" "dist names the ${line} line")
endforeach()

# 2. Pure rules (std only).
require_text("${policy}" "constexpr std::uint32_t FollowLeaveSeconds = 300;" "G3 leave after 300 s")
require_text("${policy}" "constexpr std::uint32_t RefusalFreshSeconds = 60;" "G3 refusals in a row")
require_text("${policy}" "constexpr std::uint32_t GateLineSeconds = 600;" "line window 600 s")
require_text("${policy}" "constexpr std::uint32_t ReleasePushLevels = 3;" "V2-a window L5-7")
require_text("${policy}" "inline bool CompleteOn(std::int32_t value)\n{\n    return value == 1;\n}" "only 1 is on")
require_text("${policy}" "inline bool FollowDefers(bool held, bool leaderOutsideStart)\n{\n    return held && leaderOutsideStart;\n}" "G1 rule")
require_text("${policy}" "return refusedSince != 0 && lastRefusal != 0 && std::int32_t(now - refusedSince) >= std::int32_t(seconds) &&\n        std::int32_t(now - lastRefusal) <= std::int32_t(RefusalFreshSeconds);" "G3 rule")
require_text("${policy}" "return on && applies && inStartZone && InReleaseWindow(minLevel, level);" "V2-a rule")
require_text("${policy}" "return minLevel != 0 && level >= minLevel && level < minLevel + ReleasePushLevels;" "V2-a window")
require_text("${policy}" "return push && prior < 1 ? 1 : prior;" "V2-a: at least one prior failure, never lowered")
forbid_text("${policy}" "#include \"Player" "no game header in the policy")

# 3. Game side (TravelMgr.cpp ai::zone_keep): the switch first, StartZoneHolds (no real player master, roster, .Cohort), the lines.
require_text("${travel_h}" "bool FollowGated(Player* bot, PlayerbotAI* botAI, Player* leader);" "G1 helper declared")
require_text("${travel_h}" "bool ReleasePushApplies(Player* bot, PlayerbotAI* botAI);" "V2-a helper declared")
require_text("${travel}" "return sPlayerbotAIConfig.questFirstProgressionStartZoneComplete && ai::zone_keep::StartZoneHolds(bot, botAI);" "CompleteHolds = switch + StartZoneHolds")
require_text("${travel}" "sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !(botAI && botAI->HasRealPlayerMaster());" "StartZoneHolds: roster bots without a real player master")
between("${travel}" "bool ai::zone_keep::FollowGated(" "bool ai::zone_keep::GroupLeaveDue(" follow_gated)
require_order("${follow_gated}" "!ai::zone_keep::CompleteHolds(bot, botAI)" "OutsideStartZone(bot, &leaderPos)" "G1: held before the area lookup")
require_text("${follow_gated}" "if (!ai::zone_keep::FollowDefers(true, leaderZone != 0))" "G1 rule used")
require_text("${follow_gated}" "SetZoneKeepClock(botAI, FollowRefusedSinceValue, 0);" "G1: a follow clears the refusal run")
require_text("${follow_gated}" "[ZoneKeep] state=follow_gate bot=%u level=%u zone=%u leader=%u leader_level=%u leader_zone=%u" "follow_gate line")
require_text("${follow_gated}" "ZoneKeepClock(botAI, FollowLineLeaderValue) != leader->GetGUIDLow()" "follow_gate once per bot and leader")
between("${travel}" "bool ai::zone_keep::GroupLeaveDue(" "void ai::zone_keep::NoteGroupCopyGate(" leave_due)
require_text("${leave_due}" "ai::zone_keep::LeaveDue(since, ZoneKeepClock(botAI, FollowRefusedLastValue), now, ai::zone_keep::FollowLeaveSeconds)" "G3 rule used")
require_text("${leave_due}" "[ZoneKeep] state=group_leave bot=%u level=%u zone=%u leader=%u refused_s=%u" "group_leave line")
# OB-00 (b) after OB-10's review of #948: no leave from a group with a real player in it, leader or not.
require_text("${policy}" "inline bool LeaveAllowed(bool realPlayerMember)\n{\n    return !realPlayerMember;\n}" "G3: real player rule")
require_text("${leave_due}" "for (GroupReference* ref = group->GetFirstMember(); ref && !realPlayerMember; ref = ref->next())" "G3: every member checked")
require_text("${leave_due}" "realPlayerMember = !memberAi || memberAi->IsRealPlayer();" "G3: a player's character or a bot a real player plays")
require_text("${leave_due}" "if (!ai::zone_keep::LeaveAllowed(realPlayerMember))\n        return false;" "G3: the real player check decides")
require_order("${leave_due}" "if (!ai::zone_keep::LeaveAllowed(realPlayerMember))" "[ZoneKeep] state=group_leave" "G3: the check before the line and the clock reset")
between("${travel}" "void ai::zone_keep::NoteGroupCopyGate(" "bool ai::zone_keep::ReleasePushApplies(" copy_gate)
require_text("${copy_gate}" "[ZoneKeep] state=group_copy_gate bot=%u level=%u zone=%u rejects=%u chosen=%u" "group_copy_gate line")
require_text("${copy_gate}" "ai::zone_keep::LineDue(ZoneKeepClock(botAI, CopyLineUntilValue), now)" "group_copy_gate once per window")
between("${travel}" "bool ai::zone_keep::ReleasePushApplies(" "void TravelTarget::NoteQuestObjectiveFailure()" release_applies)
require_order("${release_applies}" "!sPlayerbotAIConfig.questFirstProgressionStartZoneComplete" "sObjectMgr.GetPlayerInfo" "V2-a: the switch before any lookup")
require_text("${release_applies}" "ai::cohort::Applies(sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevelCohort, bot->GetGUIDLow())" "V2-a: the .Cohort selection")
require_text("${release_applies}" "!botAI->HasRealPlayerMaster()" "V2-a: never a bot led by a real player")
require_text("${release_applies}" "ai::zone_keep::InStartZone(bot->getRace(), customStarts, createZone, bot->GetZoneId())" "V2-a: still in the start zone")

# 4. G1 site: FollowAction::isUseful, after the BOT-12 map rule, the switch first, a real player's character always followed.
between("${follow}" "bool FollowAction::isUseful()" "bool FollowAction::CanDeadFollow(" follow_useful)
require_text("${follow_useful}" "if (followTarget && sPlayerbotAIConfig.questFirstProgressionStartZoneComplete && followTarget->IsPlayer() &&\n        !IsRealPlayerTarget(followTarget) &&\n        ai::zone_keep::FollowGated(bot, ai, static_cast<Player*>(followTarget)))\n        return false;" "G1 gate")
require_order("${follow_useful}" "sPlayerbotAIConfig.botGroupsLeaderOtherMap" "ai::zone_keep::FollowGated(" "G1 after the BOT-12 map rule")
require_order("${follow_useful}" "ai::zone_keep::FollowGated(" "Formation* formation = AI_VALUE(Formation*, \"formation\");" "G1 before the follow distance")

# 5. G3 site: LeaveFarAwayAction::isUseful, after the ad-hoc / quest group returns and the safety loop, the switch first.
between("${leave}" "bool LeaveFarAwayAction::isUseful()" "if (ai->GetGrouperType() == GrouperType::SOLO)" leave_useful)
require_text("${leave}" "#include \"playerbot/TravelMgr.h\"" "G3 include")
require_text("${leave_useful}" "if (sPlayerbotAIConfig.questFirstProgressionStartZoneComplete && ai::zone_keep::GroupLeaveDue(bot, ai, groupMaster))\n            return true;" "G3 gate")
require_order("${leave_useful}" "if (quest_groups::IsMember(ai))" "ai::zone_keep::GroupLeaveDue(" "G3: quest help groups keep their own rules")
require_order("${leave_useful}" "adhoc_group::Groups().Find(bot->GetGroup()->GetId(), adhocEntry)" "ai::zone_keep::GroupLeaveDue(" "G3: ad-hoc groups keep their own rules")
require_order("${leave_useful}" "if (!ai->IsSafe(member))" "ai::zone_keep::GroupLeaveDue(" "G3 after the safety loop")
require_order("${leave_useful}" "ai::zone_keep::GroupLeaveDue(" "if (!sPlayerbotAIConfig.randomBotGroupNearby)" "G3 before the nearby-group rules")

# 6. G2 site: the group target choice goes through SetBestTarget (the R1 deferral); the package logs the refused copies.
between("${choose}" "bool ChooseGroupTravelTargetAction::Execute(Event& event)" "bool ChooseGroupTravelTargetAction::isUseful()" group_choice)
require_text("${group_choice}" "bool const groupTargetChosen = SetBestTarget(requester, &newTarget, travelList);\n    if (sPlayerbotAIConfig.questFirstProgressionStartZoneComplete)\n        ai::zone_keep::NoteGroupCopyGate(bot, ai, lastRejects.startZone, groupTargetChosen);\n    if (!groupTargetChosen)\n        return false;" "G2 line after the choice")
require_order("${group_choice}" "NoteGroupCopyGate(" "newTarget.SetGroupCopy(" "G2 before the copy")
require_text("${choose}" "    lastRejects = ai::quest_search::RejectCounts();" "SetBestTarget resets the counts")
require_text("${choose}" "if (position && sPlayerbotAIConfig.questFirstProgressionStartZoneMinLevel && ai::zone_keep::StartZoneHolds(bot, ai) &&\n        ai::zone_keep::OutsideStartZone(bot, position))\n        return PostActiveDeferral::StartZone;" "the R1 deferral that refuses the copy")

# 7. V2-a site: TryBandStep - the failure counter read, then the push, then the verdict; the switch first; one line per push.
between("${band}" "bool ChooseTravelTargetAction::TryBandStep(" "bool const walk = walkPlan" try_step)
require_order("${try_step}" "facts.priorFailures = AI_VALUE2(int, \"manual int\", \"quest route failures\");" "ai::zone_keep::ReleasePushApplies(bot, ai)" "V2-a after the counter read")
require_order("${try_step}" "facts.priorFailures = ai::zone_keep::PushedPriorFailures(releasePush, facts.priorFailures);" "qb::StepVerdict const verdict = qb::BandStepDue(facts);" "V2-a before the verdict")
require_text("${try_step}" "bool const releasePush = sPlayerbotAIConfig.questFirstProgressionStartZoneComplete && facts.emptyOffer &&\n        ai::zone_keep::ReleasePushApplies(bot, ai);" "V2-a: switch first, empty offer only")
require_text("${try_step}" "[ZoneKeep] state=release_push bot=%u level=%u zone=%u prior=%d verdict=%s" "release_push line")
require_text("${try_step}" "if (releasePush && facts.priorFailures != priorBeforePush)" "release_push only when the push changed the input")

# 8. Test registration.
file(READ "${PB_MODULE_DIR}/tests.cmake" tests_cmake)
require_text("${tests_cmake}" "add_executable(zone_keep_policy_tests\n  \"\${PB_MODULE_DIR}/t/zone_keep_policy_tests.cpp\")" "zone_keep_policy test target")
require_text("${tests_cmake}" "add_test(NAME zone_keep_policy" "zone_keep_policy registered")
