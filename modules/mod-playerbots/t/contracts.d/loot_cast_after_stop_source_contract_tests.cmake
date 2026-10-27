# twow-repo#333 Berufe P1-w (OB-00 27.10.2026, M1r run 4: 0 gathering skill-ups). PlayerbotAI::CastSpell (the Unit overload,
# through which every loot cast goes) stopped a moving bot before a cast-time spell and then cancelled the cast without a real
# player master - the action failed, the next action moved the bot again, every try cancelled. A loot cast (OPEN_LOCK or
# SKINNING with its loot target found and no item target) now casts after the stop; every other cast and the GameObject /
# position overloads keep the old rule (combat casts: CLI-ROT). OpenLootAction holds the AI for the started cast, so the engine
# does not replace the cast wait with the react delay and let the next movement action interrupt the cast. The rules are
# tested in t/cast_while_moving_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#333 P1-w loot cast: missing ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#333 P1-w loot cast: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(count_text text needle out_var)
  string(LENGTH "${needle}" needle_len)
  set(count 0)
  string(FIND "${text}" "${needle}" offset)
  while(NOT offset EQUAL -1)
    math(EXPR count "${count} + 1")
    math(EXPR offset "${offset} + ${needle_len}")
    string(SUBSTRING "${text}" ${offset} -1 text)
    string(FIND "${text}" "${needle}" offset)
  endwhile()
  set(${out_var} ${count} PARENT_SCOPE)
endfunction()

function(require_count text needle expected description)
  count_text("${text}" "${needle}" found)
  if(NOT found EQUAL ${expected})
    message(FATAL_ERROR "#333 P1-w loot cast: ${description} (found ${found}, expected ${expected}): ${needle}")
  endif()
endfunction()

read_source("CastWhileMovingPolicy.h" policy)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/LootAction.cpp" loot_cpp)
read_source("strategy/actions/LootAction.h" loot_h)

# The rules: a loot cast needs its target and no item target; after the stop only the bot on its own with another cast
# cancels; the open loot action lasts at least the cast.
require_text("${policy}" "return lootTargetFound && !itemTarget && (firstEffect == kSpellEffectOpenLock || firstEffect == kSpellEffectSkinning);" "the loot cast")
require_text("${policy}" "return !activePlayerMaster && !lootCast;" "cancel only without a player master and without a loot cast")
require_text("${policy}" "return castDuration > actionDuration ? castDuration : actionDuration;" "the open loot duration")

# The wiring: only in the Unit overload, between its start and the GameObject overload.
set(unit_head "bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)")
set(go_head "bool PlayerbotAI::CastSpell(uint32 spellId, GameObject* goTarget, Item* itemTarget, bool waitForSpell, uint32* outSpellDuration)")
string(FIND "${ai_cpp}" "${unit_head}" unit_at)
string(FIND "${ai_cpp}" "${go_head}" go_at)
if(unit_at EQUAL -1 OR go_at EQUAL -1 OR NOT unit_at LESS go_at)
  message(FATAL_ERROR "#333 P1-w loot cast: the Unit and GameObject overloads of PlayerbotAI::CastSpell not found in order")
endif()
math(EXPR unit_len "${go_at} - ${unit_at}")
string(SUBSTRING "${ai_cpp}" ${unit_at} ${unit_len} unit_body)

