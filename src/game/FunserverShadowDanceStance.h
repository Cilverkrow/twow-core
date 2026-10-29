#ifndef TW_FUNSERVER_SHADOW_DANCE_STANCE_H
#define TW_FUNSERVER_SHADOW_DANCE_STANCE_H

#include <cstdint>

// Issue twow-repo#654 (WS10-ROGUE-TANK-STANCE-01, parent #367; owner 29.10.2026 "Als Tank-Haltung", "Alles nur in Haltung",
// name "Shadow Dance", "Im Shadow Dance kann der Schurke nicht unsichtbar werden." / "Nur Stealth und Vanish"): Shadow Dance
// becomes a rogue tank stance. The stance 61230 "Shadow Dance" (castable, no rank, cancelable) carries damage done -40 % and
// damage taken -25 % (all schools); the passive ranks 61143-61145 stay the carriers of the rank's flat threat, but for a rogue the
// switch covers they give nothing without the stance: no dodge / parry buff, no threat. In the stance every dodge / parry buff also
// casts its energy helper (-10 % energy regeneration for 3 s, at most two at once, multiplied: 0.81). The rogue's own Stealth and
// Vanish cannot be cast in the stance, and casting the stance ends an active Stealth. Shadowmeld and invisibility potions stay.
// Funserver.Rogue.ShadowDanceStance: 0 = as before (default, rollback), 1 = every rogue, 2 = bots only (until the client patch).
// Pure rules only, so they can be tested without a server.

enum FunserverShadowDanceSpell : uint32_t
{
    SHADOW_DANCE_RANK_1                    = 61143,  // passive proc carriers R1-R3 (L20 / 40 / 60), flat threat 50 / 90 / 130
    SHADOW_DANCE_RANK_3                    = 61145,
    SHADOW_DANCE_DODGE_BUFF                = 61146,  // +5 % dodge for 3 s, after a parry
    SHADOW_DANCE_PARRY_BUFF                = 61147,  // +5 % parry for 3 s, after a dodge
    SHADOW_DANCE_STANCE                    = 61230,  // the stance (migration in the main train, Ae15)
    SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY = 61231,  // -10 % energy regeneration for 3 s, next to the dodge buff
    SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE = 61232,  // -10 % energy regeneration for 3 s, next to the parry buff
};

enum FunserverShadowDanceStanceMode : uint32_t
{
    SHADOW_DANCE_STANCE_OFF  = 0,
    SHADOW_DANCE_STANCE_ALL  = 1,
    SHADOW_DANCE_STANCE_BOTS = 2,
};

// The switch covers this rogue: 1 = everyone, 2 = AI-controlled players (bots) only.
inline bool ShadowDanceStanceCovers(uint32_t mode, bool aiControlled)
{
    return mode == SHADOW_DANCE_STANCE_ALL || (mode == SHADOW_DANCE_STANCE_BOTS && aiControlled);
}

inline bool IsShadowDanceRank(uint32_t spellId)
{
    return spellId >= SHADOW_DANCE_RANK_1 && spellId <= SHADOW_DANCE_RANK_3;
}

// What a dodge / parry proc of a rank does: the old passive bonus (switch does not cover the rogue), the stance bonus (covered and
// the stance is up: buff + energy helper + threat), or nothing (covered, no stance).
enum class ShadowDanceProc
{
    Passive,
    Stance,
    None,
};

inline ShadowDanceProc ShadowDanceProcFor(bool covered, bool stanceUp)
{
    if (!covered)
        return ShadowDanceProc::Passive;
    return stanceUp ? ShadowDanceProc::Stance : ShadowDanceProc::None;
}

// The energy helper that goes with a buff (0 = none).
inline uint32_t ShadowDanceEnergyHelperFor(uint32_t buffSpellId)
{
    if (buffSpellId == SHADOW_DANCE_DODGE_BUFF)
        return SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY;
    if (buffSpellId == SHADOW_DANCE_PARRY_BUFF)
        return SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE;
    return 0;
}

// Owner 29.10.2026 "Nur Stealth und Vanish": the rogue's Stealth R1-R4 and Vanish R1-R2 (plus the stealth auras Vanish triggers).
// By ID, not by family flags: Flashbang 45607 shares Vanish's family flag 0x800. Shadowmeld and invisibility potions stay allowed.
// The same list is what casting the stance ends.
constexpr uint32_t SHADOW_DANCE_STANCE_BLOCKED_SPELLS[] = {
    1784, 1785, 1786, 1787,   // Stealth R1-R4
    1856, 1857,               // Vanish R1-R2
    11327, 11329,             // Vanish R1-R2, the stealth aura they trigger
};

inline bool ShadowDanceStanceBlocks(uint32_t spellId)
{
    for (uint32_t const blocked : SHADOW_DANCE_STANCE_BLOCKED_SPELLS)
        if (blocked == spellId)
            return true;
    return false;
}

// The rogue learns the stance (login / learning a rank) only with mode 1; mode 2 leaves it to the bot code (the AI is attached
// after the login hook), mode 0 unlearns it (rollback).
enum class ShadowDanceLearn
{
    Keep,
    Learn,
    Unlearn,
};

inline ShadowDanceLearn ShadowDanceLearnFor(uint32_t mode, bool knowsRank, bool knowsStance)
{
    if (mode == SHADOW_DANCE_STANCE_OFF)
        return knowsStance ? ShadowDanceLearn::Unlearn : ShadowDanceLearn::Keep;
    if (mode == SHADOW_DANCE_STANCE_ALL && knowsRank && !knowsStance)
        return ShadowDanceLearn::Learn;
    return ShadowDanceLearn::Keep;
}

#endif
