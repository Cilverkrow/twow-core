// A3 step 5 (ADR-0041; OB-00 07.11.2026): the combat rule, pure parts (CombatRulePolicy.h).
// Part HealerStart (AiPlayerbot.CombatRule.HealerStart, default 0): the healer gate keeps the old rule unchanged and opens only
// for a holy paladin or a restoration druid, and only where the part applies.
#include <cstdlib>
#include <iostream>

#include "CohortPolicy.h"
#include "CombatRulePolicy.h"

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
    using namespace ai::combat_rule;

    // The old rule: no heal strategy, or offdps in the combat engine - whatever the part says.
    for (int applies = 0; applies < 2; ++applies)
        for (int candidate = 0; candidate < 2; ++candidate)
        {
            Require(HealerMayAttack(false, false, applies, candidate), "no heal strategy: may attack");
            Require(HealerMayAttack(true, true, applies, candidate), "healer with offdps: may attack");
        }

    // A healer without offdps: refused unless the part applies to a healer of the part.
    Require(!HealerMayAttack(true, false, false, false), "healer, part off: refused (the old rule)");
    Require(!HealerMayAttack(true, false, false, true), "holy paladin / resto druid, part off: refused (the old rule)");
    Require(!HealerMayAttack(true, false, true, false), "another healer (priest, shaman without offdps), part on: refused");
    Require(HealerMayAttack(true, false, true, true), "holy paladin / resto druid, part on: may attack");

    // Which healers the part is for.
    Require(HealerWithoutOffdpsCreator(ClassPaladin, true, false), "paladin with holy");
    Require(HealerWithoutOffdpsCreator(ClassDruid, false, true), "druid with restoration");
    Require(!HealerWithoutOffdpsCreator(ClassPaladin, false, true), "paladin without holy (restoration is no paladin tree)");
    Require(!HealerWithoutOffdpsCreator(ClassDruid, true, false), "druid without restoration");
    Require(!HealerWithoutOffdpsCreator(5, true, false), "priest (5) holy: has an offdps creator, not the part's");
    Require(!HealerWithoutOffdpsCreator(7, false, true), "shaman (7) restoration: has an offdps creator (#653), not the part's");
    Require(!HealerWithoutOffdpsCreator(ClassPaladin, false, false), "retribution / protection paladin");
    Require(!HealerWithoutOffdpsCreator(ClassDruid, false, false), "balance / feral druid");

    // Modes come from CohortPolicy: 0 off, 1 all, 2 cohort B (odd guid), 3 cohort A (even guid), other = off.
    Require(!ai::cohort::Applies(0, 3) && ai::cohort::Applies(1, 4), "mode 0 off, mode 1 all");
    Require(ai::cohort::Applies(2, 3) && !ai::cohort::Applies(2, 4), "mode 2 = cohort B only");
    Require(ai::cohort::Applies(3, 4) && !ai::cohort::Applies(3, 3), "mode 3 = cohort A only");
    Require(!ai::cohort::Applies(4, 3) && !ai::cohort::Applies(4, 4), "unknown mode is off");

    // Counters start at zero and count up.
    HealerStartCounters& c = HealerStartGlobal();
    Require(c.asked[0].load() == 0 && c.lifted[1].load() == 0 && c.liftedCls[ClassDruid].load() == 0, "counters start at 0");
    c.lifted[1].fetch_add(2);
    Require(c.lifted[1].exchange(0) == 2 && c.lifted[1].load() == 0, "counters are increments");
    Require(ClassDruid < MaxClass, "the druid fits the class counters");

    std::cout << "combat_rule_policy_tests: ok\n";
    return 0;
}
