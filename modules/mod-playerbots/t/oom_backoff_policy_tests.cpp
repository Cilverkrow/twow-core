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

    std::cout << "oom_backoff_policy tests passed\n";
    return 0;
}
