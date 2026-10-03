if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#295 (owner 2026-10-02): riding in four stages, mount speed of players and bots by
# riding rank and mount family, stronger slows and longer roots of player-controlled casters
# (never of NPCs, nor of players or bots charmed by an NPC).
# Pure rules: t/riding_stages_policy_test.cpp. This contract locks the hooks, the caster rule and
# the default-off configuration.

function(read_source path out_var)
  file(READ "${TW_CORE_ROOT}/${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (offset EQUAL -1)
    message(FATAL_ERROR "#295: missing ${label}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle label)
  string(FIND "${text}" "${needle}" offset)
  if (NOT offset EQUAL -1)
    message(FATAL_ERROR "#295: forbidden ${label}: ${needle}")
  endif()
endfunction()

# Text from the start of begin_marker up to (not including) end_marker.
function(extract_between text begin_marker end_marker out_var)
  string(FIND "${text}" "${begin_marker}" begin_at)
  if (begin_at EQUAL -1)
    message(FATAL_ERROR "#295: missing ${begin_marker}")
  endif()
  string(SUBSTRING "${text}" ${begin_at} -1 rest)
  string(FIND "${rest}" "${end_marker}" end_at)
  if (end_at EQUAL -1)
    message(FATAL_ERROR "#295: missing ${end_marker} after ${begin_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${end_at} body)
  set(${out_var} "${body}" PARENT_SCOPE)
endfunction()

function(require_before text first second label)
  string(FIND "${text}" "${first}" first_at)
  string(FIND "${text}" "${second}" second_at)
  if (first_at EQUAL -1 OR second_at EQUAL -1 OR NOT first_at LESS second_at)
    message(FATAL_ERROR "#295: ${label}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_source("src/game/FunserverRidingStages.h" policy)
read_source("src/game/FunserverPlayerSnare.h" snare_h)
read_source("src/game/Spells/SpellAuras.cpp" auras)
read_source("src/game/Spells/SpellAuras.h" auras_h)
read_source("src/game/Objects/Player.cpp" player)
read_source("src/game/Objects/Object.cpp" object)
read_source("src/game/Spells/Spell.cpp" spell)
read_source("src/game/Spells/SpellEffects.cpp" effects)
read_source("src/game/World.h" world_h)
read_source("src/game/World.cpp" world)
read_source("src/mangosd/mangosd.conf.dist.in" config)

# The policy header stays pure (shared with the playerbots) and keeps the neutral defaults.
forbid_text("${policy}" "#include \"" "server include in the pure policy header")
foreach (required
    "inline int32_t MountedSpeedPct(" "inline int32_t ScaleSlow(" "inline int32_t ScaleRootDuration("
    "DEFAULT_SLOW_PCT = 0" "DEFAULT_MAX_SLOW_PCT = 90" "DEFAULT_ROOT_DURATION_PCT = 0")
  require_text("${policy}" "${required}" "policy")
endforeach()

# 1. Mounted speed: the Turtle hook asks the policy on apply and never unmounts.
require_text("${auras_h}" "int32 CalculateRidingMountSpeed(Player const* player) const;" "riding speed helper")
extract_between("${auras}" "int32 Aura::CalculateRidingMountSpeed(" "void Aura::HandleAuraModIncreaseMountedSpeed(" helper)
foreach (required
    "FunserverRiding::MountedSpeedPct(" "CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED"
    "GetSkillValuePure(SKILL_RIDING)" "CalculateSimpleValue(GetEffIndex())"
    "SPELL_CUSTOM_MOUNT_SPEED_100" "SPELL_CUSTOM_IGNORE_RIDING_SKILL_MOUNT_SPEED"
    "EffectApplyAuraName[EFFECT_INDEX_0] != SPELL_AURA_MOUNTED"
    "m_modifier.m_auraname != SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED")
  require_text("${helper}" "${required}" "mount speed helper")
endforeach()
extract_between("${auras}" "void Aura::HandleAuraModIncreaseMountedSpeed(" "void Aura::HandleAuraModIncreaseSwimSpeed(" handler)
require_text("${handler}" "if (apply)" "apply-only computation")
require_text("${handler}" "m_modifier.m_amount = CalculateRidingMountSpeed(player);" "policy call")
require_text("${handler}" "GetTarget()->UpdateSpeed(MOVE_RUN, false, GetTarget()->GetSpeedRatePersistance(MOVE_RUN));" "speed update")
foreach (forbidden "Unmount" "RemoveSpellsCausingAura" "GetSkillValue(" "switch (")
  forbid_text("${handler}" "${forbidden}" "inherited mount speed switch")
endforeach()

# 2. A riding rank changed while mounted sets the mount speed again at once.
extract_between("${player}" "void Player::SetSkill(uint16 id, uint16 currVal, uint16 maxVal, uint16 step" "bool Player::HasSkill(uint16 id) const" set_skill)
foreach (required
    "if (id == SKILL_RIDING && IsMounted())" "GetAurasByType(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)"
    "aura->CalculateRidingMountSpeed(this)" "UpdateSpeed(MOVE_RUN, false, GetSpeedRatePersistance(MOVE_RUN));"
    "[RidingStages]")
  require_text("${set_skill}" "${required}" "SetSkill riding hook")
endforeach()

# 3. Riding 225/300 count like 150 where the old code tested exactly 150.
forbid_text("${effects}" "GetSkillValue(SKILL_RIDING) == 150" "Plainsrunning exact riding check")
string(REGEX MATCHALL "GetSkillValue\\(SKILL_RIDING\\) >= 150" plainsrunning "${effects}")
list(LENGTH plainsrunning plainsrunning_count)
if (NOT plainsrunning_count EQUAL 3)
  message(FATAL_ERROR "#295: Plainsrunning needs 3 checks of riding >= 150 (found ${plainsrunning_count})")
endif()
forbid_text("${player}" "RequiredSkillRank == 150" "legacy riding conversion exact check")
require_text("${player}" "(proto->RequiredSkillRank >= 150)" "legacy riding conversion")

# 4. One caster rule for both snare hooks: player-controlled (player, bot, their pets, totems,
# traps, units they charm), except a unit charmed by a non-player. IsControlledByPlayer() is true
# for every Player, so without the charm test a player or bot under an NPC's Dominate Mind or
# Chains of Kel'Thuzad would snare like a player with the spells the NPC AI makes it cast.
require_text("${snare_h}" "bool IsPlayerSnareCaster(WorldObject const* caster);" "snare caster rule declaration")
extract_between("${object}" "bool FunserverSnare::IsPlayerSnareCaster(WorldObject const* caster)\n{" "\n}\n" caster_rule)
foreach (required
    "if (!caster || !caster->IsControlledByPlayer())\n        return false;"
    "if (Unit const* unit = caster->ToUnit())"
    "ObjectGuid const charmerGuid = unit->GetCharmerGuid();"
    "if (!charmerGuid.IsEmpty() && !charmerGuid.IsPlayer())\n            return false;")
  require_text("${caster_rule}" "${required}" "snare caster rule (NPC-charmed casters are not player casters)")
endforeach()
# The positive path is locked too: the rule ends with "return true;" and has exactly these three
# returns, so neither an early "return true;" nor a final "return false;" can slip in.
require_text("${caster_rule}" "            return false;\n    }\n\n    return true;" "snare caster rule (player casters pass)")
string(REGEX MATCHALL "return " caster_returns "${caster_rule}")
list(LENGTH caster_returns caster_return_count)
if (NOT caster_return_count EQUAL 3)
  message(FATAL_ERROR "#295: the snare caster rule needs exactly 3 returns (found ${caster_return_count})")
endif()

# 5. Slows of player-controlled casters: scaled once where the aura amount is calculated,
# after the caster's spell mods, only for aura 33 and never on the caster itself.
extract_between("${object}" "int32 WorldObject::CalculateSpellDamage(" "void WorldObject::CalculateSpellDamage(SpellNonMeleeDamage*" calc)
foreach (required
    "FunserverSnare::ScaleSlow(" "value < 0 && spellProto->EffectApplyAuraName[effect_index] == SPELL_AURA_MOD_DECREASE_SPEED"
    "target && target != this && FunserverSnare::IsPlayerSnareCaster(this)"
    "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_SLOW_PCT" "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_MAX_SLOW_PCT")
  require_text("${calc}" "${required}" "slow hook")
endforeach()
require_before("${calc}" "SPELLMOD_SPEED, value, spell" "FunserverSnare::ScaleSlow(" "slow scaled after the spell mods")
extract_between("${calc}" "value < 0 && spellProto->EffectApplyAuraName[effect_index] == SPELL_AURA_MOD_DECREASE_SPEED" "FunserverSnare::ScaleSlow(" slow_guard)
forbid_text("${slow_guard}" "IsControlledByPlayer" "bare player-control check in the slow hook (counts NPC-charmed players)")

# 6. Roots of player-controlled casters last longer, before diminishing returns.
extract_between("${spell}" "void Spell::DoSpellHitOnUnit(Unit *unit, uint32 effectMask)" "void Spell::DoAllEffectOnTarget(GOTargetInfo *target)" hit)
foreach (required
    "FunserverSnare::ScaleRootDuration(" "m_auraname == SPELL_AURA_MOD_ROOT"
    "pRealCaster != unit && FunserverSnare::IsPlayerSnareCaster(pRealCaster)"
    "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_ROOT_DURATION_PCT"
    "m_spellAuraHolder->SetAuraMaxDuration(duration);")
  require_text("${hit}" "${required}" "root hook")
endforeach()
require_before("${hit}" "FunserverSnare::ScaleRootDuration(" "unit->ApplyDiminishingToDuration(" "root scaled before diminishing returns")
require_before("${hit}" "FunserverSnare::ScaleRootDuration(" "unit->AddSpellAuraHolder(m_spellAuraHolder)" "root duration set before the aura is added")
extract_between("${hit}" "if (duration > 0 && pRealCaster" "FunserverSnare::ScaleRootDuration(" root_guard)
forbid_text("${root_guard}" "IsControlledByPlayer" "bare player-control check in the root hook (counts NPC-charmed players)")

# 7. Configuration: neutral defaults in code and in the shipped configuration.
foreach (required
    "CONFIG_BOOL_FUNSERVER_RIDING_STAGES_ENABLED," "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_SLOW_PCT,"
    "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_MAX_SLOW_PCT," "CONFIG_UINT32_FUNSERVER_PLAYER_SNARE_ROOT_DURATION_PCT,")
  require_text("${world_h}" "${required}" "config enum")
endforeach()
foreach (required
    "\"Funserver.Riding.Stages.Enabled\", false)"
    "\"Funserver.PlayerSnare.SlowPct\", FunserverSnare::DEFAULT_SLOW_PCT, 0, 100)"
    "\"Funserver.PlayerSnare.MaxSlowPct\", FunserverSnare::DEFAULT_MAX_SLOW_PCT, 0, 100)"
    "\"Funserver.PlayerSnare.RootDurationPct\", FunserverSnare::DEFAULT_ROOT_DURATION_PCT, 0, 200)")
  require_text("${world}" "${required}" "default-off config load")
endforeach()
foreach (required
    "\nFunserver.Riding.Stages.Enabled = 0\n" "\nFunserver.PlayerSnare.SlowPct = 0\n"
    "\nFunserver.PlayerSnare.MaxSlowPct = 90\n" "\nFunserver.PlayerSnare.RootDurationPct = 0\n")
  require_text("${config}" "${required}" "neutral default in mangosd.conf.dist.in")
endforeach()

message(STATUS "RIDING_STAGES_295_CONTRACT=PASS")
