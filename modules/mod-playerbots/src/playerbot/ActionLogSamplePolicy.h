#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "playerbot/ShortcutPolicy.h"

namespace ai::action_log
{
// twow-repo#601 / #614 (AiPlayerbot.ActionLog.Guids, owner 18.10.2026): AiPlayerbot.EnableActionLog for a fixed sample of bots
// per behaviour arm. Pure rules, no game types, no locks, no config reads.

// "123, 456,789" -> sorted {123, 456, 789}; the parser of Shortcut.TestWatcherGuids (plain decimal guids > 0, duplicates
// dropped, at most ai::shortcut::MaxWatchers kept).
inline std::vector<std::uint32_t> ParseSample(std::string const& text)
{
    std::vector<std::uint32_t> guids = ai::shortcut::ParseGuidList(text);
    std::sort(guids.begin(), guids.end());
    return guids;
}

// Empty sample = every bot (the old behaviour of EnableActionLog); otherwise only the listed guids.
inline bool Sampled(std::vector<std::uint32_t> const& sortedSample, std::uint32_t guid)
{
    return sortedSample.empty() || std::binary_search(sortedSample.begin(), sortedSample.end(), guid);
}
}
