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

// Hotfix 8.7 (twow-repo#472): fishing bots stay at skill 1 although the zones allow every
// catch - the bobber is never used successfully. Counted per declared fishing purpose and
// written as one [Fishing] line when the purpose ends.
struct PurposeTrace
{
    uint32_t casts = 0;
    uint32_t castFailed = 0;
    uint32_t noPole = 0;
    uint32_t useSent = 0;
    uint32_t channelBreaks = 0;
    bool channel = false;
    bool usedSinceCast = false;

    void Reset() { *this = PurposeTrace(); }

    void OnCast(bool ok)
    {
        if (ok)
        {
            ++casts;
            usedSinceCast = false;
        }
        else
            ++castFailed;
    }

    void OnUse()
    {
        ++useSent;
        usedSinceCast = true;
    }

    // True when a fishing channel ended without the bobber being used (a lost catch).
    bool ObserveChannel(bool channelNow)
    {
        bool const broke = channel && !channelNow && !usedSinceCast;
        channel = channelNow;
        if (broke)
            ++channelBreaks;
        return broke;
    }
};
}
