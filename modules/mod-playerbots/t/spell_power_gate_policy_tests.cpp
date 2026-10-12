// twow-repo#541 (audit A33): pure mirror of core Spell::CheckPower used by PlayerbotAI::CanCastSpell behind
// AiPlayerbot.CanCastSpell.CheckPower. Boundaries follow the core exactly: power '<' cost fails,
// health '<=' cost fails, cast items and unknown power types are not checked.
#include "SpellPowerGatePolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::spellpower;

    // Mana / rage / energy / focus: core 'GetPower(powerType) < m_powerCost' -> NO_POWER.
    Require(Evaluate(false, false, true, 100, 500, 99) == PowerVerdict::NoPower, "one short of the cost fails");
    Require(Evaluate(false, false, true, 100, 500, 100) == PowerVerdict::Affordable, "exactly the cost passes");
    Require(Evaluate(false, false, true, 100, 500, 4000) == PowerVerdict::Affordable, "plenty passes");
    Require(Evaluate(false, false, true, 0, 500, 0) == PowerVerdict::Affordable, "free cast (Clearcasting, cost 0) passes at 0 power");
    Require(Evaluate(false, false, true, 30, 1, 0) == PowerVerdict::NoPower, "rage 0 for a 30 rage ability fails");

    // Health as power: core 'GetHealth() <= m_powerCost' -> CASTER_AURASTATE.
    Require(Evaluate(false, true, false, 100, 100, 0) == PowerVerdict::NoHealth, "health equal to the cost fails");
    Require(Evaluate(false, true, false, 100, 101, 0) == PowerVerdict::Affordable, "health above the cost passes");
    Require(Evaluate(false, true, false, 0, 1, 0) == PowerVerdict::Affordable, "zero health cost passes");

    // Not checked, exactly where the core skips the check (or reports DONT_REPORT).
    Require(Evaluate(true, false, true, 100, 500, 0) == PowerVerdict::Unchecked, "cast item: cost not used");
    Require(Evaluate(true, true, false, 100, 1, 0) == PowerVerdict::Unchecked, "cast item beats health cost");
    Require(Evaluate(false, false, false, 100, 500, 0) == PowerVerdict::Unchecked, "unknown power type keeps the old answer");

    // Gate: only NoPower / NoHealth block; it never turns a fail into a pass.
    Require(Blocks(PowerVerdict::NoPower), "NoPower blocks");
    Require(Blocks(PowerVerdict::NoHealth), "NoHealth blocks");
    Require(!Blocks(PowerVerdict::Affordable), "Affordable does not block");
    Require(!Blocks(PowerVerdict::Unchecked), "Unchecked (switch off) does not block");

    std::cout << "spell_power_gate_policy_tests passed\n";
    return 0;
}
