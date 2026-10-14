#pragma once

// twow-repo#587 (ZONE-01, owner goal 14.10.2026: bots move up into level-appropriate zones on their own). Live 16:54Z:
// 81 of 270 bots were off their home continent - the quest rescue (#421 C) picked start areas / inns of other races on
// either continent, and quest routes between the continents stay blocked below level 61 (owner decision 04.10, until
// the #342 transports are reliable), so such a bot never came back; 18 bots worked on battleground quests.
//   Z1a AiPlayerbot.ZoneExit.RescueHomeContinent: the rescue stays on the bot's continent, and a bot off its home
//       continent is rescued back to it (a teleport, no transport route).
//   Z1b AiPlayerbot.ZoneExit.StrandedRescueAfter (0 = off): after that many empty quest searches in a row that only
//       other-continent candidates emptied, the bot asks for the rescue itself (the rescue limiter still applies).
//   Z3  AiPlayerbot.ZoneExit.SkipBattlegroundQuests: quests sorted under a battleground are no quest-first targets.

#include <cstdint>

namespace ai::zone_exit
{
    // Quest sort / zone of the classic battlegrounds: Warsong Gulch 3277, Arathi Basin 3358, Alterac Valley 2597.
    inline bool IsBattlegroundQuestSort(std::int32_t zoneOrSort)
    {
        return zoneOrSort == 3277 || zoneOrSort == 3358 || zoneOrSort == 2597;
    }

    // The continent the rescue should land on: back home when the bot is on the other one.
    inline std::uint32_t RescueContinent(std::uint32_t botMap, std::uint32_t homeMap)
    {
        return botMap != homeMap ? homeMap : botMap;
    }

    // An empty quest search that only the continent rule emptied (no level, danger, hub or activity reason).
    inline bool OnlyOtherContinent(std::uint32_t crossMap, std::uint32_t otherRejects)
    {
        return crossMap > 0 && otherRejects == 0;
    }

    // Count of such searches in a row; any other outcome resets it. Returns the new count.
    inline std::uint32_t NextStrandedCount(std::uint32_t count, bool onlyOtherContinent)
    {
        return onlyOtherContinent ? count + 1 : 0;
    }

    inline bool StrandedRescueDue(std::uint32_t count, std::uint32_t threshold)
    {
        return threshold > 0 && count >= threshold;
    }
}
