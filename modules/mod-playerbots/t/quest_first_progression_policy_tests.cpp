#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
bool UsesPolicy(bool enabled, bool rosterMember)
{
    return enabled && rosterMember;
}

bool MayAutonomouslyAccept(bool enabled, bool rosterMember, uint32_t activeSlots, uint32_t softLimit,
    uint32_t botLevel, uint32_t questLevel, uint32_t rejectDelta)
{
    if (!UsesPolicy(enabled, rosterMember))
        return true;
    return activeSlots < softLimit && botLevel < questLevel + rejectDelta;
}

bool MayRetire(bool enabled, bool rosterMember, uint32_t botLevel, uint32_t questLevel, uint32_t retireDelta,
    bool completed, bool timed, bool classOrProfession, bool chainOrAccess, bool itemOrSource, bool progressed, bool masterPinned)
{
    return UsesPolicy(enabled, rosterMember) && botLevel >= questLevel + retireDelta &&
        !completed && !timed && !classOrProfession && !chainOrAccess && !itemOrSource && !progressed && !masterPinned;
}
}

int main()
{
    // Default-off and non-roster calls are exact legacy pass-throughs.
    assert(!UsesPolicy(false, true));
    assert(!UsesPolicy(true, false));
    assert(MayAutonomouslyAccept(false, true, 20, 16, 60, 1, 4));
    assert(MayAutonomouslyAccept(true, false, 20, 16, 60, 1, 4));

    // Autonomous offer boundary and the unchanged hard limit separation.
    assert(MayAutonomouslyAccept(true, true, 15, 16, 20, 17, 4));
    assert(!MayAutonomouslyAccept(true, true, 16, 16, 20, 17, 4));
    assert(MayAutonomouslyAccept(true, true, 0, 16, 20, 17, 4)); // delta 3
    assert(!MayAutonomouslyAccept(true, true, 0, 16, 20, 16, 4)); // delta 4

    // Retirement boundary plus every protected class. A real master pin is
    // intentionally conservative until a per-quest pin contract exists.
    assert(!MayRetire(true, true, 20, 15, 6, false, false, false, false, false, false, false)); // delta 5
    assert(MayRetire(true, true, 21, 15, 6, false, false, false, false, false, false, false));  // delta 6
    assert(!MayRetire(true, true, 21, 15, 6, true, false, false, false, false, false, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, true, false, false, false, false, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, false, true, false, false, false, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, false, false, true, false, false, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, false, false, false, true, false, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, false, false, false, false, true, false));
    assert(!MayRetire(true, true, 21, 15, 6, false, false, false, false, false, false, true));

    std::cout << "quest_first_policy=PASS default_off=PASS soft_limit=PASS item_safety=PASS\n";
}
