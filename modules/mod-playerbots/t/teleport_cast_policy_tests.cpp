// twow-repo#333 finding 4 (TeleportCastPolicy.h, OB-00 27.10.2026): a travel step keeps the cast wait of the hearthstone or
// teleport it started, and waits for one the bot already prepares.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "TeleportCastPolicy.h"

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
    using namespace ai::teleport_cast;

    // 1) The spell ids of UseHearthStoneAction.
    Require(kHearthstone == 8690 && kScourgestone == 54403, "Hearthstone 8690, Scourgestone 54403");

    // 2) The same teleport: the step spell itself; the hearthstone node also takes the Scourgestone.
    Require(SameTeleport(8690, 8690), "hearthstone step, hearthstone prepared");
    Require(SameTeleport(8690, 54403), "hearthstone step, Scourgestone prepared");
    Require(SameTeleport(3567, 3567), "teleport spell step, the same spell prepared");
    Require(!SameTeleport(3567, 54403), "the Scourgestone only stands in for the hearthstone");
    Require(!SameTeleport(54403, 8690), "no reverse stand-in");
    Require(!SameTeleport(8690, 133), "another cast (Fireball) is not the teleport");
    Require(!SameTeleport(0, 8690) && !SameTeleport(8690, 0) && !SameTeleport(0, 0), "0 = no step spell or nothing prepared");

    // 3) The wait for a prepared teleport: the rest plus one react delay.
    Require(WaitMs(9900, 100) == 10000, "rest 9.9 s + 100 ms");
    Require(WaitMs(0, 100) == 100, "a cast at its end waits one react delay");

    // 4) The hold after a start: the wait from the spell itself; nothing PREPARING (instant teleport, a cast that ended or
    //    failed at once) = one react delay, never an inner or stale wait.
    Require(HoldMs(10100, 100) == 10100, "the spell's own wait");
    Require(HoldMs(0, 100) == 100, "nothing to wait for: one react delay");

    // 5) The watch (OB-10 review 28.10.): it ends the hold as soon as the spell is no longer PREPARING.
    {
        HoldWatch w;
        Require(!Watching(w) && Check(w, 0, 0, false, 5000) == WatchStep::Keep, "no watch: nothing to do");

        Arm(w, 0xA0, 8690, 1000, 10100);
        Require(Watching(w) && w.deadline == 11100, "armed until now + hold");
        Require(Check(w, 0xA0, 8690, true, 1050) == WatchStep::Keep, "the same spell still PREPARING: keep");
        // an interrupt in the first second (no SMSG_SPELL_FAILURE needed): the slot is empty
        Require(Check(w, 0, 0, false, 1100) == WatchStep::Release, "interrupted at once: release");
        // fear / stun cancel without an interrupt event, or a failed cast: the spell left PREPARING
        Require(Check(w, 0xA0, 8690, false, 4000) == WatchStep::Release, "no longer PREPARING: release");
        // the cast was replaced by another one (a reaction cast, a new Spell at the same address with another id)
        Require(Check(w, 0xB0, 133, true, 4000) == WatchStep::Release, "another Spell object: release");
        Require(Check(w, 0xA0, 133, true, 4000) == WatchStep::Release, "same address, another id: release");
        // the hold is over: stop watching, never reset a wait that may belong to something else now
        Require(Check(w, 0xB0, 133, true, 11100) == WatchStep::Expire, "after the deadline: expire, no release");
        Require(Check(w, 0xA0, 8690, true, 12000) == WatchStep::Expire, "pushback past the deadline: expire (the guard re-arms)");

        Arm(w, 0xA0, 8690, 0xFFFFFF00u, 0x200);   // the millisecond clock wraps
        Require(Check(w, 0xA0, 8690, true, 0x50) == WatchStep::Keep, "wrap: before the deadline");
        Require(Check(w, 0xA0, 8690, true, 0x100) == WatchStep::Expire, "wrap: at the deadline");

        Arm(w, 0, 8690, 1000, 10100);
        Require(!Watching(w) && w.id == 0 && w.deadline == 0, "no spell: not armed");
        Arm(w, 0xA0, 8690, 1000, 10100);
        Clear(w);
        Require(!Watching(w), "cleared");
    }

    std::cout << "teleport_cast_policy: all checks passed\n";
    return 0;
}
