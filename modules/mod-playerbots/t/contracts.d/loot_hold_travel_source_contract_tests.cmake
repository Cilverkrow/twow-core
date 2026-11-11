# twow-repo#575 H1 (A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix: code default 0 until the acceptance unit passes, then a flip
# pair): AiPlayerbot.Loot.HoldTravelWhileLootable, loot ping-pong. "move to rpg target" / "move to travel target" stood back
# only while the "available loot" stack (entries expire after 30 s) offered loot, not while the bot's own loot target was
# still open; the rpg-target heal branch (PartyMemberToHeal, CLI-ROT) drew the bot back the same way.
# This contract pins the key (dist, member, read, [ConfigEcho] sorted), the hold in both isUseful bodies after the existing
# available-loot test with the key tested first, the heal guard around the rpg-target heal only (CLI-ROT), the read-only
# helper LootTargetOpen (loot distance bound, IsLootPossible), the pure policy and the policy test registration.

# cmake 3.x script mode (Debian trixie builder / CI): policies as in the host cmake 4.x.
cmake_policy(VERSION 3.16)

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "LootHold: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "LootHold: ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "LootHold: order ${description}: '${first}' must come before '${second}'")
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
    message(FATAL_ERROR "LootHold: ${description}: expected ${expected}, found ${n}: ${needle}")
  endif()
endfunction()

# region(text start end out): from start up to and including the first end after it.
function(region text start end out_var)
  string(FIND "${text}" "${start}" a)
  if(a EQUAL -1)
    message(FATAL_ERROR "LootHold: region start not found: ${start}")
  endif()
  string(SUBSTRING "${text}" ${a} -1 tail)
  string(FIND "${tail}" "${end}" b)
  if(b EQUAL -1)
    message(FATAL_ERROR "LootHold: region end not found after: ${start}")
  endif()
  string(LENGTH "${end}" elen)
  math(EXPR blen "${b} + ${elen}")
  string(SUBSTRING "${tail}" 0 ${blen} part)
  set(${out_var} "${part}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" dist)
