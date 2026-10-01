#pragma once

#include <cstdint>

namespace ai::fishing
{
// Hotfix 8.3 (OB-00 2026-10-01, Thunder Bluff pond): bots never caught a fish. The "fish"
// action recast after every global cooldown, each cast replaced the bobber before a fish
// could bite (5-25 s), and in the short gaps "done fishing" equipped the weapon again -
// 118 bots swapped pole and weapon every 1-3 minutes and stayed at Fishing 1/75.
constexpr uint32_t BobberEntry = 35591;
// After a fishing cast the weapon is not equipped again for this long.
constexpr uint32_t GraceSeconds = 30;

constexpr uint32_t Apprentice = 7620;
constexpr uint32_t Journeyman = 7731;
constexpr uint32_t Expert = 7732;
constexpr uint32_t Artisan = 18248;

// The highest fishing rank the bot knows, 0 for none (the action cast 7731 for everyone).
inline uint32_t KnownRank(bool artisan, bool expert, bool journeyman, bool apprentice)
{
    return artisan ? Artisan : expert ? Expert : journeyman ? Journeyman : apprentice ? Apprentice : 0;
}

inline bool InGrace(uint32_t lastCast, uint32_t now)
{
    return lastCast && now >= lastCast && now - lastCast < GraceSeconds;
}
}
