// twow-repo#602 ROT-01 3c-B (priest Farathil loop, OB-00 24.10.2026): AiPlayerbot.Rotation.OomBackoff - an out-of-mana priest,
// mage or warlock without a wand fights in melee, and an idle out-of-mana combat pass waits DelayMs.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "OomBackoffPolicy.h"

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
    using namespace ai::oom_backoff;

    // Mode: 0 off, 1 all, 2 cohort B (odd low guid); unknown values act like 0.
    Require(!Applies(0, 9) && Applies(1, 8) && Applies(1, 9), "modes 0 and 1");
    Require(!Applies(2, 8) && Applies(2, 9) && !Applies(3, 9), "mode 2 = cohort B, unknown = off");

    // Classes with a wand as their only ranged fallback (1.12 ids): priest 5, mage 8, warlock 9.
    Require(WandClass(5) && WandClass(8) && WandClass(9), "priest, mage, warlock");
    Require(!WandClass(1) && !WandClass(3) && !WandClass(7) && !WandClass(11), "not warrior, hunter, shaman, druid");

    // Out of mana = below the "no mana" mark (5 %), and only for a bot with mana.
    Require(OutOfMana(true, 0) && OutOfMana(true, 4), "below 5 %");
    Require(!OutOfMana(true, 5) && !OutOfMana(true, 40), "at or above 5 %");
    Require(!OutOfMana(false, 0), "no mana bar (bear/cat form)");

    // Melee fallback: switch, class, out of mana, no wand.
    Require(MeleeFallback(true, 5, true, 2, false), "Farathil: priest, 2 % mana, no wand");
    Require(!MeleeFallback(false, 5, true, 2, false), "switch off");
    Require(!MeleeFallback(true, 5, true, 2, true), "with a wand the bot shoots");
    Require(!MeleeFallback(true, 5, true, 10, false), "mana above the mark");
    Require(!MeleeFallback(true, 7, true, 2, false), "a shaman is no wand class");

    // I-1 Rotation.PriestOomMelee: priests only; 0 off, 1 all, 2 odd guid, 3 even guid (crossover), unknown off.
    Require(PriestMeleeApplies(1, 5, 8) && PriestMeleeApplies(1, 5, 9), "mode 1: every priest");
    Require(!PriestMeleeApplies(2, 5, 8) && PriestMeleeApplies(2, 5, 9), "mode 2: odd-guid priests only");
    Require(PriestMeleeApplies(3, 5, 8) && !PriestMeleeApplies(3, 5, 9), "mode 3: even-guid priests only (crossover)");
    Require(!PriestMeleeApplies(0, 5, 9) && !PriestMeleeApplies(4, 5, 9) && !PriestMeleeApplies(4, 5, 8), "mode 0 and unknown modes off");
    Require(!PriestMeleeApplies(1, 8, 9) && !PriestMeleeApplies(1, 9, 9) && !PriestMeleeApplies(1, 1, 9), "not mage, warlock, warrior");
    // MeleeArmed (oomMode, priestMode, cls, guid): either switch arms the fallback; OomBackoff alone keeps its three classes.
    Require(!MeleeArmed(0, 0, 5, 9), "both off");
    Require(MeleeArmed(0, 1, 5, 8) && !MeleeArmed(0, 1, 8, 9) && !MeleeArmed(0, 1, 9, 9), "PriestOomMelee alone: priests only");
    Require(MeleeArmed(1, 0, 5, 8) && MeleeArmed(1, 0, 8, 8) && MeleeArmed(1, 0, 9, 8), "OomBackoff alone unchanged");
    Require(MeleeArmed(0, 2, 5, 9) && !MeleeArmed(0, 2, 5, 8), "PriestOomMelee mode 2 by guid parity");
    Require(MeleeArmed(2, 2, 5, 9) && !MeleeArmed(2, 2, 5, 8) && MeleeArmed(2, 1, 5, 8), "both on: either applies");
    // ViaPriestKey: the [PriestOomMelee] counters take a bot only when OomBackoff does not apply to it.
    Require(ViaPriestKey(0, 1, 5, 8) && ViaPriestKey(2, 1, 5, 8), "armed by PriestOomMelee alone");
    Require(!ViaPriestKey(1, 1, 5, 8) && !ViaPriestKey(2, 1, 5, 9), "OomBackoff applies: its counters");
    Require(!ViaPriestKey(0, 1, 8, 8) && !ViaPriestKey(0, 0, 5, 8), "a mage, or the key off");

    // Backoff: all conditions, LowHealth 40.
    std::uint32_t const low = 40;
    Require(MayBackoff(true, true, true, false, false, true, 2, 80, low), "idle out-of-mana pass");
    Require(!MayBackoff(false, true, true, false, false, true, 2, 80, low), "switch off");
    Require(!MayBackoff(true, false, true, false, false, true, 2, 80, low), "the pass executed an action");
    Require(!MayBackoff(true, true, false, false, false, true, 2, 80, low), "someone else set the delay");
    Require(!MayBackoff(true, true, true, true, false, true, 2, 80, low), "minimal pass");
    Require(!MayBackoff(true, true, true, false, true, true, 2, 80, low), "casting");
    Require(!MayBackoff(true, true, true, false, false, true, 5, 80, low), "mana at the mark");
    Require(!MayBackoff(true, true, true, false, false, false, 0, 80, low), "no mana bar");
    Require(!MayBackoff(true, true, true, false, false, true, 2, 39, low), "below LowHealth");
    Require(MayBackoff(true, true, true, false, false, true, 2, 40, low), "at LowHealth");

    // Wake: mana back at the mark, or health below LowHealth; nothing pending = no wake.
    Require(!ShouldWake(false, true, 50, 10, low), "nothing pending");
    Require(!ShouldWake(true, true, 3, 80, low), "still out of mana, healthy");
    Require(ShouldWake(true, true, 5, 80, low), "mana back");
    Require(ShouldWake(true, true, 3, 39, low), "health dropped");

    // The wake cuts only a delay of at most the backoff.
    Require(WakeResets(1000, 1000) && WakeResets(1, 1000), "own wait");
    Require(!WakeResets(0, 1000) && !WakeResets(5000, 1000), "no wait / a longer delay someone else set");

    Require(fallback.load() == 0 && stayed.load() == 0 && backoffs.load() == 0 && woken.load() == 0, "counters start at 0");
    Require(priestFallback[0].load() == 0 && priestFallback[1].load() == 0 && priestStayed[0].load() == 0 &&
        priestStayed[1].load() == 0, "I-1 counters start at 0");
    // Counter index per cohort (OB-10 A7 control null): 0 = A even low guid, 1 = B odd.
    Require(CohortIndex(8) == 0 && CohortIndex(9) == 1 && CohortIndex(0) == 0 && CohortIndex(4294967295u) == 1, "cohort index");

    std::cout << "oom_backoff_policy tests passed\n";
    return 0;
}
