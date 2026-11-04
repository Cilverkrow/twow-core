// twow-repo#602 ROT-01 (owner 04.11.2026, OB-00 plan OK): AiPlayerbot.Rotation.KnownSpellNodes - a trigger node is skipped only
// when every handler is an unknown spell action without alternatives; the cached answer is taken again after a known-spell stamp
// change and at least every RecheckMs.
#include <cstdlib>
#include <iostream>

#include "KnownSpellNodesPolicy.h"

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
    using namespace ai::known_spell_nodes;

    Handler unknownSpell;
    unknownSpell.spellAction = true;
    Require(Dead(unknownSpell), "an unknown spell action without alternatives is dead");

    Handler known = unknownSpell;
    known.spellKnown = true;
    Require(!Dead(known), "a known spell is checked");

    Handler withAlternatives = unknownSpell;
    withAlternatives.hasAlternatives = true;
    Require(!Dead(withAlternatives), "an unknown spell with alternatives is checked (its IMPOSSIBLE pushes them)");

    Handler keeps = unknownSpell;
    keeps.keepsUnknown = true;
    Require(!Dead(keeps), "a runtime / vehicle / mount spell keeps its full checks");

    Handler notSpell;
    Require(!Dead(notSpell), "a non-spell action (loot, move, item) is checked");

    // Recheck: never taken, a stamp change, or older than RecheckMs; otherwise the cached answer.
    Require(Recheck(false, 0, 7, 1000, 0), "a node never taken is taken");
    Require(Recheck(true, 7, 8, 1000, 900), "a stamp change takes the node again (learn / level-up / talents)");
    Require(!Recheck(true, 7, 7, 1000 + RecheckMs - 1, 1000), "same stamp within RecheckMs keeps the answer");
    Require(Recheck(true, 7, 7, 1000 + RecheckMs, 1000), "same stamp after RecheckMs takes it again (stale spell id)");
    // getMSTime wraps: 0xFFFFF000 -> 0x1000 is 8192 ms (keep), -> 0x3000 is 16384 ms (take again).
    Require(!Recheck(true, 7, 7, 0x1000u, 0xFFFFF000u), "8192 ms across the wrap keeps the answer");
    Require(Recheck(true, 7, 7, 0x3000u, 0xFFFFF000u), "16384 ms across the wrap takes it again");

    // Counters start at zero and count up.
    Counters& c = Global();
    Require(c.skipped[1].load() == 0 && c.recomputes.load() == 0, "counters start at 0");
    c.skipped[1].fetch_add(3);
    Require(c.skipped[1].exchange(0) == 3 && c.skipped[1].load() == 0, "counters are increments");

    std::cout << "known_spell_nodes_policy_tests: ok\n";
    return 0;
}
