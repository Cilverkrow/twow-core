if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.3 (twow-repo#455): a teaching spell is a self-cast of the player only with
# visual 222 and TARGET_UNIT_CASTER; visual 222 with target 0 (Turtle 47312 and the clones
# 61213-61220) is cast by the trainer, so the player's own cast cannot hang. Money is still
# taken only when the learning cast was accepted.
file(READ "${TW_CORE_ROOT}/src/game/Handlers/NPCHandler.cpp" npc)

foreach (required
    "if (proto->SpellVisual == 222 && proto->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_CASTER)"
    "spell = new Spell(_player, proto, false);"
    "spell = new Spell(unit, proto, seatedTrainer);"
    "if (cast_result == SPELL_CAST_OK)")
  string(FIND "${npc}" "${required}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Trainer teaching cast: missing ${required}")
  endif()
endforeach()

# The old condition (visual 222 alone) must not come back.
string(REGEX MATCH "if \(proto->SpellVisual == 222\)" old "${npc}")
if (old)
  message(FATAL_ERROR "Trainer teaching cast: visual 222 alone must not select the player self-cast")
endif()

# Money only inside the accepted branch.
string(FIND "${npc}" "if (cast_result == SPELL_CAST_OK)" ok_at)
string(FIND "${npc}" "_player->ModifyMoney(-int32(nSpellCost));" money_at)
if (money_at EQUAL -1 OR money_at LESS ok_at)
  message(FATAL_ERROR "Trainer teaching cast: money must be taken only after SPELL_CAST_OK")
endif()

message(STATUS "TRAINER_TEACH_CAST_CONTRACT=PASS")
