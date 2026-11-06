// twow-repo#602 ROT-01 W24 pattern B (RetargetEmptyPolicy.h, OB-00 06.11.2026, Rotation.RetargetEmptyTrace default 0, log only): when
// "select new target" fails while "has attackers" is true, one [RetargetEmpty] line per bot at most every 10 s names the first filter of
// "possible attack targets" an attacker fails. Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "RetargetEmptyPolicy.h"

#include <cstdlib>
#include <cstring>
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

bool Named(ai::retarget_empty::Reason reason, char const* name)
{
    return std::strcmp(ai::retarget_empty::ReasonName(reason), name) == 0;
}
}

int main()
{
    using namespace ai::retarget_empty;

    // 1) Modes: 0 off (default), 1 every bot, 2 cohort B (odd low guid).
    Require(!Applies(0, 10) && !Applies(0, 11), "mode 0: no bot");
    Require(Applies(1, 10) && Applies(1, 11), "mode 1: every bot");
    Require(!Applies(2, 10) && Applies(2, 11), "mode 2: cohort B only");

    // 2) The first failing filter, in the value's order (exists, alive, attackable, range, immune, tapped for NPCs, the rest of IsValid).
    Require(FirstFailing(false, false, false, false, true, true, false, false) == Reason::Gone, "not on the map: gone first");
    Require(FirstFailing(true, false, false, false, true, true, false, false) == Reason::Dead, "dead before every other filter");
    Require(FirstFailing(true, true, false, false, true, true, false, false) == Reason::NotValidAttack, "not attackable before range");
    Require(FirstFailing(true, true, true, false, true, true, false, false) == Reason::OutOfRange, "out of range before immune");
    Require(FirstFailing(true, true, true, true, true, true, false, false) == Reason::Immune, "immune before tapped");
    Require(FirstFailing(true, true, true, true, false, true, false, false) == Reason::NotTapped, "an untapped NPC");
    Require(FirstFailing(true, true, true, true, false, false, false, false) == Reason::NoVictimNoTarget,
        "a player is never 'not tapped': the rest of IsValid decides");
    Require(FirstFailing(true, true, true, true, false, true, true, false) == Reason::NoVictimNoTarget, "no victim, no target");
    Require(FirstFailing(true, true, true, true, false, true, true, true) == Reason::Ok, "passes every filter");

    // 3) The names on the line (the evaluation greps them).
    Require(Named(Reason::Ok, "ok") && Named(Reason::Gone, "gone") && Named(Reason::Dead, "dead"), "names: ok, gone, dead");
    Require(Named(Reason::NotValidAttack, "not_valid_attack") && Named(Reason::OutOfRange, "out_of_range"), "names: attackable, range");
    Require(Named(Reason::Immune, "immune") && Named(Reason::NotTapped, "not_tapped"), "names: immune, tapped");
    Require(Named(Reason::NoVictimNoTarget, "no_victim_no_target") && Named(Reason::AssistNone, "assist_none"), "names: rest, assist");

    // 4) At most one line per bot every 10 s; the first one at once.
    Require(LineIntervalSeconds == 10, "10 s per bot (OB-00)");
    Require(Due(0, 1000, LineIntervalSeconds), "no line yet: due");
    Require(!Due(1000, 1000, LineIntervalSeconds) && !Due(1000, 1009, LineIntervalSeconds), "within 10 s: not due");
    Require(Due(1000, 1010, LineIntervalSeconds) && Due(1000, 5000, LineIntervalSeconds), "after 10 s: due");

    std::cout << "retarget_empty_policy_tests passed\n";
    return 0;
}
