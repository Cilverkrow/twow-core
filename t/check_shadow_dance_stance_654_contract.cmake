if (NOT DEFINED TW_CORE_ROOT)
  message(FATAL_ERROR "TW_CORE_ROOT is required")
endif()

# twow-repo#654 (WS10-ROGUE-TANK-STANCE-01, owner 29.10.2026): Shadow Dance as a rogue tank stance behind
# Funserver.Rogue.ShadowDanceStance (0 = as before, default; 1 = every rogue; 2 = bots only until the client patch).
# - the switch: dist key, read (0..2), startup guard (no stance spells 61230-61232 -> 0) and the same check at use time;
# - the ranks' proc: covered rogue -> bonus only in the stance (buff + energy helper + threat), nothing without it;
#   not covered -> the old passive bonus;
# - the stance's own script: refused while the switch does not cover the caster, ends an active rogue Stealth;
# - Stealth / Vanish are refused in the stance (by ID: owner "Nur Stealth und Vanish", Shadowmeld stays);
# - who knows the stance: mode 1 learns it with a rank, mode 0 unlearns it, mode 2 leaves it to the bot code;
# - the passive list stays exactly 61143-61145 (the stance is no passive).
# The rules are tested in t/shadow_dance_stance_policy_test.cpp.

function(read_file rel out)
  file(READ "${TW_CORE_ROOT}/${rel}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(require_text text needle what)
  string(FIND "${text}" "${needle}" at)
  if (at EQUAL -1)
    message(FATAL_ERROR "#654 ShadowDanceStance: missing ${what}: ${needle}")
  endif()
endfunction()

function(require_order text first second what)
  string(FIND "${text}" "${first}" a)
  string(FIND "${text}" "${second}" b)
  if (a EQUAL -1 OR b EQUAL -1 OR NOT a LESS b)
    message(FATAL_ERROR "#654 ShadowDanceStance: order ${what}: '${first}' must come before '${second}'")
  endif()
endfunction()

read_file("src/game/FunserverShadowDanceStance.h" policy)
read_file("src/game/World.h" world_h)
read_file("src/game/World.cpp" world_cpp)
read_file("src/mangosd/mangosd.conf.dist.in" dist)
read_file("src/game/Spells/Spell.cpp" spell_cpp)
read_file("src/scripts/spells/spell_rogue.cpp" script)
read_file("src/game/FunserverPassiveSpells.h" passive_h)

# 1. Pure policy (std only), the IDs and the rules.
string(REGEX MATCHALL "#include [<\"][^>\"]+[>\"]" incs "${policy}")
foreach (inc ${incs})
  if (NOT inc STREQUAL "#include <cstdint>")
    message(FATAL_ERROR "#654 ShadowDanceStance: FunserverShadowDanceStance.h is not pure: ${inc}")
  endif()
endforeach()
require_text("${policy}" "SHADOW_DANCE_STANCE                    = 61230," "the stance id")
require_text("${policy}" "SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY = 61231," "the energy helper after a parry")
require_text("${policy}" "SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE = 61232," "the energy helper after a dodge")
require_text("${policy}" "return mode == SHADOW_DANCE_STANCE_ALL || (mode == SHADOW_DANCE_STANCE_BOTS && aiControlled);" "the switch: 1 all, 2 bots only")
require_text("${policy}" "    if (!covered)\n        return ShadowDanceProc::Passive;\n    return stanceUp ? ShadowDanceProc::Stance : ShadowDanceProc::None;" "covered: only in the stance; not covered: the old passive")
require_text("${policy}" "constexpr uint32_t SHADOW_DANCE_STANCE_BLOCKED_SPELLS[] = {\n    1784, 1785, 1786, 1787,   // Stealth R1-R4\n    1856, 1857,               // Vanish R1-R2\n    11327, 11329,             // Vanish R1-R2, the stealth aura they trigger\n};" "Stealth / Vanish by ID (owner: only these two)")
foreach (allowed 20580 45607 3680 11392)
  string(FIND "${policy}" "${allowed}" at)
  string(FIND "${policy}" "constexpr uint32_t SHADOW_DANCE_STANCE_BLOCKED_SPELLS[]" list_at)
  string(SUBSTRING "${policy}" ${list_at} 260 blocked_list)
  string(FIND "${blocked_list}" "${allowed}" in_list)
  if (NOT in_list EQUAL -1)
    message(FATAL_ERROR "#654 ShadowDanceStance: ${allowed} (Shadowmeld / Flashbang / invisibility) must not be blocked")
  endif()
endforeach()
require_text("${policy}" "    if (mode == SHADOW_DANCE_STANCE_OFF)\n        return knowsStance ? ShadowDanceLearn::Unlearn : ShadowDanceLearn::Keep;\n    if (mode == SHADOW_DANCE_STANCE_ALL && knowsRank && !knowsStance)\n        return ShadowDanceLearn::Learn;" "mode 0 unlearns, mode 1 learns with a rank")

# 2. The switch: enum, read 0..2 (default 0), dist key, startup guard after the spells are loaded.
require_text("${world_h}" "    CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE," "the config enum")
require_text("${world_cpp}" "setConfigMinMax(CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE, \"Funserver.Rogue.ShadowDanceStance\", 0, 0, 2);" "the read, default 0, 0..2")
require_text("${dist}" "\nFunserver.Rogue.ShadowDanceStance = 0\n" "dist key, default 0")
require_text("${world_cpp}" "        setConfig(CONFIG_UINT32_FUNSERVER_ROGUE_SHADOW_DANCE_STANCE, 0);" "the startup guard sets 0")
require_order("${world_cpp}" "sSpellMgr.ApplyFunserverPassiveSpells();" "!sSpellMgr.GetSpellEntry(SHADOW_DANCE_STANCE)" "the guard after the spells are loaded")

# 3. Stealth / Vanish are refused in the stance, early in CheckCast.
string(FIND "${spell_cpp}" "SpellCastResult Spell::CheckCast(bool strict)" cc)
string(SUBSTRING "${spell_cpp}" ${cc} 1500 check_cast)
require_text("${check_cast}" "    if (m_casterUnit && ShadowDanceStanceBlocks(m_spellInfo->Id) && m_casterUnit->HasAura(SHADOW_DANCE_STANCE))\n        return SPELL_FAILED_CASTER_AURASTATE;" "the Stealth / Vanish refusal in the stance")
require_order("${check_cast}" "return SPELL_FAILED_NOT_STANDING;" "ShadowDanceStanceBlocks(m_spellInfo->Id)" "the refusal right after the sitting check")
require_text("${spell_cpp}" "#include \"FunserverShadowDanceStance.h\"" "the policy include in Spell.cpp")

# 4. The script: the switch as it acts (0 without the spells), the proc gate, the stance script, the learn hook, registration.
require_text("${script}" "#include \"FunserverShadowDanceStance.h\"" "the policy include in spell_rogue.cpp")
require_text("${script}" "    if (mode && (!sSpellMgr.GetSpellEntry(SHADOW_DANCE_STANCE) || !sSpellMgr.GetSpellEntry(SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY) ||\n                 !sSpellMgr.GetSpellEntry(SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE)))\n        return SHADOW_DANCE_STANCE_OFF;" "no stance spells: the switch acts as 0 (also after a reload)")
require_text("${script}" "    return mode != SHADOW_DANCE_STANCE_OFF && player && ShadowDanceStanceCovers(mode, Script_IsAIControlled(player));" "covered = the switch + AI control (mode 2)")
string(FIND "${script}" "struct spell_rogue_shadow_dance : public AuraScript" sd)
string(SUBSTRING "${script}" ${sd} 2200 proc)
require_text("${proc}" "        ShadowDanceProc const proc = ShadowDanceProcFor(ShadowDanceStanceCoversUnit(owner), owner->HasAura(SHADOW_DANCE_STANCE));\n        if (proc == ShadowDanceProc::None)\n            return SPELL_AURA_PROC_FAILED;" "no stance: no buff, no threat")
require_order("${proc}" "if (proc == ShadowDanceProc::None)" "owner->CastSpell(owner, buff, true, nullptr, aura);" "the gate before the buff")
require_order("${proc}" "if (proc == ShadowDanceProc::None)" "victim->AddThreat(owner, float(threat)" "the gate before the threat")
require_text("${proc}" "        if (proc == ShadowDanceProc::Stance)\n            owner->CastSpell(owner, ShadowDanceEnergyHelperFor(buff), true, nullptr, aura);" "the energy helper only in the stance")
require_text("${script}" "        if (!ShadowDanceStanceCoversUnit(spell->m_casterUnit))\n            return SPELL_FAILED_DONT_REPORT;" "the stance is refused while not covered")
require_text("${script}" "        for (uint32 const stealth : SHADOW_DANCE_STANCE_BLOCKED_SPELLS)\n            caster->RemoveAurasDueToSpell(stealth);" "the stance ends an active rogue Stealth")
require_text("${script}" "PlayerScript(\"ShadowDanceStancePlayerScript\", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEARN_SPELL })" "the learn hook on login and on learning")
require_text("${script}" "            case ShadowDanceLearn::Unlearn:\n                player->RemoveAurasDueToSpell(SHADOW_DANCE_STANCE);\n                player->removeSpell(SHADOW_DANCE_STANCE);" "mode 0: aura and spell removed")
require_text("${script}" "        if (login && mode == SHADOW_DANCE_STANCE_BOTS && !Script_IsAIControlled(player))\n            player->RemoveAurasDueToSpell(SHADOW_DANCE_STANCE);" "mode 2: a real player's leftover stance aura")
require_text("${script}" "    RegisterSpellScript(\"spell_rogue_shadow_dance_stance\", &GetSpellScript<spell_rogue_shadow_dance_stance>);" "the stance script registered")
require_text("${script}" "    new ShadowDanceStancePlayerScript();" "the learn hook registered")

# 5. The stance is no passive: the forced list stays exactly the three ranks.
string(FIND "${passive_h}" "FUNSERVER_FORCED_PASSIVE_SPELLS[] = {" pl)
string(FIND "${passive_h}" "};" pe)
math(EXPR plen "${pe} - ${pl}")
string(SUBSTRING "${passive_h}" ${pl} ${plen} plist)
string(REGEX REPLACE "//[^\n]*" "" plist "${plist}")
string(REGEX MATCHALL "[0-9][0-9][0-9][0-9][0-9]+" pids "${plist}")
if (NOT "${pids}" STREQUAL "61143;61144;61145")
  message(FATAL_ERROR "#654 ShadowDanceStance: the forced passive list must stay 61143-61145 (got '${pids}')")
endif()

message(STATUS "#654 ShadowDanceStance contract passed")
