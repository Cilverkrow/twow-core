if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# Hotfix 8.6 (twow-repo#367, #471): Flowing Blades tells the client the shortened cooldown,
# [GhostlyEvasion] measuring point, bots loot a corpse before they skin it.
file(READ "${TW_CORE_ROOT}/src/scripts/spells/spell_rogue.cpp" rogue)
file(READ "${TW_CORE_ROOT}/src/game/Objects/Object.cpp" object)
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/src/playerbot/LootObjectStack.cpp" stack)
file(READ "${TW_CORE_ROOT}/modules/mod-playerbots/src/playerbot/strategy/actions/LootAction.cpp" loot)

foreach (pair
    "rogue|player->SendClearCooldown(spellId, player)"
    "rogue|player->SendSpellCooldown(spellId, uint32(left - seconds) * IN_MILLISECONDS, player->GetObjectGuid())"
    "object|TraceGhostlyEvasion(pVictim->GetGUIDLow(), dodgeChance, dodged)"
    "rogue|TraceRogueTalent(caster, \"vigorous_fury\""
    "rogue|TraceRogueTalent(caster, \"deep_wounds\""
    "rogue|TraceRogueTalent(owner, \"shadow_edge\", true"
    "stack|(TARGET_NOT_LOOTED). Normal loot first"
    "loot|if (creature && creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 var)
  list(GET parts 1 needle)
  string(FIND "${${var}}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "Hotfix 8.6: missing in ${var}: ${needle}")
  endif()
endforeach()

string(FIND "${loot}" "UNIT_DYNFLAG_LOOTABLE) && !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))" old_rule)
if (NOT old_rule EQUAL -1)
  message(FATAL_ERROR "Hotfix 8.6: a skinnable corpse must still be looted first")
endif()

# Vigorous Fury: the energy return cannot trigger, so OnHit never runs for it.
string(FIND "${rogue}" "struct spell_rogue_vigor_energy" vigor_at)
string(SUBSTRING "${rogue}" ${vigor_at} 900 vigor)
string(FIND "${vigor}" "void OnEffectExecuted(Spell* spell, SpellEffectIndex effIdx) const override" vigor_effect)
string(FIND "${vigor}" "void OnHit(" vigor_hit)
if (vigor_effect EQUAL -1 OR NOT vigor_hit EQUAL -1)
  message(FATAL_ERROR "Hotfix 8.6: Vigorous Fury must run in OnEffectExecuted, not OnHit")
endif()

message(STATUS "HOTFIX86_CONTRACT=PASS")
