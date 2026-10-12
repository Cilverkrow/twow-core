# cmake 3.x script mode (Debian trixie / CI): without a policy version TRUE in while()/if() (CMP0012) and
# empty list elements (CMP0007) behave as in cmake 2.x; the host cmake 4.x has them NEW already.
cmake_policy(VERSION 3.16)
# twow-repo#541 (audit A33, card 3, BEHAVIOUR CHANGE, default 0): PlayerbotAI::CanCastSpell (unit
# target) runs Spell::CheckCast(true) on a Spell that never went through Spell::prepare, so
# m_powerCost is 0 and CheckPower passes every cost. Unaffordable spells passed isPossible and the
# trigger checks and failed inside Execute (SpellStart -> SPELL_FAILED_NO_POWER) every tick.
# With AiPlayerbot.CanCastSpell.CheckPower = 1 the cost is computed like prepare (same Spell, mod
# charges handed back) and an unaffordable spellbook spell turns a pass into a fail. Off = old path.
# Pure part: t/spell_power_gate_policy_tests.cpp.
#
# Inputs: -DPB_SOURCE_DIR=<module>/src/playerbot -DCORE_SOURCE_DIR=<module>/../..

function(read_source path out_var)
  file(READ "${path}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR "#541 A33: missing ${description}: ${needle}")
  endif()
endfunction()

function(forbid_text text needle description)
  string(FIND "${text}" "${needle}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR "#541 A33: forbidden ${description}: ${needle}")
  endif()
endfunction()

function(require_order text first second description)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if(a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#541 A33: order ${description}: '${first}' must come before '${second}'")
  endif()
endfunction()

function(require_count text needle expected description)
  set(count 0)
  set(rest "${text}")
  string(LENGTH "${needle}" needle_len)
  while(TRUE)
    string(FIND "${rest}" "${needle}" offset)
    if(offset EQUAL -1)
      break()
    endif()
    math(EXPR count "${count} + 1")
    math(EXPR next "${offset} + ${needle_len}")
    string(SUBSTRING "${rest}" ${next} -1 rest)
  endwhile()
  if(NOT count EQUAL expected)
    message(FATAL_ERROR "#541 A33: ${description}: '${needle}' found ${count}x, expected ${expected}x")
  endif()
endfunction()

function(region text start_marker end_marker out)
  string(FIND "${text}" "${start_marker}" start)
  if(start EQUAL -1)
    message(FATAL_ERROR "#541 A33: missing region start: ${start_marker}")
  endif()
  string(SUBSTRING "${text}" ${start} -1 rest)
  string(FIND "${rest}" "${end_marker}" length)
  if(length EQUAL -1)
    message(FATAL_ERROR "#541 A33: missing region end: ${end_marker}")
  endif()
  string(SUBSTRING "${rest}" 0 ${length} body)
  set(${out} "${body}" PARENT_SCOPE)
endfunction()

read_source("${PB_SOURCE_DIR}/PlayerbotAI.cpp" ai_cpp)
read_source("${PB_SOURCE_DIR}/SpellPowerGatePolicy.h" policy_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.h" config_h)
read_source("${PB_SOURCE_DIR}/PlayerbotAIConfig.cpp" config_cpp)
read_source("${PB_SOURCE_DIR}/aiplayerbot.conf.dist.in" conf_dist)
read_source("${PB_SOURCE_DIR}/RandomPlayerbotMgr.cpp" rnd_mgr)
read_source("${CORE_SOURCE_DIR}/src/game/Spells/Spell.cpp" spell_cpp)
read_source("${CORE_SOURCE_DIR}/src/game/Spells/Spell.h" spell_h)
read_source("${CORE_SOURCE_DIR}/src/game/Objects/Player.cpp" player_cpp)
read_source("${CORE_SOURCE_DIR}/src/game/Objects/Player.h" player_h)

set(unit_sig "bool PlayerbotAI::CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell")
set(go_sig "bool PlayerbotAI::CanCastSpell(uint32 spellid, GameObject* goTarget, uint8 effectMask, bool checkHasSpell")
set(pos_sig "bool PlayerbotAI::CanCastSpell(uint32 spellid, float x, float y, float z, uint8 effectMask, bool checkHasSpell")
region("${ai_cpp}" "${unit_sig}" "${go_sig}" unit_body)
region("${ai_cpp}" "${go_sig}" "${pos_sig}" go_body)
region("${ai_cpp}" "${pos_sig}" "bool PlayerbotAI::CastSpell(std::string name" pos_body)

# --- Switch: default off, read once at config load, documented with value 0.
require_text("${config_h}" "bool canCastSpellChecksPower = false;" "switch member, default off")
require_order("${config_h}" "uint32 aiDelayJitterPct = 0;" "bool canCastSpellChecksPower = false;" "switch after the #541 switches")
require_text("${config_cpp}" "canCastSpellChecksPower = config.GetBoolDefault(\"AiPlayerbot.CanCastSpell.CheckPower\", false);" "switch read, default 0")
require_text("${conf_dist}" "\nAiPlayerbot.CanCastSpell.CheckPower = 0\n" "documented key, value 0")
require_text("${ai_cpp}" "#include \"playerbot/SpellPowerGatePolicy.h\"" "policy include")

# --- Only the unit-target overload, gated by the switch AND checkHasSpell (spellbook casts).
require_count("${ai_cpp}" "sPlayerbotAIConfig.canCastSpellChecksPower" 1 "switch read only in CanCastSpell(unit)")
require_text("${unit_body}" "if (checkHasSpell && sPlayerbotAIConfig.canCastSpellChecksPower)" "gate: spellbook cast + switch")
forbid_text("${go_body}" "spellpower" "power gate in the GameObject overload (scope: unit target only)")
forbid_text("${pos_body}" "spellpower" "power gate in the position overload (scope: unit target only)")
forbid_text("${rnd_mgr}" "canCastSpellChecksPower" "switch in RandomPlayerbotMgr (UpdateAIInternal untouched)")

# --- Cost exactly like Spell::prepare: same Spell (spell mods), charges restored, cheat option,
#     all BEFORE CheckCast, and the temporary Spell deleted after CheckCast as today.
require_count("${ai_cpp}" "Spell::CalculatePowerCost(" 1 "one cost computation")
require_text("${unit_body}" "uint32 powerCost = Spell::CalculatePowerCost(spellInfo, bot, spell, spell->GetCastItem());" "cost with the same Spell and cast item")
forbid_text("${unit_body}" "CalculatePowerCost(spellInfo, bot, nullptr" "cost without the Spell (SPELLMOD_COST, Clearcasting, Inner Focus missed)")
forbid_text("${unit_body}" "CalculatePowerCost(spellInfo, bot)" "cost without the Spell (SPELLMOD_COST, Clearcasting, Inner Focus missed)")
require_order("${unit_body}" "spell->m_targets.setItemTarget(spell->GetCastItem());" "uint32 powerCost = Spell::CalculatePowerCost(" "targets and cast item set before the cost")
require_order("${unit_body}" "uint32 powerCost = Spell::CalculatePowerCost(" "bot->RestoreSpellMods(spell);" "charges handed back right after the cost")
require_order("${unit_body}" "bot->RestoreSpellMods(spell);" "if (bot->HasOption(PLAYER_CHEAT_NO_POWER))" "cheat option after restore")
require_order("${unit_body}" "if (bot->HasOption(PLAYER_CHEAT_NO_POWER))" "SpellCastResult result = spell->CheckCast(true);" "cost before CheckCast")
require_order("${unit_body}" "SpellCastResult result = spell->CheckCast(true);" "delete spell;" "temporary Spell deleted after CheckCast")
require_count("${unit_body}" "bot->RestoreSpellMods(spell);" 1 "exactly one restore (CheckCast's own range mods stay as today)")
string(FIND "${unit_body}" "delete spell;" del_pos)
string(FIND "${unit_body}" "bot->RestoreSpellMods(spell);" res_pos)
if(NOT res_pos LESS del_pos)
  message(FATAL_ERROR "#541 A33: RestoreSpellMods must run before the temporary Spell is deleted")
endif()
require_text("${unit_body}" "bool const healthCost = spellInfo->powerType == POWER_HEALTH;" "health cost")
require_text("${unit_body}" "bool const knownPowerType = spellInfo->powerType < MAX_POWERS;" "known power type")
require_text("${unit_body}" "powerVerdict = ai::spellpower::Evaluate(spell->GetCastItem() != nullptr, healthCost, knownPowerType, powerCost," "policy call")
require_text("${unit_body}" "bot->GetHealth(), knownPowerType ? bot->GetPower(Powers(spellInfo->powerType)) : 0);" "no GetPower for unknown power types")
require_text("${unit_body}" "ai::spellpower::PowerVerdict powerVerdict = ai::spellpower::PowerVerdict::Unchecked;" "verdict defaults to Unchecked (off = old answer)")
# Review 2: cost, restore and verdict must sit INSIDE the switch block, so switch 0 leaves the
# verdict Unchecked (a body moved above an empty `if` must fail here).
require_order("${unit_body}" "if (checkHasSpell && sPlayerbotAIConfig.canCastSpellChecksPower)" "uint32 powerCost = Spell::CalculatePowerCost(" "switch line before the cost")
require_text("${unit_body}" "if (checkHasSpell && sPlayerbotAIConfig.canCastSpellChecksPower)\n    {\n        uint32 powerCost = Spell::CalculatePowerCost(spellInfo, bot, spell, spell->GetCastItem());\n        bot->RestoreSpellMods(spell);" "cost and restore open the switch block")
require_text("${unit_body}" "knownPowerType ? bot->GetPower(Powers(spellInfo->powerType)) : 0);\n    }\n\n    SpellCastResult result = spell->CheckCast(true);" "verdict closes the switch block right before CheckCast")
require_order("${unit_body}" "powerVerdict = ai::spellpower::Evaluate(" "SpellCastResult result = spell->CheckCast(true);" "verdict before CheckCast")
require_count("${unit_body}" "powerVerdict = ai::spellpower::Evaluate(" 1 "one verdict assignment")
require_count("${unit_body}" "powerVerdict = " 2 "verdict only declared (Unchecked) and set in the switch block")

# --- Old mapping kept byte for byte (review 3: every case, default and the gate's return false),
#     the gate only turns a pass into a fail, after checkResult = result.
set(old_mapping_and_gate [=[bool canCast = false;
    switch (result)
    {
        case SPELL_FAILED_NOT_INFRONT:
        case SPELL_FAILED_NOT_STANDING:
        case SPELL_FAILED_UNIT_NOT_INFRONT:
        case SPELL_FAILED_MOVING:
        case SPELL_FAILED_TRY_AGAIN:
        case SPELL_CAST_OK:
            canCast = true;
            break;
        case SPELL_FAILED_OUT_OF_RANGE:
        case SPELL_FAILED_LINE_OF_SIGHT:
            canCast = ignoreRange;
            break;
        case SPELL_FAILED_AFFECTING_COMBAT:
            canCast = ignoreInCombat;
            break;
        case SPELL_FAILED_NOT_MOUNTED:
            canCast = ignoreMount;
            break;
        default:
            break;
    }

    // twow-repo#541 (audit A33): only a pass can turn into a fail (Unchecked = switch off, cast item, unknown
    // power type keeps the old answer); checkResult changes only in that case.
    if (canCast && ai::spellpower::Blocks(powerVerdict))
    {
        if (checkResult)
        {
            *checkResult = powerVerdict == ai::spellpower::PowerVerdict::NoHealth ? SPELL_FAILED_CASTER_AURASTATE : SPELL_FAILED_NO_POWER;
        }

        return false;
    }

    return canCast;
}]=])
require_text("${unit_body}" "${old_mapping_and_gate}" "exact old result mapping + gate block (switch 0 = old answer, gate returns false)")
require_count("${unit_body}" "bool canCast = false;" 1 "one result mapping")
require_count("${unit_body}" "switch (result)" 1 "one result switch")
forbid_text("${unit_body}" "return ignoreRange;" "direct return that bypasses the gate")
forbid_text("${unit_body}" "case SPELL_CAST_OK:\n            return true;" "direct return that bypasses the gate")
require_order("${unit_body}" "*checkResult = result;" "if (canCast && ai::spellpower::Blocks(powerVerdict))" "gate after the CheckCast result")
require_order("${unit_body}" "if (canCast && ai::spellpower::Blocks(powerVerdict))" "return canCast;" "gate before the final return")
require_text("${unit_body}" "*checkResult = powerVerdict == ai::spellpower::PowerVerdict::NoHealth ? SPELL_FAILED_CASTER_AURASTATE : SPELL_FAILED_NO_POWER;" "core result codes")
# GameObject / position overloads unchanged (old direct returns still there).
require_text("${go_body}" "return ignoreRange;" "GameObject overload unchanged")
require_text("${pos_body}" "return ignoreRange;" "position overload unchanged")

# --- Policy header: pure, no state, core boundaries.
forbid_text("${policy_h}" "#include \"" "core/module include in the pure policy")
forbid_text("${policy_h}" "static " "static state in the policy")
forbid_text("${policy_h}" "thread_local" "thread-local state in the policy")
forbid_text("${policy_h}" "shared_mutex" "shared_mutex")
require_text("${policy_h}" "return health <= cost ? PowerVerdict::NoHealth : PowerVerdict::Affordable;" "core health boundary (<=)")
require_text("${policy_h}" "return power < cost ? PowerVerdict::NoPower : PowerVerdict::Affordable;" "core power boundary (<)")
require_order("${policy_h}" "if (hasCastItem)" "if (healthCost)" "cast item first, like CheckPower")
require_text("${policy_h}" "return verdict == PowerVerdict::NoPower || verdict == PowerVerdict::NoHealth;" "only real shortfalls block")

# --- Core facts this mirror depends on (fail here = re-check the mirror).
require_text("${spell_h}" "uint32 m_powerCost = 0;" "m_powerCost starts at 0 (the bug)")
require_text("${spell_h}" "static uint32 CalculatePowerCost(SpellEntry const* spellInfo, Unit* caster, Spell* spell = nullptr, Item* castItem = nullptr);" "public static cost function")
require_text("${spell_cpp}" "m_powerCost = CalculatePowerCost(m_spellInfo, m_casterUnit, this, m_CastItem);" "prepare computes the cost with the Spell")
require_text("${spell_cpp}" "if (pPlayerCaster->HasOption(PLAYER_CHEAT_NO_POWER))" "prepare cheat option")
region("${spell_cpp}" "SpellCastResult Spell::CheckPower() const" "bool Spell::IgnoreItemRequirements() const" check_power)
require_text("${check_power}" "if (m_CastItem || m_IsTriggeredSpell || !m_casterUnit)" "CheckPower skips cast items")
require_text("${check_power}" "if (m_casterUnit->GetHealth() <= m_powerCost)\n            return SPELL_FAILED_CASTER_AURASTATE;" "CheckPower health rule")
require_text("${check_power}" "if (m_casterUnit->GetPower(powerType) < m_powerCost)\n        return SPELL_FAILED_NO_POWER;" "CheckPower power rule")
region("${spell_cpp}" "uint32 Spell::CalculatePowerCost(" "void Spell::OnSpellCritChanceCalculate" calc_cost)
require_text("${calc_cost}" "if (spell)\n        if (Player* modOwner = caster->GetSpellModOwner())\n            modOwner->ApplySpellMod(spellInfo->Id, SPELLMOD_COST, powerCost, spell);" "spell mods only with a Spell")
require_text("${player_h}" "void RestoreSpellMods(Spell* spell, uint32 ownerAuraId = 0, Aura* aura = nullptr);" "public RestoreSpellMods")
require_text("${player_cpp}" "if (!spell || spell->HasModifierApplied(mod))\n        return;" "DropModCharge records into the Spell")
region("${player_cpp}" "void Player::RestoreSpellMods(Spell* spell, uint32 ownerAuraId, Aura* aura)" "void Player::RestoreAllSpellMods" restore)
require_text("${restore}" "if (mod->charges == -1)\n                mod->charges = 1;\n            else\n                mod->charges++;" "restore gives the charge back")

message(STATUS "cancast_power_gate source contract passed")
