# twow-repo#703 item 3 (A3-PLAN-ZIEL, owner standing approval 09.11.2026; design v2 4e9c26b5, OB-10 OK 11.11.2026; default 0
# until its A/B): AiPlayerbot.Loot.MoveGiveUp, the move-to-loot loop. "move to loot" followed an unreachable loot target without
# an attempt count (ARB N1: bots 853 / 169 / 2142 stood 219-262 min, moved=0.0).
# This contract pins the key (dist 0, member 0, read with 0, cohort clamp with its error line, [ConfigEcho] sorted), the pure
# policy constants, the hook in MoveToLootAction after the hold / abort checks and before the move with the key tested first,
# the scope (cohort, roster bot on its own), the give-up steps, GiveUp only from MoveToLootAction, the Add refusal (Add is the
# only insert, so OrderByDistance never sees a given-up object), the mode-0 neutrality, the two line formats and the unit test.

cmake_policy(VERSION 3.16)

set(TAG "LootMoveGiveUp")

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
read_source("${PB_SOURCE_DIR}/LootGiveUpPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/LootObjectStack.h" stack_h)
read_source("${PB_SOURCE_DIR}/LootObjectStack.cpp" stack_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/MovementActions.cpp" move_cpp)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The key: dist default 0, member default 0, read with default 0, modes 0..3 (other -> 0, logged), [ConfigEcho] sorted.
require_text("${dist}" "\nAiPlayerbot.Loot.MoveGiveUp = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Loot.MoveGiveUp =" 1 "one dist line")
require_text("${config_h}" "    uint32 lootMoveGiveUp = 0;" "member default 0")
require_text("${config_cpp}" "    lootMoveGiveUp = config.GetIntDefault(\"AiPlayerbot.Loot.MoveGiveUp\", 0);" "read with default 0")
require_text("${config_cpp}" "    if (lootMoveGiveUp > 3)\n    {\n        sLog.outError(\"[LootGiveUp] config key=Loot.MoveGiveUp value=%u result=off (0 off, 1 all, 2 cohort B, 3 cohort A)\", lootMoveGiveUp);\n        lootMoveGiveUp = 0;\n    }" "cohort clamp with its error line")
require_text("${config_cpp}" "            {\"Loot.HoldTravelWhileLootable.MaxSeconds\", Value(lootHoldTravelWhileLootableMaxSeconds)},\n            {\"Loot.MoveGiveUp\", Value(lootMoveGiveUp)},\n            {\"Loot.RoleWeights\", Value(lootRoleWeights)}," "[ConfigEcho] sorted")

# 2. The policy constants of design v2.
foreach(constant
    "constexpr std::int64_t WindowSeconds = 30;"
    "constexpr std::uint32_t MinTries = 5;"
    "constexpr float MinImproveYards = 1.0f;"
    "constexpr std::int64_t MaxGapSeconds = 5;"
    "constexpr std::int64_t ExcludeSeconds = 600;"
    "constexpr std::size_t MaxBlocked = 16;")
  require_text("${policy}" "${constant}" "policy constant")
endforeach()
require_text("${policy}" "    if (t.tries == 0 || guid != t.guid || now - t.last > MaxGapSeconds)" "restart on no state, a new guid or a gap > 5 s")
require_text("${policy}" "    if (dist <= t.distBest - MinImproveYards)\n    {\n        t.distBest = dist;\n        t.since = now;\n        t.tries = 1;\n        return Step::Progress;\n    }" "progress >= 1 yd restarts the clock")
require_text("${policy}" "    if (now - t.since >= WindowSeconds && t.tries >= MinTries)\n        return Step::GiveUp;" "give up: 30 s and 5 tries")

# 3. The hook in MoveToLootAction: after the T28b end check and the IsLootPossible abort, before the move; the key first.
region("${move_cpp}" "bool MoveToLootAction::Execute(Event& event)" "\n}\n" move_body)
set(hook "    if (sPlayerbotAIConfig.lootMoveGiveUp && wo && LootMoveGiveUp(ai, bot, loot, wo))\n        return false;\n")
require_text("${move_body}" "    WorldObject *wo = loot.GetWorldObject(bot);\n\n    // twow-repo#703 item 3 (AiPlayerbot.Loot.MoveGiveUp): after the hold / abort checks above, before the move - a loot target\n    // the bot comes no nearer to is given up (no endless move-to-loot loop). Key 0 (default): no state, as before.\n${hook}" "the hook right after the world object lookup")
require_order("${move_body}" "if (sPlayerbotAIConfig.gatheringPaceReach && GatheringPaceTimeout(ai, loot))" "${hook}" "after the T28b end check")
require_order("${move_body}" "    if (!loot.IsLootPossible(bot))\n" "${hook}" "after the IsLootPossible abort")
require_order("${move_body}" "${hook}" "bool moved = los ? MoveNear(wo, sPlayerbotAIConfig.contactDistance) : MoveTo(WorldPosition(wo));" "before the move")
count_text("${move_cpp}" "sPlayerbotAIConfig.lootMoveGiveUp &&" 1 "one key test at the hook")
count_text("${move_cpp}" "LootMoveGiveUp(" 2 "LootMoveGiveUp: one definition, one call")

# 4. The give-up: scope first (cohort, roster bot without a real master), then the policy step; on give-up the line, the loot
#    target reset, the exclusion, the state reset.
region("${move_cpp}" "bool LootMoveGiveUp(PlayerbotAI* ai, Player* bot, LootObject const& loot, WorldObject* wo)" "\n}\n" helper)
require_text("${helper}" "    if (!ai::cohort::Applies(sPlayerbotAIConfig.lootMoveGiveUp, bot->GetGUIDLow()) || !IsRosterBotOnItsOwn(ai))\n        return false;\n" "scope: cohort, roster bot on its own")
require_order("${helper}" "|| !IsRosterBotOnItsOwn(ai))" "stack->MoveTry()" "no state outside the scope")
require_text("${helper}" "    if (lg::Next(state, loot.guid.GetRawValue(), bot->GetDistance(wo), now) != lg::Step::GiveUp)\n        return false;\n" "3D distance to the loot object, give up only on GiveUp")
require_text("${helper}" "sLog.outBasic(\"[LootGiveUp] bot=%u guid=%llu entry=%u dist_start=%.1f dist_best=%.1f tries=%u waited_s=%lld mode=%u\"," "[LootGiveUp] line format")
require_text("${helper}" "    ai->GetAiObjectContext()->GetValue<LootObject>(\"loot target\")->Set(LootObject());\n    stack->GiveUp(loot.guid, now);\n    state = lg::Try();\n    return true;\n}" "loot target reset, exclusion, state reset")
require_order("${helper}" "[LootGiveUp] bot=" "->Set(LootObject());" "the line before the reset")

# 5. GiveUp only from MoveToLootAction; the exclusion list is filled only by GiveUp.
file(GLOB_RECURSE pb_sources LIST_DIRECTORIES false "${PB_SOURCE_DIR}/*.cpp" "${PB_SOURCE_DIR}/*.h")
list(LENGTH pb_sources pb_source_count)
if(pb_source_count LESS 100)
  message(FATAL_ERROR "${TAG}: source scan found only ${pb_source_count} files under ${PB_SOURCE_DIR}")
endif()
foreach(source IN LISTS pb_sources)
  file(RELATIVE_PATH relative "${PB_SOURCE_DIR}" "${source}")
  read_source("${source}" text)
  if(NOT relative STREQUAL "strategy/actions/MovementActions.cpp")
    forbid_text("${text}" "->GiveUp(" "GiveUp outside MoveToLootAction (${relative})")
    if(NOT relative STREQUAL "LootObjectStack.h")
      forbid_text("${text}" "MoveTry()" "the approach state outside MoveToLootAction (${relative})")
    endif()
  endif()
endforeach()
count_text("${move_cpp}" "->GiveUp(" 1 "one GiveUp call")
count_text("${move_cpp}" "GiveUp(" 3 "MovementActions.cpp: the helper definition, its call and the one GiveUp call")
count_text("${stack_cpp}" "GiveUp(" 1 "LootObjectStack.cpp: only the definition, no call")
count_text("${stack_h}" "GiveUp(" 1 "LootObjectStack.h: only the declaration")
count_text("${move_cpp}" "MoveTry()" 1 "one approach state read")
count_text("${stack_cpp}" "giveUp.Add(" 1 "the list filled only by GiveUp")
region("${stack_cpp}" "void LootObjectStack::GiveUp(ObjectGuid guid, int64 now)" "\n}\n" give_up)
require_text("${give_up}" "giveUp.Add(guid.GetRawValue(), now + loot_giveup::ExcludeSeconds, now)" "600 s exclusion")
require_text("${give_up}" "    Remove(guid);\n" "the object leaves the loot list")
require_text("${stack_h}" "        loot_giveup::BlockList giveUp;" "per-bot exclusion list on the loot stack")
require_text("${stack_h}" "        loot_giveup::Try moveTry;" "per-bot approach state on the loot stack")

# 6. The Add refusal; Add is the only insert, so OrderByDistance (byte-pinned by the T28b contract) never sees a blocked object.
region("${stack_cpp}" "bool LootObjectStack::Add(ObjectGuid guid)" "\n}\n" add_body)
require_text("${add_body}" "    if (giveUp.Size() && giveUp.Blocks(guid.GetRawValue(), int64(time(nullptr))))\n        return false;\n    if (!availableLoot.insert(guid).second)" "Add refuses a blocked object; empty list: no time() call")
count_text("${stack_cpp}" "availableLoot.insert(" 1 "Add is the only insert")
foreach(insert "availableLoot.emplace" "availableLoot = " "availableLoot.swap")
  forbid_text("${stack_cpp}" "${insert}" "another insert into the loot list")
endforeach()

# 7. Mode-0 neutrality: the purge / minute line only with the key; the gauge leaves with the bot.
region("${stack_cpp}" "bool LootObjectStack::CanLoot(float maxDistance)" "\n}\n" can_loot)
require_text("${can_loot}" "    if (sPlayerbotAIConfig.lootMoveGiveUp)\n    {\n" "CanLoot: the key first")
require_text("${can_loot}" "sLog.outBasic(\"[LootGiveUpMin] mode=%u loot_giveup=%u loot_blocked=%u\", sPlayerbotAIConfig.lootMoveGiveUp,\n                counters.TakeGiveUps(), counters.Held());" "[LootGiveUpMin] line format")
require_text("${can_loot}" "    std::vector<LootObject> ordered = OrderByDistance(maxDistance);\n    return !ordered.empty();" "CanLoot result unchanged")
region("${stack_cpp}" "LootObjectStack::~LootObjectStack()" "\n}\n" dtor)
require_text("${dtor}" "loot_giveup::SharedCounters().held.fetch_sub(int64(giveUp.Size()), std::memory_order_relaxed);" "held entries leave the gauge with the bot")

# 8. The unit test is registered.
require_text("${tests}" "add_executable(loot_giveup_policy_tests\n  \"\${PB_MODULE_DIR}/t/loot_giveup_policy_tests.cpp\")" "unit test target")
require_text("${tests}" "add_test(NAME loot_giveup_policy\n  COMMAND loot_giveup_policy_tests" "unit test registration")
