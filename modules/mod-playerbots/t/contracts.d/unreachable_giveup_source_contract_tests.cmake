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
require_text("${config_cpp}" "            {\"Rotation.TankRoguePoison\", Value(rotationTankRoguePoison)},\n            {\"Rotation.UnreachableGiveUp\", Value(rotationUnreachableGiveUp)},\n            {\"Rotation.UnreachableLock\", Value(rotationUnreachableLock)},\n            {\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)}," "[ConfigEcho] sorted (item 4b: Rotation.UnreachableLock between)")

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

# 8. twow-repo#703 item 4b (AiPlayerbot.Rotation.UnreachableLock, design 20.11.2026 L1-L8; default 0 until its A/B): after a
#    give-up a bot on its own does not pick that guid, nor the same entry within 100 yd of the spot, for 1800 s (self-defence
#    per match type). Pins: the key in three places (dist 0, member 0, read with 0, clamp, inert line, the four-line echo), the
#    pure rules, the read gate (key first, then solo), the arm only inside UnreachableGiveUp after its line (key first, no
#    group), no threat or combat change in the arm and the read, each pick site at its anchor. The checks collect their reasons
#    so the negative probes below can run them on mutated text.
read_source("${PB_SOURCE_DIR}/PlayerbotAI.h" ai_h)
read_source("${PB_SOURCE_DIR}/strategy/values/GrindTargetValue.cpp" grind_cpp)
read_source("${PB_SOURCE_DIR}/strategy/values/TargetValue.cpp" target_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/AttackAction.cpp" attack_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/PullActions.cpp" pull_cpp)

function(lock_need text needle what)
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    set(reasons "${reasons}|missing ${what}" PARENT_SCOPE)
  endif()
endfunction()
function(lock_ban text needle what)
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    set(reasons "${reasons}|forbidden ${what}" PARENT_SCOPE)
  endif()
