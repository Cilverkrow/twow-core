#pragma once

#include <cstdint>

namespace ai::catchup_level
{
// twow-repo#340 catch-up share (CatchupQuestAction::Admit): which bot levels may take a quest
// the master shares.
//
// Until 26.11.2026: only bots at or below the master, at most MaxBelow levels below.
// GM check 3 (26.11.2026): Tamsin (L21) refused 40471 (quest level 14) from Uthery (L20).
// Owner decision 26.11.2026 ("okay go 4 it", relayed by OB-00): bots may take green quests,
// so a bot above the master takes a shared quest as long as it is not grey for the bot.
// The window below the master stays.
constexpr std::uint32_t MaxBelow = 8;

// Grey as everywhere in the module (QuestAcceptPolicy::SkipForRosterBot): questLevel <= grey level.
// A quest level <= 0 scales with the player and is never grey.
inline bool GreyFor(std::int32_t questLevel, std::uint32_t botGrayLevel)
{
    return questLevel > 0 && std::uint32_t(questLevel) <= botGrayLevel;
}

inline bool Admits(std::uint32_t masterLevel, std::uint32_t botLevel, std::int32_t questLevel,
    std::uint32_t botGrayLevel)
{
    if (botLevel <= masterLevel)
        return masterLevel - botLevel <= MaxBelow;
    return !GreyFor(questLevel, botGrayLevel);
}
}
