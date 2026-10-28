# twow-repo#333 finding 4 (CLI-Berufe pattern hunt 27.10.2026, OB-00; M1r run 4: Hearthstone 21 x INTERRUPTED + 1 x
# SPELL_IN_PROGRESS against 8 x OK). The travel step (HandleSpecialMovement), unstuck and zone escape started the hearthstone
# (and the travel step a teleport spell) through DoSpecificAction; the engine then set the AI wait to the outer action's own
# duration, so the cast wait of the use action was lost, the next pass failed the step and reset the path, and the next
# movement interrupted the cast. Now every site holds for the cast (the wait taken from the spell), and the travel step and
# unstuck wait for a teleport the bot already prepares; a watch in PlayerbotAI::UpdateAI ends every hold as soon as the spell
# is no longer PREPARING (OB-10 review 28.10.: interrupt, fear / stun cancel, failure), and the hearthstone is recorded as the
# last spell cast. Rules: t/teleport_cast_policy_tests.cpp; this contract pins the wiring.

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
    message(FATAL_ERROR "#333 finding 4 teleport cast wait: missing ${description}: ${needle}")
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
    message(FATAL_ERROR "#333 finding 4 teleport cast wait: ${description} (found ${found}, expected ${expected}): ${needle}")
  endif()
endfunction()

read_source("TeleportCastPolicy.h" policy)
read_source("TeleportCastWait.h" wait_h)
read_source("PlayerbotAI.h" ai_h)
read_source("PlayerbotAI.cpp" ai_cpp)
read_source("strategy/actions/MovementActions.cpp" move_cpp)
read_source("strategy/actions/UnstuckAction.cpp" unstuck_cpp)
read_source("strategy/actions/ZoneEscapeActions.cpp" escape_cpp)
read_source("strategy/actions/UseItemAction.cpp" use_cpp)

# The rules: the same teleport, the wait from the spell, the hold, and the watch (release before the deadline when the spell
# is no longer that PREPARING Spell; after the deadline only stop watching).
require_text("${policy}" "return stepSpell == kHearthstone && preparingSpell == kScourgestone;" "the Scourgestone stands in for the hearthstone only")
require_text("${policy}" "return castRestMs + reactDelayMs;" "the wait for a prepared teleport")
require_text("${policy}" "return preparedWaitMs ? preparedWaitMs : reactDelayMs;" "the hold: the spell's wait, else one react delay")
require_text("${policy}" "    if (static_cast<std::int32_t>(nowMs - w.deadline) >= 0)\n        return WatchStep::Expire;\n    if (preparing && currentSpell == w.spell && currentId == w.id)\n        return WatchStep::Keep;\n    return WatchStep::Release;" "the watch rule")
require_text("${wait_h}" "    Spell* const spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);\n    if (!spell || spell->getState() != SPELL_STATE_PREPARING || !SameTeleport(stepSpell, spell->m_spellInfo->Id))\n        return 0;\n    return WaitMs(spell->GetCastedTime(), reactDelayMs);" "the prepared wait from the spell")
require_text("${wait_h}" "    std::uint32_t const waitMs = PreparedWaitMs(bot, stepSpell, reactDelayMs);\n    ai->WatchTeleportHold(waitMs);\n    return HoldMs(waitMs, reactDelayMs);" "HoldFor arms the watch with every hold")

# The watch in PlayerbotAI: the member, the arming from the generic slot, and the release in UpdateAI before the internal AI
# update (an interrupt, a fear / stun cancel or a failed cast ends the hold at once - no SMSG_SPELL_FAILURE, no 1 s limit).
require_text("${ai_h}" "    ai::teleport_cast::HoldWatch teleportHold;" "the watch member")
require_text("${ai_h}" "    void WatchTeleportHold(uint32 holdMs);" "the arming method")
require_text("${ai_cpp}" "    Spell* const spell = holdMs ? bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;\n    if (!spell)\n    {\n        ai::teleport_cast::Clear(teleportHold);\n        return;\n    }\n    ai::teleport_cast::Arm(teleportHold, uint64(uintptr_t(spell)), spell->m_spellInfo->Id, WorldTimer::getMSTime(), holdMs);" "the arming from the generic slot")
require_text("${ai_cpp}" "        if (holdStep == ai::teleport_cast::WatchStep::Release)\n            ResetAIInternalUpdateDelay();\n        if (holdStep != ai::teleport_cast::WatchStep::Keep)\n            ai::teleport_cast::Clear(teleportHold);" "the release in UpdateAI")
string(FIND "${ai_cpp}" "if (ai::teleport_cast::Watching(teleportHold))" watch_at)
string(FIND "${ai_cpp}" "SC_PHASE(\"UpdateAI.UpdateAIReaction\"" gate_at)
string(FIND "${ai_cpp}" "if (sPlayerbotAIConfig.castWaitPreparing && ai::cast_wait::Active(castWait))" castwait_at)
if(watch_at EQUAL -1 OR gate_at EQUAL -1 OR castwait_at EQUAL -1 OR NOT castwait_at LESS watch_at OR NOT watch_at LESS gate_at)
  message(FATAL_ERROR "#333 finding 4 teleport cast wait: the watch runs in UpdateAI after the cast_wait release and before the internal AI gate")
