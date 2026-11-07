// L1 (owner 07.11.2026, AiPlayerbot.BotGroups.ResetRaidsRealInviterOnly): the pure lockout rules (LockoutPolicy.h).
// I0: the perm assert never fires for the core's own instance choice. I1: the split guard. L1 (a): when reset raids runs.
#include "LockoutPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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
    using namespace ai::lockout;

    // I0 over every combination of absent, non-perm and perm binds on both sides, with equal and different ids.
    std::vector<Bind> selves;
    std::vector<Bind> groups;
    for (std::uint32_t id : {0u, 7u, 9u})
    {
        for (bool perm : {false, true})
        {
            if (!id && perm)
                continue;   // no bind carries no perm flag
            Bind b;
            b.instanceId = id;
            b.perm = perm;
            selves.push_back(b);
            groups.push_back(b);
        }
    }
    for (Bind const& self : selves)
        for (Bind const& group : groups)
            Require(!PermAssertWouldFire(self, CoreChoice(self, group)), "I0: the core's own choice never fires the perm assert");

    // The model of the core choice itself.
    Require(CoreChoice({7, true}, {9, false}) == 7, "perm self before the group bind");
    Require(CoreChoice({7, false}, {9, false}) == 9, "non-perm self: the group bind wins");
    Require(CoreChoice({7, false}, {}) == 7, "non-perm self without a group bind: the solo bind");
    Require(CoreChoice({}, {9, true}) == 9, "no self bind: the group bind");
    Require(CoreChoice({}, {}) == 0, "no bind at all: a new id");

    // Negative case: a forced other copy with a perm self fires (the HARM path the core rules out).
    Require(PermAssertWouldFire({7, true}, 9), "perm self forced into another copy fires");
    Require(PermAssertWouldFire({7, true}, 0), "perm self forced into a new copy fires");
    Require(!PermAssertWouldFire({7, true}, 7), "perm self in its own copy does not fire");
    Require(!PermAssertWouldFire({7, false}, 9), "a non-perm bind never fires");
    Require(!PermAssertWouldFire({}, 9), "no bind never fires");

    // LockSplit truth table.
    Require(LockSplit({7, true}, 9), "perm on M, target in another copy: split");
    Require(!LockSplit({7, true}, 7), "perm on M, target in the same copy: no split");
    Require(!LockSplit({7, true}, 0), "perm on M, no target copy: no check");
    Require(!LockSplit({7, false}, 9), "non-perm bind: no split (the core follows the group)");
    Require(!LockSplit({}, 9), "no bind: no split");
    Require(!LockSplit({}, 0), "nothing: no split");

    // ResetRaidsOnJoin truth table: switch 0 is always true (old behaviour).
    for (int bits = 0; bits < 8; ++bits)
    {
        bool const wm = (bits & 1) != 0;
        bool const tail = (bits & 2) != 0;
        bool const real = (bits & 4) != 0;
        Require(ResetRaidsOnJoin(false, wm, tail, real), "switch 0: reset raids on every accept");
        Require(ResetRaidsOnJoin(true, wm, tail, real) == (!wm && !tail && real), "switch 1: only a real inviter on the direct path");
    }

    // The switch parse: only "0" is off.
    Require(!SwitchOn("0") && !SwitchOn(" 0 ") && !SwitchOn("0\r"), "\"0\" is off");
    Require(SwitchOn("1") && SwitchOn("") && SwitchOn("on") && SwitchOn("2") && SwitchOn("off") && SwitchOn("00"), "anything else is on");

    // The minute line: field order fixed, printed only with a non-zero field.
    Counts c;
    Require(!c.Any(), "an empty minute prints nothing");
    c.resetReal = 1;
    c.resetBot = 2;
    c.keepBot = 3;
    c.splitDecline = 4;
    c.splitEntry = 5;
    Require(c.Any(), "a counted minute prints");
    Require(MinuteLine(true, c) == "[Lockout] minute mode=1 reset_real=1 reset_bot=2 keep_bot=3 split_decline=4 split_entry=5",
        "minute line format");
    Require(MinuteLine(false, Counts()) == "[Lockout] minute mode=0 reset_real=0 reset_bot=0 keep_bot=0 split_decline=0 split_entry=0",
        "minute line at switch 0");

    std::cout << "lockout_policy_tests passed\n";
    return 0;
}
