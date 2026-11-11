# twow-repo#703 item 4 (A3-PLAN-ZIEL, owner standing approval 09.11.2026; design v2 85574eca, OB-10 OK 11.11.2026; default 0
# until its A/B): AiPlayerbot.Rotation.UnreachableGiveUp, the endless fight on an unreachable target. Hotfix 8.2 ignored such a
# target for 300 s and the bot picked the same mob again (ARB N1 bot 798: 12x [Unreachable] on entry 1501, about 260 min).
# This contract pins the key (dist 0, member 0, read with 0, cohort clamp with its error line, [ConfigEcho] sorted), the pure
# policy constants and rules, the hook in CheckReachProgress after the 300 s ignore with the key tested first (mode 0 = no
# counting), the gate (cohort, roster bot, no real player master, no group or not its leader), the give-up steps (long ignore,
# the engagement drop, the quest mark only for the current objective entry with a fixed duration and the 1 h guard), the
# unchanged player-bot drop (gate and [ReachDrop] line), the line format and the unit test.

cmake_policy(VERSION 3.16)

set(TAG "UnreachableGiveUp")

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "${TAG}: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "${TAG}: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle expected description)
  set(rest "${text}")
  set(n 0)
  string(LENGTH "${needle}" len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" at)
    if(at EQUAL -1)
      break()
    endif()
    math(EXPR n "${n} + 1")
    math(EXPR cut "${at} + ${len}")
    string(SUBSTRING "${rest}" ${cut} -1 rest)
  endwhile()
  if(NOT n EQUAL expected)
    message(FATAL_ERROR "${TAG}: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

# region(text start end out): from start up to and including the first end after it.
function(region text start end out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "${TAG}: region start not found: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "${TAG}: region end not found after: ${start}")
  endif()
  string(LENGTH "${end}" elen)
  math(EXPR blen "${b} + ${elen}")
  string(SUBSTRING "${tail}" 0 ${blen} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/UnreachableGiveUpPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/TravelMgr.cpp" travel_cpp)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The key: dist default 0, member default 0, read with default 0, modes 0..3 (other -> 0, logged), [ConfigEcho] sorted.
require_text("${dist}" "\nAiPlayerbot.Rotation.UnreachableGiveUp = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Rotation.UnreachableGiveUp =" 1 "one dist line")
require_text("${config_h}" "    uint32 rotationUnreachableGiveUp = 0;" "member default 0")
require_text("${config_cpp}" "    rotationUnreachableGiveUp = config.GetIntDefault(\"AiPlayerbot.Rotation.UnreachableGiveUp\", 0);" "read with default 0")
require_text("${config_cpp}" "    if (rotationUnreachableGiveUp > 3)\n    {\n        sLog.outError(\"[UnreachableGiveUp] config key=Rotation.UnreachableGiveUp value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", rotationUnreachableGiveUp);\n        rotationUnreachableGiveUp = 0;\n    }" "cohort clamp with its error line")
require_text("${config_cpp}" "            {\"Rotation.TankRoguePoison\", Value(rotationTankRoguePoison)},\n            {\"Rotation.UnreachableGiveUp\", Value(rotationUnreachableGiveUp)},\n            {\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)}," "[ConfigEcho] sorted")

# 2. The policy constants and rules of design v2.
foreach(constant
    "constexpr std::uint32_t K = 3;"
    "constexpr std::uint32_t WindowSeconds = 1800;"
    "constexpr std::uint32_t LongIgnoreSeconds = 1800;"
    "constexpr std::uint32_t MarkSeconds = 1800;"
    "constexpr std::uint32_t MarkGuardSeconds = 3600;"
    "constexpr std::size_t RingSlots = 4;"
    "constexpr std::size_t MarkGuardSlots = 8;")
  require_text("${policy}" "${constant}" "policy constant")
endforeach()
require_text("${policy}" "    return rosterBot && !realPlayerMaster && (!inGroup || !groupLeader);" "the gate: roster, no real master, no group or not the leader")
require_text("${policy}" "    return mode != 0 && cohortApplies && counts;" "mode 0 = no counting")
require_text("${policy}" "        if (slot->count && now - slot->first >= WindowSeconds)\n            slot->count = 0;" "the window restarts the count")
require_text("${policy}" "        if (count >= K)\n            *slot = Slot{};" "the slot clears at K")
require_text("${policy}" "            if (e.quest && e.quest == quest && e.entry == entry && now - e.at < MarkGuardSeconds)\n                return false;" "the 1 h mark guard")

# 3. The hook in CheckReachProgress: after the 300 s ignore and the [Unreachable] line, the key first, right before the result.
region("${ai_cpp}" "bool PlayerbotAI::CheckReachProgress(Unit* target, float distance)" "\n}\n" reach_body)
set(hook "    if (sPlayerbotAIConfig.rotationUnreachableGiveUp)\n        UnreachableGiveUp(target, now);\n    return true;\n}\n")
require_text("${reach_body}" "${hook}" "the hook (key first) right before the result")
require_order("${reach_body}" "    unreachableTargets.Add(target->GetObjectGuid().GetRawValue(), now, ignoreSeconds);" "${hook}" "after the 300 s ignore")
require_order("${reach_body}" "[Unreachable] bot=%u" "${hook}" "after the [Unreachable] line")
count_text("${ai_cpp}" "sPlayerbotAIConfig.rotationUnreachableGiveUp)" 1 "one key test at the hook")
count_text("${ai_cpp}" "UnreachableGiveUp(target, now);" 1 "UnreachableGiveUp: one call")
count_text("${ai_cpp}" "unreachableGiveUp.Note(" 1 "the count only in UnreachableGiveUp")

# 4. The give-up: the gate first (mode + cohort, roster bot, no real master, no group or not the leader), the count, then at K
#    the long ignore, the engagement drop, the quest mark (current objective entry only, fixed duration, 1 h guard), the line.
region("${ai_cpp}" "void PlayerbotAI::UnreachableGiveUp(Unit* target, uint32 now)" "\n}\n" giveup)
require_text("${giveup}" "    if (!ug::Active(sPlayerbotAIConfig.rotationUnreachableGiveUp, ai::cohort::Applies(sPlayerbotAIConfig.rotationUnreachableGiveUp, bot->GetGUIDLow()),\n            ug::Counts(sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()), HasRealPlayerMaster(), group != nullptr,\n                group && group->GetLeaderGuid() == bot->GetObjectGuid())))\n        return;\n" "the gate")
require_text("${giveup}" "    uint32 const count = unreachableGiveUp.Note(guid, now);\n    if (count < ug::K)\n        return;\n" "the count, K")
require_text("${giveup}" "    unreachableTargets.Add(guid, now, ug::LongIgnoreSeconds);" "the long ignore")
require_text("${giveup}" "    ClearEngagement(target);" "the engagement drop for this roster bot")
require_text("${giveup}" "    if (uint32 const objectiveQuest = travel ? travel->UnreachableCampQuest(entry) : 0)" "the mark only for the current objective entry")
require_text("${giveup}" "        if (unreachableMarkGuard.Allows(objectiveQuest, entry, now))\n        {\n            evicted = travel->MarkUnreachableCamp(objectiveQuest, entry, ug::MarkSeconds * 1000u);\n            unreachableMarkGuard.Note(objectiveQuest, entry, now);" "the mark: fixed duration behind the 1 h guard")
require_text("${giveup}" "            mark = \"skip_1h\";" "the guard's skip is named")
require_order("${giveup}" "    if (count < ug::K)" "    unreachableTargets.Add(guid, now, ug::LongIgnoreSeconds);" "nothing before K")
require_order("${giveup}" "    ClearEngagement(target);" "travel->MarkUnreachableCamp(" "the drop before the mark")
require_text("${giveup}" "sLog.outBasic(\"[UnreachableGiveUp] bot=%u guid=%u entry=%u quest=%u count=%u ignore_s=%u mark_s=%u mark=%s evicted=%u\"\n        \" reason=unreachable_k cohort=%c\"," "[UnreachableGiveUp] line format")
count_text("${ai_cpp}" "travel->MarkUnreachableCamp(" 1 "the mark only from UnreachableGiveUp")
count_text("${ai_cpp}" "unreachableMarkGuard.Note(" 1 "the guard noted only with a mark")

# 5. The camp mark in TravelTarget: the current quest objective of this entry; the shared ring with a fixed duration, the eviction
#    reported, the target ends (no inactive-reselect fallback to a COOLDOWN at the camp).
region("${travel_cpp}" "uint32 TravelTarget::UnreachableCampQuest(int32 entry) const" "\n}\n" camp_quest)
require_text("${camp_quest}" "    QuestObjectiveTravelDestination const* objective = dynamic_cast<QuestObjectiveTravelDestination const*>(tDestination);\n    return objective && entry && objective->GetEntry() == entry ? objective->GetQuestId() : 0;" "current objective entry only")
region("${travel_cpp}" "bool TravelTarget::MarkUnreachableCamp(uint32 quest, int32 entry, uint32 markMs)" "\n}\n" camp_mark)
require_text("${camp_mark}" "    bool const evicted = ai::unreachable_giveup::MarkEvictsLive(emptyCamps, quest, entry, nowMs);\n    quest_loop::MarkEmptyCamp(emptyCamps, quest, entry, nowMs, markMs);\n    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "eviction checked before the mark, then the target ends")
forbid_text("${camp_mark}" "inactiveReselectPending" "no COOLDOWN fallback at the given-up camp")
require_order("${camp_mark}" "    ai::supervisor::WriterScope writerScope(ai::supervisor::WriterId::Execution);\n" "    SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);" "the arbiter Execution scope before the status write (a3s3 SCOPES entry)")
count_text("${travel_cpp}" "MarkUnreachableCamp(" 1 "MarkUnreachableCamp: only its definition in TravelMgr.cpp")

# 6. The player-bot drop is unchanged: its gate, then the shared steps, then its own [ReachDrop] line.
region("${ai_cpp}" "void PlayerbotAI::DropUnreachableEngagement(Unit* target)" "\n}\n" drop)
require_text("${drop}" "    if (!target || !HasRealPlayerMaster() || !ai::cohort::Applies(sPlayerbotAIConfig.rotationReachDropEngagement, bot->GetGUIDLow()))\n        return;\n\n    ai::reach_drop::Held const held = ClearEngagement(target);\n    if (!held.Any())\n        return;\n" "the player-bot gate before the shared steps")
require_text("${drop}" "[ReachDrop] bot=%u target_entry=%u" "the [ReachDrop] line stays with the player-bot drop")
region("${ai_cpp}" "ai::reach_drop::Held PlayerbotAI::ClearEngagement(Unit* target)" "\n}\n" clear)
forbid_text("${clear}" "[ReachDrop]" "ClearEngagement itself logs nothing")
forbid_text("${clear}" "HasRealPlayerMaster()" "ClearEngagement has no gate of its own")
count_text("${ai_cpp}" "ClearEngagement(target)" 2 "ClearEngagement: the player-bot drop and the give-up")

# 7. The unit test is registered.
require_text("${tests}" "add_executable(unreachable_giveup_policy_tests\n  \"\${PB_MODULE_DIR}/t/unreachable_giveup_policy_tests.cpp\")" "unit test target")
require_text("${tests}" "add_test(NAME unreachable_giveup_policy\n  COMMAND unreachable_giveup_policy_tests" "unit test registration")