read_source("${PB_SOURCE_DIR}/LootHoldPolicy.h" policy)
read_source("${PB_SOURCE_DIR}/LootObjectStack.cpp" stack_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/MoveToRpgTargetAction.cpp" rpg_cpp)
read_source("${PB_SOURCE_DIR}/strategy/actions/MoveToTravelTargetAction.cpp" travel_cpp)
read_source("${PB_SOURCE_DIR}/strategy/values/PartyMemberToHeal.cpp" heal_cpp)
read_source("${PB_MODULE_DIR}/tests.cmake" tests)

# 1. The key: dist default 0, member default false, read with default false, [ConfigEcho] in sorted place.
require_text("${dist}" "\nAiPlayerbot.Loot.HoldTravelWhileLootable = 0\n" "dist default 0")
count_text("${dist}" "AiPlayerbot.Loot.HoldTravelWhileLootable =" 1 "one dist line")
require_text("${config_h}" "    bool lootHoldTravelWhileLootable = false;" "member default false")
require_text("${config_cpp}" "    lootHoldTravelWhileLootable = config.GetBoolDefault(\"AiPlayerbot.Loot.HoldTravelWhileLootable\", false);" "read with default false")
require_text("${config_cpp}" "            {\"LogInGroupOnly\", Value(logInGroupOnly)},\n            {\"Loot.HoldTravelWhileLootable\", Value(lootHoldTravelWhileLootable)},\n            {\"Loot.HoldTravelWhileLootable.MaxSeconds\", Value(lootHoldTravelWhileLootableMaxSeconds)},\n            {\"Loot.MoveGiveUp\", Value(lootMoveGiveUp)},\n            {\"Loot.RoleWeights\", Value(lootRoleWeights)}," "[ConfigEcho] sorted (twow-repo#703 item 3: Loot.MoveGiveUp between)")
# The hold limit (OB-10 review 09.11.): dist 60, member 60, read through the clamp with the policy default.
require_text("${dist}" "\nAiPlayerbot.Loot.HoldTravelWhileLootable.MaxSeconds = 60\n" "limit dist default 60")
require_text("${config_h}" "    uint32 lootHoldTravelWhileLootableMaxSeconds = 60;" "limit member default 60")
require_text("${config_cpp}" "    lootHoldTravelWhileLootableMaxSeconds = ai::loot_hold::ClampMaxSeconds(config.GetIntDefault(\"AiPlayerbot.Loot.HoldTravelWhileLootable.MaxSeconds\", int(ai::loot_hold::DefaultMaxSeconds)));" "limit read through the clamp")

# 2. The hold in both isUseful bodies: key first (off = LootTargetOpen never called), after the available-loot test.
set(HOLD "    if (sPlayerbotAIConfig.lootHoldTravelWhileLootable && ai::loot_hold::LootTargetOpen(ai))\n        return false;")
set(AVAILABLE "        LootObject lootObject = AI_VALUE(LootObjectStack*, \"available loot\")->GetLoot(sPlayerbotAIConfig.lootDistance);\n        if (lootObject.IsLootPossible(bot))\n            return false;\n    }\n")
foreach(site rpg travel)
  if(site STREQUAL "rpg")
    set(src "${rpg_cpp}")
    set(head "bool MoveToRpgTargetAction::isUseful()")
  else()
    set(src "${travel_cpp}")
    set(head "bool MoveToTravelTargetAction::isUseful()")
  endif()
  require_text("${src}" "#include \"playerbot/LootHoldPolicy.h\"" "${site}: policy include")
  count_text("${src}" "lootHoldTravelWhileLootable" 1 "${site}: one hold in the file")
  region("${src}" "${head}" "\n}\n" body)
  require_text("${body}" "${HOLD}" "${site}: the hold inside isUseful")
  require_text("${body}" "${AVAILABLE}    // twow-repo#575 H1" "${site}: the hold right after the available-loot test")
endforeach()

# 3. Heal branch (CLI-ROT): the guard wraps only the rpg-target heal; key first; party and other heal targets outside.
require_text("${heal_cpp}" "#include \"playerbot/LootHoldPolicy.h\"" "heal: policy include")
count_text("${heal_cpp}" "lootHoldTravelWhileLootable" 1 "heal: one guard")
set(GUARD "    if (!(sPlayerbotAIConfig.lootHoldTravelWhileLootable && ai::loot_hold::LootTargetOpen(ai)))\n    {\n        if (GuidPosition rpgTarget = AI_VALUE(GuidPosition, \"rpg target\"))")
require_text("${heal_cpp}" "${GUARD}" "heal: the guard around the rpg-target heal")
region("${heal_cpp}" "${GUARD}" "\n    }\n" guarded)
count_text("${guarded}" "needHeals.push_back(" 1 "heal: only the rpg-target heal inside the guard")
require_order("${heal_cpp}" "${GUARD}" "    const std::vector<Player*> partyMembers = GetPartyMembers();" "heal: party members after the guard, unguarded")

# 4. The helper: the loot distance bound, then IsLootPossible, then the hold clock (OB-10 review 09.11.: no hold without end).
#    It writes only the bot's own hold clock, never the loot target.
require_text("${stack_cpp}" "#include \"playerbot/LootHoldPolicy.h\"" "helper: policy include")
region("${stack_cpp}" "bool ai::loot_hold::LootTargetOpen(PlayerbotAI* ai)" "\n}\n" helper)
require_text("${helper}" "GetValue<LootObject>(\"loot target\")->Get();" "helper: reads the loot target")
require_text("${helper}" "    if (lootObject.IsEmpty())\n        return false;" "helper: no loot target, no hold")
require_text("${helper}" "!ai::loot_hold::WithinLootDistance(bot->GetDistance(wo), sPlayerbotAIConfig.lootDistance)" "helper: loot distance bound")
require_order("${helper}" "WithinLootDistance(" "    if (!lootObject.IsLootPossible(bot))\n        return false;" "helper: bound before the lootable test")
require_order("${helper}" "    if (!lootObject.IsLootPossible(bot))\n        return false;" "ai::loot_hold::Next(" "helper: the clock only for a lootable target")
require_text("${helper}" "context->GetValue<std::string>(\"manual string\", \"loot hold guid\");" "helper: own clock guid (bot AI value)")
require_text("${helper}" "context->GetValue<time_t>(\"manual time\", \"loot hold since\");" "helper: own clock start (bot AI value)")
require_text("${helper}" "ai::loot_hold::Step const step = ai::loot_hold::Next(guid, heldGuid, std::int64_t(heldSince), std::int64_t(now),\n        sPlayerbotAIConfig.lootHoldTravelWhileLootableMaxSeconds);" "helper: the clock with the configured limit")
require_text("${helper}" "    if (step == ai::loot_hold::Step::Start)\n    {\n        heldGuidValue->Set(std::to_string(guid));\n        heldSinceValue->Set(now);\n    }" "helper: a new loot target starts the clock")
require_text("${helper}" "sLog.outBasic(\"[LootHold] bot=%u level=%u map=%u target_entry=%u target_low=%u kind=%s held_s=%u result=expired\"," "helper: the expired line")
require_order("${helper}" "result=expired" "        heldSinceValue->Set(0);" "helper: expired marked after the line (one line per expiry)")
require_text("${helper}" "    return ai::loot_hold::Holds(step);\n}" "helper: the answer is the clock's")
count_text("${helper}" "->Set(" 3 "helper writes only its own clock (guid, start, expired mark)")
forbid_text("${helper}" "\"loot target\")->Set(" "helper sets the loot target")
forbid_text("${helper}" "->Reset(" "helper resets a value")
forbid_text("${helper}" "RESET_AI_VALUE" "helper resets a value")
forbid_text("${helper}" "SET_AI_VALUE" "helper sets a value through the macro")

# 5. The pure policy.
require_text("${policy}" "inline bool Hold(bool key, bool lootTargetOpen)\n{\n    return key && lootTargetOpen;\n}" "policy: Hold")
require_text("${policy}" "inline bool WithinLootDistance(float distance, float lootDistance)\n{\n    return distance >= 0.0f && distance <= lootDistance;\n}" "policy: loot distance bound")
require_text("${policy}" "bool LootTargetOpen(PlayerbotAI* ai);" "policy: helper declaration")
require_text("${policy}" "constexpr std::uint32_t DefaultMaxSeconds = 60;" "policy: default limit")
require_text("${policy}" "    return value < 10 ? 10u : (value > 600 ? 600u : std::uint32_t(value));" "policy: limit clamp 10..600")
require_text("${policy}" "    if (guid != heldGuid)\n        return Step::Start;\n    if (heldSince == 0)\n        return Step::Expired;\n    if (now - heldSince < std::int64_t(maxSeconds))\n        return Step::Keep;\n    return Step::Expire;" "policy: the clock")
require_text("${policy}" "inline bool Holds(Step step)\n{\n    return step == Step::Start || step == Step::Keep;\n}" "policy: only start and keep hold")

# 6. The policy test is registered and compiled.
require_text("${tests}" "add_executable(loot_hold_policy_tests\n  \"\${PB_MODULE_DIR}/t/loot_hold_policy_tests.cpp\")" "policy test target")
require_text("${tests}" "add_test(NAME loot_hold_policy\n  COMMAND loot_hold_policy_tests" "policy test registration")
