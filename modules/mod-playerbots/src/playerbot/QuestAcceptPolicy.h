#pragma once

#include <cstdint>

namespace ai::quest_accept
{
// Train 8b (twow-repo#338 measurement, OB-10 analysis 2026-09-30): roster bots took every
// quest a giver offered, and CleanQuestLogAction dropped the red ones again seconds to
// minutes later once the log held 14 quests - 45 % of all drops, some quests taken and
// dropped four times in an hour. A roster bot on its own now skips those quests.

// The red rule of CleanQuestLogAction: the quest level is at least 5 above the bot.
inline bool IsRed(uint32_t botLevel, uint32_t questLevel)
{
    return botLevel + 5 <= questLevel;
}

// Hotfix 8.11 (owner 02.10.2026, quest churn): 8.5/8.7 drop open grey quests of a roster
// bot on its own, but the accept paths only skipped red ones - QuestDetailsAction had no
// filter at all. Bots took a grey quest, dropped it, and took it again at the next giver:
// ~890 accepts and ~940 drops an hour on v24 ("The Grizzled Den" alone 205 times).
// Accepting now uses the drop rule: no grey (XP grey level) and no red quest for a roster
// bot on its own; class quests stay allowed, as the drop rule keeps them.
//
// Hotfix 8.15 (v27, 3 h: still ~275 drops an hour, e.g. one bot took and dropped the same two
// quests within 10 seconds): CleanQuestLogAction drops random quests without progress as soon as
// CleanFreeSlots or fewer slots are free, while the accept paths filled the log to the last slot.
// A roster bot on its own no longer takes a new quest at that fill level.
constexpr uint32_t CleanFreeSlots = 4;

inline bool LogTooFull(uint32_t freeSlots)
{
    return freeSlots <= CleanFreeSlots;
}

inline bool SkipForRosterBot(bool rosterOnItsOwn, uint32_t botLevel, uint32_t questLevel, uint32_t grayLevel,
    bool classQuest, uint32_t freeSlots = 25)
{
    if (!rosterOnItsOwn || classQuest)
        return false;
    return IsRed(botLevel, questLevel) || questLevel <= grayLevel || LogTooFull(freeSlots);
}
}
