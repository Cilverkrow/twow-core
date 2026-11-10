#include "RosterSnapshotPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#698: the role/group snapshot a bot publishes from its own thread for the roster list.

namespace
{
using namespace ai::roster_snapshot;

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
    // a word that was never written reads as "unknown": DPS, no group (the offline member answer)
    Unpacked const never = Unpack(0);
    Require(!never.valid && never.role == RoleDps && never.group == GroupFree, "zero word = not valid, DPS, free");

    // every role/group pair survives the round trip and is valid
    for (uint32_t role : { uint32_t(RoleDps), uint32_t(RoleTank), uint32_t(RoleHeal) })
    {
        for (uint32_t group : { uint32_t(GroupFree), uint32_t(GroupParty), uint32_t(GroupRaid) })
        {
            uint32_t const word = Pack(role, group);
            Require(word != 0, "a packed word is never 0");
            Unpacked const u = Unpack(word);
            Require(u.valid && u.role == role && u.group == group, "round trip");
        }
    }

    // even DPS + free packs to a non-zero word, so "never written" and "written as DPS/free" differ
    Require(Pack(RoleDps, GroupFree) == ValidBit, "DPS/free is the valid bit alone");
    Require(Unpack(Pack(RoleDps, GroupFree)).valid, "written DPS/free is valid");

    // out-of-range inputs cannot set other bits
    Require((Pack(0xFFu, 0xFFu) & ~(ValidBit | RoleMask | (GroupMask << GroupShift))) == 0, "no stray bits");

    // refresh timing: first write always, then not before the interval, wrap-safe
    Require(RefreshDue(0, 0, false), "first write is due");
    Require(!RefreshDue(1000, 1000 + RefreshIntervalMs - 1, true), "not due before the interval");
    Require(RefreshDue(1000, 1000 + RefreshIntervalMs, true), "due at the interval");
    Require(RefreshDue(0xFFFFFF00u, 0xFFFFFF00u + RefreshIntervalMs, true), "due across the 32-bit wrap");
    Require(!RefreshDue(0xFFFFFF00u, 0xFFFFFF10u, true), "not due right after the wrap");

    std::cout << "roster snapshot policy tests passed\n";
    return 0;
}
