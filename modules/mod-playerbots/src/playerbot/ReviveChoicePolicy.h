#pragma once

#include <cstdint>

namespace ai::revive_choice
{
// Hotfix 8.34 (twow-repo#544): v35 revived 506 times at the spirit healer and 13 times at the
// corpse in 6.56 h. ShouldSpiritHealerValue sent a bot to the spirit healer when the graveyard was in
// sight and the corpse was not (the ghost always starts at the graveyard, so nearly every time with
// the repair cheat) or when enemies stood near the corpse. Above level 10 that costs resurrection
// sickness (up to 10 minutes at reduced stats). With AiPlayerbot.Revive.PreferCorpseRun = 1 a roster
// bot on its own above level 10 takes these two shortcuts only from the second death in a row;
// every other spirit-healer reason (sickness, low durability, many deaths, long dead) is unchanged.
constexpr std::uint32_t NoSicknessMaxLevel = 10;
constexpr std::uint32_t ShortcutFromDeathCount = 2;

inline bool ShortcutToSpiritHealerAllowed(bool preferCorpseRun, bool rosterOnItsOwn, std::uint32_t level, std::uint32_t deathCount)
{
    if (!preferCorpseRun || !rosterOnItsOwn || level <= NoSicknessMaxLevel)
        return true;
    return deathCount >= ShortcutFromDeathCount;
}
}
