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
}
