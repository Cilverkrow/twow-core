#pragma once

// twow-repo#541 (audit A33, AiPlayerbot.CanCastSpell.CheckPower, default 0): PlayerbotAI::CanCastSpell runs
// Spell::CheckCast(true) on a Spell that never went through Spell::prepare, so Spell::m_powerCost is
// still 0 and CheckPower passes every spell. The bot then only learns in Execute (SpellStart ->
// prepare -> SPELL_FAILED_NO_POWER) that it cannot afford the cast, every reaction tick.
//
// This header is the pure part of the fix: a mirror of core Spell::CheckPower (Spells/Spell.cpp) for
// a bot's own, non-triggered spellbook cast, plus the gate rule. No core includes, so
// t/spell_power_gate_policy_tests.cpp tests it without the game library. The caller computes the
// cost exactly like prepare (Spell::CalculatePowerCost with the same Spell, then RestoreSpellMods).

#include <cstdint>

namespace ai
{
namespace spellpower
{
    enum class PowerVerdict : std::uint8_t
    {
        Unchecked,   // switch off, cast item (core: cost not used) or unknown power type: old result stays
        Affordable,
        NoPower,     // core CheckPower: SPELL_FAILED_NO_POWER
        NoHealth     // POWER_HEALTH cost, core CheckPower: SPELL_FAILED_CASTER_AURASTATE
    };

    // hasCastItem: Spell::GetCastItem() != nullptr; healthCost: powerType == POWER_HEALTH;
    // knownPowerType: powerType < MAX_POWERS; power: GetPower(powerType) (0 when not knownPowerType).
    inline PowerVerdict Evaluate(bool hasCastItem, bool healthCost, bool knownPowerType, std::uint32_t cost, std::uint32_t health, std::uint32_t power)
    {
        if (hasCastItem)
            return PowerVerdict::Unchecked;

        if (healthCost)
            return health <= cost ? PowerVerdict::NoHealth : PowerVerdict::Affordable;

        if (!knownPowerType)
            return PowerVerdict::Unchecked;

        return power < cost ? PowerVerdict::NoPower : PowerVerdict::Affordable;
    }

    // The gate only ever turns a pass into a fail; it never turns a fail into a pass.
    inline bool Blocks(PowerVerdict verdict)
    {
        return verdict == PowerVerdict::NoPower || verdict == PowerVerdict::NoHealth;
    }
}
}
