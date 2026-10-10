#include "PartyScanPolicy.h"

#include <cstdlib>
#include <iostream>

// twow-repo#541 (audit A15): the out-of-group scan is skipped only for a solo bot asking with a
// group-only predicate, and in exactly that case no candidate can share the bot's (null) group.

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

struct FakeGroup
{
    int id;
};
}

int main()
{
    using ai::party_scan::SharesGroup;
    using ai::party_scan::SkipUngroupedScan;

    FakeGroup g1{1};
    FakeGroup g2{2};
    void const* members[] = { nullptr, &g1, &g2 };

    // 1. SharesGroup models Player::IsInGroup(Player const*): one shared non-null group.
    Require(SharesGroup(&g1, &g1), "same group is shared");
    Require(!SharesGroup(&g1, &g2), "different groups are not shared");
    Require(!SharesGroup(nullptr, nullptr), "two players without group are not grouped");
    for (void const* member : members)
        Require(!SharesGroup(member, nullptr), "nobody shares the group of a bot without group");

    // 2. Full truth table: only (on, group-only predicate, no bot group) skips.
    for (int mask = 0; mask < 8; ++mask)
    {
        bool const switchOn = (mask & 1) != 0;
        bool const onlyGroup = (mask & 2) != 0;
        bool const botHasGroup = (mask & 4) != 0;
        bool const expected = switchOn && onlyGroup && !botHasGroup;
        Require(SkipUngroupedScan(switchOn, onlyGroup, botHasGroup) == expected, "truth table");
    }
    Require(!SkipUngroupedScan(false, true, false), "off never skips");
    Require(!SkipUngroupedScan(false, false, false), "off never skips (generic predicate)");
    Require(!SkipUngroupedScan(true, false, false), "a generic predicate (heal/buff/dispel) never skips");
    Require(!SkipUngroupedScan(true, true, true), "a grouped bot keeps the full scan");
    Require(SkipUngroupedScan(true, true, false), "a solo bot with a group-only predicate skips");

    // 3. Consistency: wherever the scan is skipped the bot has no group, so no member can pass
    // the predicate's IsInGroup gate and the skipped scan could only have returned nothing.
    for (int mask = 0; mask < 8; ++mask)
    {
        bool const switchOn = (mask & 1) != 0;
        bool const onlyGroup = (mask & 2) != 0;
        bool const botHasGroup = (mask & 4) != 0;
        if (!SkipUngroupedScan(switchOn, onlyGroup, botHasGroup))
            continue;

        Require(!botHasGroup, "a skip implies a bot without group");
        void const* botGroup = botHasGroup ? static_cast<void const*>(&g1) : nullptr;
        for (void const* member : members)
            Require(!SharesGroup(member, botGroup), "a skip never drops a candidate");
    }

    std::cout << "party_scan_policy tests passed\n";
    return 0;
}
