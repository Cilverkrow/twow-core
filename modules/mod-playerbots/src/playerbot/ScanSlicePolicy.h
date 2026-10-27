#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// twow-repo#628 (AiPlayerbot.BotGroups.AdHoc.ScanSlices, AiPlayerbot.QuestGroups.ScanSlices; default 0): a world-thread manager
// pass over all bots, spread over several manager updates so one world tick carries only a share of it.
namespace ai::scan_slice
{
// The end of this update's share: from next, ceil(size / slices) bots, at least one; 0 or 1 slices = all bots at once.
inline std::size_t SliceEnd(std::size_t size, std::size_t next, std::uint32_t slices)
{
    std::uint32_t const n = std::max<std::uint32_t>(1, slices);
    std::size_t const per = std::max<std::size_t>(1, (size + n - 1) / n);
    return std::min(size, next + per);
}

// s7r 26.10.2026 (AiPlayerbot.QuestGroups.ScanBudgetUs, AiPlayerbot.BotGroups.AdHoc.ScanBudgetUs; default 0): a slice also
// ends once it took budgetUs - single bots with expensive quest logs made one slice of ~54 bots cost 3.8-9.9 ms. At least
// one bot per update, so a scan always ends; 0 = by count only (as before).
inline bool BudgetSpent(std::uint32_t budgetUs, std::uint32_t elapsedUs, std::size_t doneThisUpdate)
{
    return budgetUs > 0 && doneThisUpdate > 0 && elapsedUs >= budgetUs;
}
}