endfunction()
function(lock_before text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    set(reasons "${reasons}|order ${what}" PARENT_SCOPE)
  endif()
endfunction()
function(lock_count text needle expected what)
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
    set(reasons "${reasons}|count ${what} (expected ${expected}, found ${n})" PARENT_SCOPE)
  endif()
endfunction()
# lock_region(text start end out): from start up to and including the first end after it ("" when not found).
function(lock_region text start end out_var)
  set(${out_var} "" PARENT_SCOPE)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    return()
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    return()
  endif()
  string(LENGTH "${end}" elen)
  math(EXPR blen "${b} + ${elen}")
  string(SUBSTRING "${tail}" 0 ${blen} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

function(check_lock dist config_h config_cpp policy ai_cpp ai_h grind_cpp target_cpp attack_cpp pull_cpp out_var)
  set(reasons "")

  # 8a. The key: dist default 0 once, member 0, read with 0 after the give-up clamp, > 3 off with its line, the inert line, echo.
  lock_need("${dist}" "\nAiPlayerbot.Rotation.UnreachableLock = 0\n" "lock dist default 0")
  lock_count("${dist}" "AiPlayerbot.Rotation.UnreachableLock =" 1 "lock dist line")
  lock_count("${dist}" "AiPlayerbot.Rotation.UnreachableGiveUp =" 1 "give-up dist line (the lock text must not quote it)")
  lock_need("${config_h}" "    uint32 rotationUnreachableLock = 0;" "lock member default 0")
  lock_need("${config_cpp}" "    rotationUnreachableLock = config.GetIntDefault(\"AiPlayerbot.Rotation.UnreachableLock\", 0);" "lock read with default 0")
  lock_need("${config_cpp}" "    if (rotationUnreachableLock > 3)\n    {\n        sLog.outError(\"[UnreachableLock] config key=Rotation.UnreachableLock value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", rotationUnreachableLock);\n        rotationUnreachableLock = 0;\n    }" "lock clamp with its error line")
  lock_need("${config_cpp}" "    if (rotationUnreachableLock && !rotationUnreachableGiveUp)\n        sLog.outError(\"[UnreachableLock] config key=Rotation.UnreachableLock value=%u giveup=0 result=inert" "lock inert line")
  lock_need("${config_cpp}" "    else if (rotationUnreachableLock >= 2 && rotationUnreachableGiveUp >= 2 && rotationUnreachableLock != rotationUnreachableGiveUp)\n        sLog.outError(\"[UnreachableLock] config key=Rotation.UnreachableLock value=%u giveup=%u result=inert (the two cohorts do not overlap)\"" "lock inert line for disjoint cohorts (2 with 3)")
  lock_before("${config_cpp}" "        rotationUnreachableGiveUp = 0;\n    }" "    rotationUnreachableLock = config.GetIntDefault(" "lock read after the give-up clamp")
  lock_need("${config_cpp}" "            {\"Rotation.TankRoguePoison\", Value(rotationTankRoguePoison)},\n            {\"Rotation.UnreachableGiveUp\", Value(rotationUnreachableGiveUp)},\n            {\"Rotation.UnreachableLock\", Value(rotationUnreachableLock)},\n            {\"Rpg.TurnInFirst\", Value(rpgTurnInFirst)}," "lock [ConfigEcho] four adjacent lines")

  # 8b. The pure rules.
  foreach(constant
      "constexpr std::uint32_t LockSeconds = LongIgnoreSeconds;"
      "constexpr float EntryRadiusYards = 100.0f;"
      "constexpr std::size_t LockSlots = 4;"
      "constexpr std::uint32_t LockLogSeconds = 60;")
    lock_need("${policy}" "${constant}" "lock constant ${constant}")
  endforeach()
  lock_need("${policy}" "    return mode != 0 && cohortApplies && !inGroup;" "LockActive: mode 0 off, cohort, no group")
  lock_need("${policy}" "    if (match == Match::Entry)\n        return attacksBotOrPet;\n    if (match == Match::Guid)\n        return attacksBotOrPet && hittableNow;\n    return true;" "MayDefend per match type")
  lock_need("${policy}" "            if (guid && l.guid == guid)\n                return Match::Guid;" "Guid wins, guid 0 never")
  lock_need("${policy}" "            if (entry && l.entry == entry && l.map == map && Near(l, x, y))\n                match = Match::Entry;" "Entry: same entry, same map, radius")
  lock_need("${policy}" "        return dx * dx + dy * dy <= EntryRadiusYards * EntryRadiusYards;" "2D radius")
  lock_need("${policy}" "        *slot = Lock{ guid, entry, map, x, y, now + LockSeconds };" "own clock: until = now + LockSeconds")
  lock_need("${policy}" "        return !logged || now - last >= LockLogSeconds;" "block line budget")

  # 8c. The read: key first (mode 0 = no state, no work), then the fast path, creatures only, solo only.
  lock_region("${ai_cpp}" "bool PlayerbotAI::UnreachableLockBlocks(Unit* unit, ai::unreachable_giveup::Path path)\n{" "\n}\n" read)
  if(read STREQUAL "")
    set(reasons "${reasons}|missing lock read UnreachableLockBlocks")
  endif()
  lock_need("${read}" "bool PlayerbotAI::UnreachableLockBlocks(Unit* unit, ai::unreachable_giveup::Path path)\n{\n    if (!sPlayerbotAIConfig.rotationUnreachableLock || !unit)\n        return false;\n" "lock read: the key is the first statement")
  lock_need("${read}" "    if (!unreachableLocks.Any(now) || !unit->GetObjectGuid().IsCreature() || bot->GetGroup())\n        return false;" "lock read: fast path, creatures, solo only")
  lock_need("${read}" "    bool const hittableNow = match == ug::Match::Guid && attacksBotOrPet &&" "lock read: hittable only for a Guid match that attacks (lazy)")
  lock_need("${read}" "    bool const defend = ug::MayDefend(match, attacksBotOrPet, hittableNow);" "lock read: MayDefend")
  lock_need("${read}" "    return !defend;\n}\n" "lock read: blocks unless defended")
  lock_need("${read}" "\"[UnreachableLock] state=block bot=%u guid=%u entry=%u match=%s path=%s attacks_bot=%u left_s=%u\"\n            \" n_grind_att=%u n_grind_cand=%u n_assist=%u n_attack=%u n_pull=%u defended=%u cohort=%c\"" "lock block line format")
  lock_count("${ai_cpp}" "unreachableLockLog.Note(" 1 "the block line budget only in the read")
  # Its log-free twin for the CastLosGate pre-scan: the same key first, the same gates, the same rule, no block line.
  lock_region("${ai_cpp}" "bool PlayerbotAI::UnreachableLockWouldBlock(Unit* unit)\n{" "\n}\n" twin)
  if(twin STREQUAL "")
    set(reasons "${reasons}|missing lock twin UnreachableLockWouldBlock")
  endif()
  lock_need("${twin}" "bool PlayerbotAI::UnreachableLockWouldBlock(Unit* unit)\n{\n    if (!sPlayerbotAIConfig.rotationUnreachableLock || !unit)\n        return false;\n" "lock twin: the key is the first statement")
  lock_need("${twin}" "    if (!unreachableLocks.Any(now) || !unit->GetObjectGuid().IsCreature() || bot->GetGroup())\n        return false;" "lock twin: fast path, creatures, solo only")
  lock_need("${twin}" "    bool const hittableNow = match == ug::Match::Guid && attacksBotOrPet &&" "lock twin: hittable only for a Guid match that attacks (lazy)")
  lock_need("${twin}" "    return !ug::MayDefend(match, attacksBotOrPet, hittableNow);\n}\n" "lock twin: the same rule")
  lock_ban("${twin}" "unreachableLockLog" "block line state in the lock twin")
  lock_ban("${twin}" "sLog." "a line in the lock twin")
  lock_need("${ai_h}" "    bool UnreachableLockWouldBlock(Unit* unit);" "lock twin declaration")

  # 8d. The arm: only inside UnreachableGiveUp, after its line, the key first and no group; one writer of the locks.
  lock_region("${ai_cpp}" "void PlayerbotAI::UnreachableGiveUp(Unit* target, uint32 now)" "\n}\n" giveup)
  set(arm_call "    if (sPlayerbotAIConfig.rotationUnreachableLock &&\n        ug::LockActive(sPlayerbotAIConfig.rotationUnreachableLock, ai::cohort::Applies(sPlayerbotAIConfig.rotationUnreachableLock, bot->GetGUIDLow()),\n            group != nullptr))\n        ArmUnreachableLock(target, entry, now);\n}\n")
  lock_need("${giveup}" "${arm_call}" "lock arm: key first, cohort, no group, the last statement of UnreachableGiveUp")
  lock_before("${giveup}" "[UnreachableGiveUp] bot=%u" "ArmUnreachableLock(target, entry, now);" "lock arm after the [UnreachableGiveUp] line")
  lock_count("${ai_cpp}" "ArmUnreachableLock(target, entry, now);" 1 "lock arm call")
  lock_count("${ai_cpp}" "unreachableLocks.Arm(" 1 "lock writer")
  lock_region("${ai_cpp}" "void PlayerbotAI::ArmUnreachableLock(Unit* target, int32 entry, uint32 now)\n{" "\n}\n" arm)
  if(arm STREQUAL "")
    set(reasons "${reasons}|missing lock arm ArmUnreachableLock")
  endif()
  lock_need("${arm}" "void PlayerbotAI::ArmUnreachableLock(Unit* target, int32 entry, uint32 now)\n{\n    if (!target->GetObjectGuid().IsCreature())\n        return;\n" "lock arm: creatures only, the first statement")
  lock_need("${arm}" "    bool const evicted = unreachableLocks.Arm(" "lock arm: the lock")
  lock_need("${arm}" "\"[UnreachableLock] state=arm bot=%u guid=%u entry=%u map=%u x=%.0f y=%.0f lock_s=%u radius=%.0f victim_stop=%u pet_stop=%u\"\n        \" evicted=%u cohort=%c\"" "lock arm line format")
  lock_need("${arm}" "ug::Match::Entry && !ug::MayDefend(ug::Match::Entry, attacksUs(victim), false))\n        {\n            bot->AttackStop();" "lock arm: the swing stops only on an Entry match not hitting the bot or pet")
  foreach(part arm read twin)
    foreach(banned "ClearEngagement(target)" "CombatStop" "->Set(" "deleteReference" "released=" "SetTarget(" "SetStatus(" "SetNullTravelTarget(")
      lock_ban("${${part}}" "${banned}" "${banned} in the lock ${part}")
    endforeach()
  endforeach()
  lock_need("${ai_h}" "    ai::unreachable_giveup::Locks unreachableLocks;" "lock member")
  lock_need("${ai_h}" "    bool UnreachableLockBlocks(Unit* unit, ai::unreachable_giveup::Path path);" "lock read declaration")

  # 8e. The pick sites, each a pure insertion at its anchor (L1a, L1b, L2, L3, L4).
  lock_region("${grind_cpp}" "std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>(\"possible attack targets\")->Get();" "std::list<ObjectGuid> targets = *context->GetValue<std::list<ObjectGuid> >(\"possible targets\");" grind_att)
  lock_need("${grind_att}" "        if (!unit || !sServerFacade.IsAlive(unit))\n            continue;\n\n        // twow-repo#703 4b" "L1a after the alive check")
  lock_need("${grind_att}" "        if (ai->UnreachableLockBlocks(unit, ai::unreachable_giveup::Path::GrindAttacker))\n        {\n            logGrind(unit, \"(hostile) ignored (unreachable lock).\");\n            continue;\n        }" "L1a check")
  lock_before("${grind_att}" "Path::GrindAttacker" "CanFreeMoveValue::CanFreeTarget(ai, GuidPosition(unit))" "L1a before the free-range check")
  lock_before("${grind_att}" "Path::GrindAttacker" "logGrind(unit, \"(hostile) selected.\");" "L1a before the attacker pick")
  lock_need("${grind_cpp}" "        if (ai->IsUnreachableTarget(unit->GetObjectGuid()))\n            continue;\n\n        // twow-repo#703 4b" "L1b directly after the hotfix 8.2 skip")
  lock_need("${grind_cpp}" "        if (ai->UnreachableLockBlocks(unit, ai::unreachable_giveup::Path::GrindCandidate))\n            continue;\n" "L1b check")
  lock_before("${grind_cpp}" "if (ai->IsUnreachableTarget(unit->GetObjectGuid()))" "Path::GrindCandidate" "L1b after the hotfix 8.2 skip")
  lock_before("${grind_cpp}" "Path::GrindCandidate" "needForQuestCache[entry] = needForQuest;" "L1b before the quest preference")
  lock_count("${grind_cpp}" "UnreachableLockBlocks(" 2 "lock checks in GrindTargetValue.cpp")
  lock_region("${target_cpp}" "Unit* TargetValue::FindTarget(FindTargetStrategy* strategy)" "\n}\n" find)
  lock_need("${find}" "        if (!unit)\n            continue;\n\n        // twow-repo#703 4b" "L2 after the null check")
  lock_need("${find}" "        if (ai->UnreachableLockBlocks(unit, ai::unreachable_giveup::Path::Assist))\n            continue;\n" "L2 check")
  lock_before("${find}" "Path::Assist" "ai::cast_los_gate::SkipUnreachable(" "L2 before (independent of) the CastLosGate skip")
  lock_before("${find}" "Path::Assist" "strategy->CheckAttacker(unit, &threatManager);" "L2 before the strategy sees the unit")
  lock_count("${target_cpp}" "UnreachableLockBlocks(" 1 "lock checks in TargetValue.cpp (ClosestAttackerTargetingMe not gated)")
  # The CastLosGate pre-scan: a unit the lock skips is no reachable alternative (else the gate skips the defended given-up
  # unit and the lock the sibling: no pick at all). A pure insertion inside the pinned candidate test.
  lock_need("${find}" "            if (!ai->IsUnreachableTarget(guid) && ai->GetUnit(guid))\n            {\n                // twow-repo#703 4b" "L2 pre-scan inside the CastLosGate candidate test")
  lock_need("${find}" "                if (ai->UnreachableLockWouldBlock(ai->GetUnit(guid)))\n                    continue;\n                reachable = true;\n                break;" "L2 pre-scan: a locked unit is no reachable alternative")
  lock_before("${find}" "UnreachableLockWouldBlock(" "Path::Assist" "L2 pre-scan before the main loop")
  lock_count("${target_cpp}" "UnreachableLockWouldBlock(" 1 "lock twin calls in TargetValue.cpp")
  lock_region("${attack_cpp}" "bool AttackAction::Attack(Player* requester, Unit* target)" "\n}\n" attack)
  lock_need("${attack}" "    if (ai->UnreachableLockBlocks(target, ai::unreachable_giveup::Path::Attack))\n    {\n" "L3 check")
  lock_before("${attack}" "Path::Attack" "    if (IsTargetValid(requester, target))" "L3 before IsTargetValid")
  lock_count("${attack_cpp}" "UnreachableLockBlocks(" 1 "lock checks in AttackAction.cpp")
  lock_region("${pull_cpp}" "bool PullRequestAction::Execute(Event& event)" "\n}\n" pull)
  lock_need("${pull}" "    if (ai->UnreachableLockBlocks(target, ai::unreachable_giveup::Path::Pull))\n        return false;\n" "L4 check")
  lock_before("${pull}" "ai->TellPlayerNoFacing(requester, \"You have no target\");" "Path::Pull" "L4 after the null check")
  lock_before("${pull}" "Path::Pull" "const float maxPullDistance = sPlayerbotAIConfig.reactDistance * 3;" "L4 before the distance check")
  lock_before("${pull}" "Path::Pull" "strategy->RequestPull(target);" "L4 before the pull request")
  lock_count("${pull_cpp}" "UnreachableLockBlocks(" 1 "lock checks in PullActions.cpp")

  set(${out_var} "${reasons}" PARENT_SCOPE)
endfunction()

check_lock("${dist}" "${config_h}" "${config_cpp}" "${policy}" "${ai_cpp}" "${ai_h}" "${grind_cpp}" "${target_cpp}" "${attack_cpp}" "${pull_cpp}" lock_reasons)
if(NOT lock_reasons STREQUAL "")
  message(FATAL_ERROR "${TAG}: UnreachableLock (twow-repo#703 4b): ${lock_reasons}")
endif()

# Negative probes: each mutation must apply and be caught by the check it aims at.
set(lock_probes 0)
function(lock_probe label var from to pattern)
  string(REPLACE "${from}" "${to}" mut "${${var}}")
  if(mut STREQUAL "${${var}}")
    message(FATAL_ERROR "${TAG}: lock probe '${label}': the mutation did not apply")
  endif()
  set(${var} "${mut}")
  check_lock("${dist}" "${config_h}" "${config_cpp}" "${policy}" "${ai_cpp}" "${ai_h}" "${grind_cpp}" "${target_cpp}" "${attack_cpp}" "${pull_cpp}" r)
  if(r STREQUAL "")
    message(FATAL_ERROR "${TAG}: lock probe '${label}': the contract did not catch it")
  endif()
  if(NOT r MATCHES "${pattern}")
    message(FATAL_ERROR "${TAG}: lock probe '${label}' caught by another check: ${r}")
  endif()
  math(EXPR lock_probes "${lock_probes} + 1")
  set(lock_probes "${lock_probes}" PARENT_SCOPE)
endfunction()
# Drop a key guard: the read without the key first; the arm without the key first.
lock_probe("read key guard dropped" ai_cpp "    if (!sPlayerbotAIConfig.rotationUnreachableLock || !unit)\n" "    if (!unit)\n" "the key is the first statement")
lock_probe("arm key guard dropped" ai_cpp "    if (sPlayerbotAIConfig.rotationUnreachableLock &&\n        ug::LockActive(" "    if (\n        ug::LockActive(" "lock arm: key first")
# Defend = locked: no self-defence for a sibling hitting the bot; the read blocking a defended unit.
lock_probe("defend = locked (policy)" policy "    if (match == Match::Entry)\n        return attacksBotOrPet;" "    if (match == Match::Entry)\n        return false;" "MayDefend per match type")
lock_probe("defend = locked (read)" ai_cpp "    return !defend;\n}\n" "    return true;\n}\n" "blocks unless defended")
# No group check: at the arm (the rule) and at the read.
lock_probe("no group check (rule)" policy "    return mode != 0 && cohortApplies && !inGroup;" "    return mode != 0 && cohortApplies;" "LockActive")
lock_probe("no group check (read)" ai_cpp " || !unit->GetObjectGuid().IsCreature() || bot->GetGroup())" " || !unit->GetObjectGuid().IsCreature())" "solo only")
lock_probe("no group check (arm)" ai_cpp "            group != nullptr))\n        ArmUnreachableLock(" "            false))\n        ArmUnreachableLock(" "lock arm: key first, cohort, no group")
# Dist default 1.
lock_probe("dist default 1" dist "\nAiPlayerbot.Rotation.UnreachableLock = 0\n" "\nAiPlayerbot.Rotation.UnreachableLock = 1\n" "lock dist default 0")
# And two more the design names: Entry radius 3D / any map, and a released= step in the arm.
lock_probe("entry on any map" policy " && l.map == map && Near(l, x, y))" " && Near(l, x, y))" "Entry: same entry, same map, radius")
lock_probe("threat release in the arm" ai_cpp "    bool const evicted = unreachableLocks.Arm(" "    bot->getHostileRefManager().deleteReference(target);\n    bool const evicted = unreachableLocks.Arm(" "forbidden deleteReference in the lock arm")
# Review holes 20.11.: the CastLosGate pre-scan without the lock; the arm on a player or pet; the twin logging; the disjoint
# cohorts silent.
lock_probe("pre-scan without the lock" target_cpp "                if (ai->UnreachableLockWouldBlock(ai->GetUnit(guid)))\n                    continue;\n" "" "L2 pre-scan")
lock_probe("arm on any unit" ai_cpp "    if (!target->GetObjectGuid().IsCreature())\n        return;\n\n    namespace ug" "    namespace ug" "lock arm: creatures only")
lock_probe("twin logs" ai_cpp "    return !ug::MayDefend(match, attacksBotOrPet, hittableNow);\n}\n" "    unreachableLockLog.Reset(now);\n    return !ug::MayDefend(match, attacksBotOrPet, hittableNow);\n}\n" "block line state in the lock twin")
lock_probe("disjoint cohorts silent" config_cpp "    else if (rotationUnreachableLock >= 2 && rotationUnreachableGiveUp >= 2 && rotationUnreachableLock != rotationUnreachableGiveUp)\n" "    else if (false)\n" "disjoint cohorts")
message(STATUS "${TAG}: UnreachableLock section passed (${lock_probes} negative probes)")
