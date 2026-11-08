#pragma once

#include <atomic>
#include <cstdint>

namespace ai::combat_rule
{
// A3 step 5 (ADR-0041; OB-00 07.11.2026, design A3-S5-KAMPFREGEL-ENTWURF.md): the combat rule - one module that owns the combat
// decisions the old trigger ladder spreads over many places (who may start a fight, rest before the pull, target choice, flee
// point, loop guard). Each part has its own switch AiPlayerbot.CombatRule.<Part> with the CohortPolicy modes (0 off = default and
// rollback, 1 all, 2 cohort B, 3 cohort A); the old call site only asks the rule, which computes the old expression unchanged
// while the part is off. Pure rules, no game types, no config reads.

// ---------------------------------------------------------------------------------------------------------------------------
// Part HealerStart (AiPlayerbot.CombatRule.HealerStart; A3-AKTIONS-VERDRAHTUNG v1.1 §0.2 point 5, F2 (a)).
//
// The healer gate of attack my target, attack RTI target and attack anything: a bot with a heal strategy attacks only when its
// combat engine holds "offdps". AiFactory adds "offdps" to a holy paladin and a restoration druid too, but only the priest and
// shaman contexts create it, so the engine drops it and those two never start a fight while they keep their heal strategy (in a
// group; a solo free bot swaps holy -> retribution and restoration -> balance in AiFactory, so it is no healer any more). The
// rule owns the bit instead of adding creators: with the part on, these two healers may attack like a healer with offdps. It
// lifts only the gate - no trigger, action, multiplier or heal priority changes.

// Class ids as in SharedDefines.h (Classes): the two healers whose class context has no "offdps" creator.
constexpr std::uint32_t ClassPaladin = 2;
constexpr std::uint32_t ClassDruid = 11;

// The healer the part is for: a paladin holding "holy" or a druid holding "restoration" in its combat engine.
inline bool HealerWithoutOffdpsCreator(std::uint32_t cls, bool holy, bool restoration)
{
    return (cls == ClassPaladin && holy) || (cls == ClassDruid && restoration);
}

// The gate. The old rule is the first line, unchanged; the part only adds the second.
inline bool HealerMayAttack(bool hasHealStrategy, bool hasOffdps, bool partApplies, bool healerWithoutOffdpsCreator)
{
    if (!hasHealStrategy || hasOffdps)
        return true;
    return partApplies && healerWithoutOffdpsCreator;
}

// Per-minute counts for the "[CombatRule] part=HealerStart" line (part on), increments. "asked" = gate checks the old rule
// refused, by a healer of the part (both cohorts, so mode 2/3 has its own control); "lifted" = of those, opened by the part.
// Index 0 = cohort A, 1 = cohort B; cls 0..MaxClass-1.
constexpr std::uint32_t MaxClass = 12;
struct HealerStartCounters
{
    std::atomic<std::uint64_t> asked[2] {};
    std::atomic<std::uint64_t> lifted[2] {};
    std::atomic<std::uint64_t> liftedCls[MaxClass] {};
};

inline HealerStartCounters& HealerStartGlobal()
{
    static HealerStartCounters counters;
    return counters;
}
}