endif()
require_count("${ai_cpp}" "if (ai::teleport_cast::Watching(teleportHold))" 1 "one watch check")

# HandleSpecialMovement (the live travel path; MoveTo returns MoveTo2 at once, so its old teleport tail is not touched): the
# prepared-teleport guard first in the NODE_TELEPORT block, then a watched hold after both starts.
require_text("${move_cpp}" "#include \"playerbot/TeleportCastWait.h\"" "the include in MovementActions.cpp")
require_count("${move_cpp}" "SetDuration(teleport_cast::HoldFor(ai, bot, nextPoint.entry, reactDelay));" 3 "the watched hold (guard + hearthstone start + teleport cast start)")
require_count("${move_cpp}" "return ai->DoSpecificAction(\"hearthstone\", Event(\"move action\"), true) ? true : false;" 0 "no hearthstone start in HandleSpecialMovement that returns before the hold")
string(FIND "${move_cpp}" "if (nextPoint.type == PathNodeType::NODE_TELEPORT && nextPoint.entry)" block_at)
string(FIND "${move_cpp}" "if (teleport_cast::PreparedWaitMs(bot, nextPoint.entry, reactDelay))" wait_at)
string(FIND "${move_cpp}" "if (nextPoint.entry == 8690) // Hearthstone" hearth_at)
if(block_at EQUAL -1 OR wait_at EQUAL -1 OR hearth_at EQUAL -1 OR NOT block_at LESS wait_at OR NOT wait_at LESS hearth_at)
  message(FATAL_ERROR "#333 finding 4 teleport cast wait: the prepared wait sits in the NODE_TELEPORT block before the hearthstone use")
endif()

# Unstuck: each run from the react delay; both long-stuck branches wait for a prepared hearthstone before "action useful"
# (no repop mid-cast) and hold after a start, both watched.
require_text("${unstuck_cpp}" "    uint32 const reactDelay = sPlayerbotAIConfig.reactDelay;\n    SetDuration(reactDelay);\n    auto const hearthPrepared = [&]()" "unstuck starts each run from the react delay")
require_count("${unstuck_cpp}" "SetDuration(ai::teleport_cast::HoldFor(ai, bot, ai::teleport_cast::kHearthstone, reactDelay));" 2 "the watched holds in unstuck")
require_count("${unstuck_cpp}" "        if (hearthPrepared())\n            return true;\n        if (AI_VALUE2(bool, \"action useful\", \"hearthstone\") && bot->IsAlive())\n        {\n            return hearth();\n        }" 2 "both long-stuck branches")
require_count("${unstuck_cpp}" "DoSpecificAction(\"hearthstone\"" 1 "the hearthstone start only in the helper")

# Zone escape: each run from the react delay; the hearthstone step holds, watched.
require_text("${escape_cpp}" "bool ZoneEscapeAction::Execute(Event& event)\n{\n    // twow-repo#333 finding 4: each run starts from the react delay" "zone escape starts each run from the react delay")
require_text("${escape_cpp}" "            SetDuration(teleport_cast::HoldFor(ai, bot, teleport_cast::kHearthstone, sPlayerbotAIConfig.reactDelay));" "zone escape holds for the hearthstone")

# No hold without the watch: every hold in the three actions goes through HoldFor.
require_count("${move_cpp}" "HoldMs(" 0 "no unwatched hold in MovementActions.cpp")
require_count("${unstuck_cpp}" "HoldMs(" 0 "no unwatched hold in UnstuckAction.cpp")
require_count("${escape_cpp}" "HoldMs(" 0 "no unwatched hold in ZoneEscapeActions.cpp")

# The hearthstone is recorded as the last spell cast, so SMSG_SPELL_FAILURE -> PlayerbotAI::SpellInterrupted also resets the wait.
require_text("${use_cpp}" "#include \"playerbot/strategy/values/LastSpellCastValue.h\"" "the LastSpellCast include in UseItemAction.cpp")
require_text("${use_cpp}" "        if (Spell* const spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))\n            AI_VALUE(LastSpellCast&, \"last spell cast\").Set(spell->m_spellInfo->Id, bot->GetObjectGuid(), time(nullptr));" "the hearthstone recorded as the last spell cast")
string(FIND "${use_cpp}" "bool UseHearthStoneAction::Execute(Event& event)" hs_at)
string(FIND "${use_cpp}" "bool UseHearthStoneAction::isUseful()" hs_end)
string(FIND "${use_cpp}" "AI_VALUE(LastSpellCast&, \"last spell cast\").Set(" set_at)
if(hs_at EQUAL -1 OR hs_end EQUAL -1 OR set_at EQUAL -1 OR NOT hs_at LESS set_at OR NOT set_at LESS hs_end)
  message(FATAL_ERROR "#333 finding 4 teleport cast wait: the last spell cast is recorded inside UseHearthStoneAction::Execute")
endif()
