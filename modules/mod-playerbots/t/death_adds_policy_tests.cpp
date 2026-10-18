// twow-repo#602 ROT-01 F6: the adds column of deaths.csv - snapshot refresh, freshness at OnDeath (also across a getMSTime wrap), the bound,
// and the merge of live victims with the snapshot (DeathAddsPolicy.h). Hand-rolled Require(), std::exit(1) on failure, no gtest.
#ifdef _WIN32
#include <windows.h>
#endif
#include "DeathAddsPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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
    using namespace ai::death_adds;

    // 1) Refresh: none yet, attacker count changed, or a second old.
    {
        Snapshot s;
        Require(Due(s, 5000, 0), "no snapshot yet: due");
        s.Begin(5000, 2);
        Require(!Due(s, 5999, 2), "same count within a second: not due");
        Require(Due(s, 6000, 2), "a second old: due");
        Require(Due(s, 5100, 3), "an add joined: due at once");
        Require(Due(s, 5100, 1), "an attacker left: due at once");
        s.Begin(0xFFFFFF00u, 2);
        Require(!Due(s, 0x00000100u, 2), "timer wrap: 512 ms old, not due");
        Require(Due(s, 0x00000400u, 2), "timer wrap: 1280 ms old, due");
    }

    // 2) Fresh at OnDeath: at most 5 s old, also across the wrap; a cleared snapshot is never fresh.
    {
        Snapshot s;
        Require(!Fresh(s, 1000), "never taken: not fresh");
        s.Begin(10000, 1);
        Require(Fresh(s, 15000), "5 s old: fresh");
        Require(!Fresh(s, 15001), "older than 5 s: another fight");
        s.Begin(0xFFFFF000u, 1);
        Require(Fresh(s, 0x00000100u), "timer wrap: 4.3 s old, fresh");
        s.Clear();
        Require(!Fresh(s, 0xFFFFF001u), "cleared (out of combat): not fresh");
        Require(s.count == 0 && s.attackers == 0, "clear empties");
    }

    // 3) Bounded: at most MaxUnits guids, Begin starts over.
    {
        Snapshot s;
        s.Begin(1, 20);
        for (std::uint64_t g = 1; g <= MaxUnits; ++g)
            Require(s.Add(g), "within the bound");
        Require(!s.Add(999), "17th guid refused");
        Require(s.count == MaxUnits, "count at the bound");
        s.Begin(2, 1);
        Require(s.count == 0 && s.Add(7) && s.count == 1, "begin starts over");
    }

    // 4) Merge: live victims first, then the snapshot; once each, never the current target, nothing from a stale snapshot.
    {
        Snapshot s;
        s.Begin(1, 3);
        s.Add(11);   // the killer
        s.Add(22);
        s.Add(33);
        std::vector<std::uint64_t> const none;
        Require(MergeAdds(none, s, true, 11) == std::vector<std::uint64_t>({22, 33}), "the death cleared every victim: adds from the snapshot");
        Require(MergeAdds(none, s, false, 11).empty(), "stale snapshot: no adds (the old behaviour)");
        Require(MergeAdds({33, 44}, s, true, 11) == std::vector<std::uint64_t>({33, 44, 22}), "live first, each guid once");
        Require(MergeAdds({11}, s, false, 11).empty(), "the current target is never an add");
        Require(MergeAdds(none, s, true, 0) == std::vector<std::uint64_t>({11, 22, 33}), "no current target: every attacker is an add");
        Require(MergeAdds({0}, Snapshot(), true, 0).empty(), "an empty guid is skipped");
    }

    std::cout << "death_adds_policy_tests passed\n";
    return 0;
}
