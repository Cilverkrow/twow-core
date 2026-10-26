#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace ai::destroy_item
{
// OB-10 26.10.2026 (use after free): SmartDestroyItemAction, grey-only branch, collected the found grey stacks as Item*
// and destroyed each one BY ITEM ID - every stack of that id. With two stacks of one grey item the second iteration read
// the already destroyed Item* (item->GetProto()). ThrownUsageFix (P1-s) makes smart destroy run more often. The fix keeps
// the first stack of each id only, so every pointer the loop reads belongs to an id not yet destroyed.
// Pure rule, no game types.

// The positions of the first occurrence of each id, in order.
inline std::vector<std::size_t> FirstOfEachId(std::vector<std::uint32_t> const& ids)
{
    std::vector<std::size_t> first;
    std::unordered_set<std::uint32_t> seen;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (seen.insert(ids[i]).second)
            first.push_back(i);
    return first;
}
}
