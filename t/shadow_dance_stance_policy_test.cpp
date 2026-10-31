// twow-repo#654 rules for Shadow Dance as a rogue tank stance (Funserver.Rogue.ShadowDanceStance, default 0).
#include "../src/game/FunserverShadowDanceStance.h"

#include <initializer_list>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(bool ok, char const* label)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << label << "\n";
            ++failures;
        }
    }
}

int main()
{
    // The switch: 0 off, 1 every rogue, 2 bots (AI-controlled players) only.
    Check(!ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_OFF, false) && !ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_OFF, true), "mode 0 covers nobody");
    Check(ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_ALL, false) && ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_ALL, true), "mode 1 covers every rogue");
    Check(!ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_BOTS, false) && ShadowDanceStanceCovers(SHADOW_DANCE_STANCE_BOTS, true), "mode 2 covers bots only");
    Check(!ShadowDanceStanceCovers(3, true), "an unknown mode covers nobody");

    // The ranks are the passive carriers 61143-61145.
    Check(IsShadowDanceRank(61143) && IsShadowDanceRank(61144) && IsShadowDanceRank(61145), "ranks 61143-61145");
    Check(!IsShadowDanceRank(61146) && !IsShadowDanceRank(61230) && !IsShadowDanceRank(61142), "buffs, the stance and Spit are no ranks");

    // Owner "Alles nur in Haltung": a covered rogue gets the bonus only in the stance; an uncovered one keeps the old passive.
    Check(ShadowDanceProcFor(false, false) == ShadowDanceProc::Passive, "not covered: the old passive bonus");
    Check(ShadowDanceProcFor(false, true) == ShadowDanceProc::Passive, "not covered, stance up anyway: still the old passive (no helper)");
    Check(ShadowDanceProcFor(true, true) == ShadowDanceProc::Stance, "covered and in the stance: buff + energy helper + threat");
    Check(ShadowDanceProcFor(true, false) == ShadowDanceProc::None, "covered without the stance: nothing (no leftover passive bonus)");

    // Each buff has its own energy helper, so at most two are up at once (0.9 x 0.9 = 0.81).
    Check(ShadowDanceEnergyHelperFor(SHADOW_DANCE_DODGE_BUFF) == SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY, "dodge buff (after a parry) -> 61231");
    Check(ShadowDanceEnergyHelperFor(SHADOW_DANCE_PARRY_BUFF) == SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE, "parry buff (after a dodge) -> 61232");
    Check(ShadowDanceEnergyHelperFor(61230) == 0, "no helper for anything else");
    Check(SHADOW_DANCE_STANCE_ENERGY_AFTER_PARRY != SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE, "two separate helpers");

    // Owner "Nur Stealth und Vanish": Stealth R1-R4 and Vanish R1-R2 (+ their stealth auras) are blocked, nothing else.
    for (uint32_t id : { 1784u, 1785u, 1786u, 1787u, 1856u, 1857u, 11327u, 11329u })
        Check(ShadowDanceStanceBlocks(id), "Stealth / Vanish ranks are blocked in the stance");
    Check(!ShadowDanceStanceBlocks(20580), "Shadowmeld stays allowed");
    Check(!ShadowDanceStanceBlocks(3680) && !ShadowDanceStanceBlocks(11392), "invisibility potions stay allowed");
    Check(!ShadowDanceStanceBlocks(45607), "Flashbang (shares Vanish's family flag) stays allowed");
    Check(!ShadowDanceStanceBlocks(1789) && !ShadowDanceStanceBlocks(1858), "the trainer teaching spells are not casts of Stealth / Vanish");

    // Who knows the stance: mode 1 learns it with a rank, mode 0 unlearns it, mode 2 leaves it to the bot code.
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_ALL, true, false) == ShadowDanceLearn::Learn, "mode 1: a rank teaches the stance");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_ALL, false, false) == ShadowDanceLearn::Keep, "mode 1: no rank, no stance");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_ALL, true, true) == ShadowDanceLearn::Keep, "mode 1: already known");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_OFF, true, true) == ShadowDanceLearn::Unlearn, "mode 0: the stance is unlearned (rollback)");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_OFF, true, false) == ShadowDanceLearn::Keep, "mode 0: nothing to unlearn");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_BOTS, true, false) == ShadowDanceLearn::Keep, "mode 2: the core teaches nothing (bot code)");
    Check(ShadowDanceLearnFor(SHADOW_DANCE_STANCE_BOTS, true, true) == ShadowDanceLearn::Keep, "mode 2: a known stance stays");

    // The stance and its helpers sit in the custom range below the 16-bit client limit (#455).
    Check(SHADOW_DANCE_STANCE >= 61002 && SHADOW_DANCE_STANCE_ENERGY_AFTER_DODGE <= 65535, "custom spell range");

    if (failures)
        return 1;
    std::cout << "shadow_dance_stance_policy_test: all checks passed\n";
    return 0;
}