require_text("${ai_cpp}" "#include \"playerbot/CastWhileMovingPolicy.h\"" "the policy include")
require_text("${unit_body}" "            targets.setGOTarget(go);\n            faceTo = go;\n            lootTargetFound = true;" "a spawned game object is a found target")
require_text("${unit_body}" "                targets.setUnitTarget(creature);\n                faceTo = creature;\n                lootTargetFound = true;" "a unit is a found target")
require_text("${unit_body}" "bool const lootCast = ai::cast_moving::IsLootCast(pSpellInfo->Effect[0], lootTargetFound, targets.getItemTarget() != nullptr);" "the loot cast flag")
# The moving gate is unchanged (a loot cast still stops first), and the decision ends the moving block right after the
# unconditional stop; lootCast is used only there.
require_text("${unit_body}" "    if (sServerFacade.isMoving(bot) && ((GetSpellCastTime(pSpellInfo, bot, spell) > 0) || (IsChanneledSpell(pSpellInfo) && (GetSpellDuration(pSpellInfo) > 0))))\n    {\n        // always fail when jumping\n        if (IsJumping() || bot->IsFalling())\n        {\n            spell->cancel();\n            return false;\n        }\n\n        StopMoving();\n\n        // fail if not with real player to avoid movement glitches\n" "the unchanged moving gate, jumping cancel and stop")
require_text("${unit_body}" "        if (ai::cast_moving::CancelAfterStop(HasActivePlayerMaster(), lootCast))\n        {\n            if (waitForSpell)\n            {\n                SetAIInternalUpdateDelay(sPlayerbotAIConfig.globalCoolDown);\n            }\n\n            spell->cancel();\n            return false;\n        }\n    }\n" "the decision as the end of the moving block")
require_order("${unit_body}" "bool const lootCast = ai::cast_moving::IsLootCast(" "    if (sServerFacade.isMoving(bot) && " "the flag before the moving gate")
require_order("${unit_body}" "        StopMoving();\n" "if (ai::cast_moving::CancelAfterStop(HasActivePlayerMaster(), lootCast))" "the decision after the stop")
require_order("${unit_body}" "if (ai::cast_moving::CancelAfterStop(HasActivePlayerMaster(), lootCast))" "SpellCastResult spellSuccess = spell->SpellStart(&targets);" "before SpellStart")
require_count("${unit_body}" "lootCast" 2 "lootCast only declared and used in the decision")

# The GameObject and position overloads are unchanged (CLI-ROT's generic cast fix takes them), so the old check appears there.
string(SUBSTRING "${ai_cpp}" ${go_at} -1 after_unit)
require_count("${after_unit}" "        // fail if not with real player to avoid movement glitches\n        if (!HasActivePlayerMaster())\n" 2 "the GameObject and position overloads must keep the old check")
require_count("${ai_cpp}" "ai::cast_moving::CancelAfterStop(" 1 "CancelAfterStop only in the Unit overload")

# OpenLootAction holds the AI for the cast it started: each run starts from the react delay, every DoLoot cast reports its
# cast time, and a successful run lasts at least that long.
require_text("${loot_cpp}" "#include \"playerbot/CastWhileMovingPolicy.h\"" "the policy include in LootAction.cpp")
require_text("${loot_h}" "        uint32 castDuration = 0;" "the cast time member of OpenLootAction")
string(FIND "${loot_cpp}" "bool OpenLootAction::Execute(Event& event)" exec_at)
string(FIND "${loot_cpp}" "uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)" doloot_end)
if(exec_at EQUAL -1 OR doloot_end EQUAL -1 OR NOT exec_at LESS doloot_end)
  message(FATAL_ERROR "#333 P1-w loot cast: OpenLootAction::Execute / DoLoot not found before GetOpeningSpell")
endif()
math(EXPR open_len "${doloot_end} - ${exec_at}")
string(SUBSTRING "${loot_cpp}" ${exec_at} ${open_len} open_body)
require_text("${open_body}" "    SetDuration(sPlayerbotAIConfig.reactDelay);\n    castDuration = 0;\n    LootObject lootObject = AI_VALUE(LootObject, \"loot target\");\n    bool result = DoLoot(lootObject);\n" "the reset before DoLoot")
require_text("${open_body}" "        context->GetValue<LootObject>(\"loot target\")->Set(LootObject());\n        SetDuration(ai::cast_moving::LootActionDuration(GetDuration(), castDuration));\n    }\n    return result;\n" "the cast wait on success")
require_count("${open_body}" "ai->CastSpell(" 7 "the casts of DoLoot")
require_count("${open_body}" ", nullptr, true, &castDuration)" 7 "every DoLoot cast reports its cast time")

message(STATUS "#333 P1-w loot cast source contract passed")
