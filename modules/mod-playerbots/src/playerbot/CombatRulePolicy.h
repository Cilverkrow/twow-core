#pragma once

#include <atomic>
#include <cstdint>
#include <string>

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

// ---------------------------------------------------------------------------------------------------------------------------
// Part PriestOverlay (AiPlayerbot.CombatRule.PriestOverlay; A3-AKTIONS-VERDRAHTUNG v1.1 §0.2 point 4, F1 (a)).
//
// AiPlayerbot.RandomBotCombatStrategies ("+dps,+dps assist,-threat" in the profile) goes onto every free bot's combat engine. Only
// the priest context creates "dps" (the shadow placeholder), and class strategies of one context are siblings, so "+dps" replaces
// "holy" / "discipline": every priest fights as shadow (K8, 10 h: mind flay is the combat default in 100 % of priest combat ticks
// and IMPOSSIBLE in 66 %). With the part on, the overlay of a discipline or holy priest goes on without the "+dps" entry; the
// priest keeps its tree and its "offdps" ladder. "+dps assist" and every other entry stay, in their order.

constexpr std::uint32_t ClassPriest = 5;

// A priest of a healing tree: AiFactory's tab 0 (discipline) or 1 (holy); tab 2 is shadow, where "+dps" changes nothing.
inline bool HealerPriest(std::uint32_t cls, int tab)
{
    return cls == ClassPriest && (tab == 0 || tab == 1);
}

// The comma list without the entries that equal `token` exactly. Engine::ChangeStrategy splits on ',' and does not trim, so the
// match does not trim either: "+dps assist" and " +dps" are other entries. Empty entries are kept as they were.
inline std::string WithoutEntry(std::string const& list, std::string const& token)
{
    std::string out;
    bool first = true;
    std::string::size_type start = 0;
    while (true)
    {
        std::string::size_type const comma = list.find(',', start);
        std::string const entry = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (entry != token)
        {
            if (!first)
                out += ',';
            out += entry;
            first = false;
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

// Per-minute counts for the "[CombatRule] part=PriestOverlay" line (part on), increments: combat engines of healing-tree priests
// set up with the overlay (both cohorts), and of those, the ones that got it without "+dps". Index 0 = cohort A, 1 = cohort B.
struct PriestOverlayCounters
{
    std::atomic<std::uint64_t> engines[2] {};
    std::atomic<std::uint64_t> stripped[2] {};
};

inline PriestOverlayCounters& PriestOverlayGlobal()
{
    static PriestOverlayCounters counters;
    return counters;
}

// ---------------------------------------------------------------------------------------------------------------------------
// Part RestBeforePull (AiPlayerbot.CombatRule.RestBeforePull; A3-AKTIONS-VERDRAHTUNG v1.1 §1.4, F7).
//
// Without RestGate a bot eats only with HP in [20, 50) (the "low health" band) and drinks below 65 % mana, and below 20 % HP out of
// combat nothing runs at all; it pulls the next mob at any HP. The rule owns the pull bit instead: a solo free bot out of combat and
// not attacked does not start a pull (attack anything) below StartHp, or below StartMana with a mana pool, and its own rest node
// (strategy "combat rule": food / drink at 7.0, above choose travel target 6.98 and attack anything 5) eats or drinks first - with
// no lower bound, so it closes the < 20 % gap. Unlike RestGate it locks no movement ("can move around" stays) and keeps no episode
// state: one food / drink cast lasts until the bot is full (eat / drink duration). A target that already fights the bot is always
// allowed (self-defence). A bot RestGate applies to is left to RestGate.

enum class RestNeed : std::uint8_t
{
    None = 0,
    Health = 1,
    Mana = 2,
};

struct RestReading
{
    bool soloFree = false;       // no group, no real player (master), free bot, not in a battleground, alive
    bool inCombat = false;
    bool attacked = false;       // has attackers
    std::uint32_t hp = 100;      // percent
    bool manaPool = false;       // has a mana pool now (not a druid in cat / bear form)
    std::uint32_t mana = 100;    // percent
};

// What the bot has to rest for before its next pull; health first.
inline RestNeed RestBeforePull(RestReading const& r, std::uint32_t startHp, std::uint32_t startMana)
{
    if (!r.soloFree || r.inCombat || r.attacked)
        return RestNeed::None;
    if (r.hp < startHp)
        return RestNeed::Health;
    if (r.manaPool && r.mana < startMana)
        return RestNeed::Mana;
    return RestNeed::None;
}

// The pull bit: refused while a rest is needed, unless the target already fights the bot.
inline bool MayPull(RestNeed need, bool targetFightsBot)
{
    return need == RestNeed::None || targetFightsBot;
}

// Clamps for the .dist keys: StartHp 1..99 (default 60), StartMana 0..99 (default 40; 0 = no mana rest).
inline std::uint32_t ClampStartHp(int value)
{
    return value < 1 ? 1u : (value > 99 ? 99u : std::uint32_t(value));
}

inline std::uint32_t ClampStartMana(int value)
{
    return value < 0 ? 0u : (value > 99 ? 99u : std::uint32_t(value));
}

// Per-minute counts for the "[CombatRule] part=RestBeforePull" line (part on), increments, index 0 = cohort A, 1 = cohort B:
// pulls started by solo free bots (both cohorts) and of those below StartHp / StartMana (the outcome, measured in the control
// cohort too), pull checks the part refused, and rest nodes fired (health / mana).
struct RestBeforePullCounters
{
    std::atomic<std::uint64_t> pulls[2] {};
    std::atomic<std::uint64_t> pullsLowHp[2] {};
    std::atomic<std::uint64_t> pullsLowMana[2] {};
    std::atomic<std::uint64_t> refused[2] {};
    std::atomic<std::uint64_t> restHealth[2] {};
    std::atomic<std::uint64_t> restMana[2] {};
};

inline RestBeforePullCounters& RestBeforePullGlobal()
{
    static RestBeforePullCounters counters;
    return counters;
}
}
