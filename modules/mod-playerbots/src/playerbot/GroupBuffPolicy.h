#pragma once

#include <cstdint>
#include <map>

namespace ai::group_buff
{
// twow-repo#420 (2026-09-28): in the owner's raid test in Ironforge the group bots
// kept buffing and sat down to drink while following, which broke the formations.
// Two causes in the code: party buffs look for "friendly unit without aura", which
// also takes up to 100 nearby players outside the group - in a capital a steady
// stream of new targets - and bots drank below 65 % mana even while their master
// was walking on.

// Bots grouped with a real player buff their group only; free random bots keep
// buffing strangers as part of their organic play.
inline bool MayBuffOutOfGroup(bool inGroupWithRealPlayer)
{
    return !inGroupWithRealPlayer;
}

// While the master walks on, a following bot only stops to drink below
// mediumMana; a standing master keeps the normal threshold.
inline bool DrinkWhileMasterMoves(unsigned manaPct, unsigned mediumMana)
{
    return manaPct < mediumMana;
}

// [GroupBuff] diagnostic: helpful aura casts on others outside combat, per bot,
// reported once per window.
constexpr uint32_t WindowSeconds = 60;
constexpr uint32_t SameTargetSeconds = 60;

struct BuffWindow
{
    uint32_t start = 0;
    uint32_t casts = 0;
    uint32_t outOfGroup = 0;
    uint32_t sameTarget = 0;
    std::map<uint64_t, uint32_t> lastCastAt;

    void Record(uint32_t now, uint64_t target, bool outOfGroupTarget)
    {
        if (!casts)
            start = now;
        ++casts;
        if (outOfGroupTarget)
            ++outOfGroup;

        auto const last = lastCastAt.find(target);
        if (last != lastCastAt.end() && now - last->second < SameTargetSeconds)
            ++sameTarget;
        lastCastAt[target] = now;
    }

    bool Due(uint32_t now) const
    {
        return casts && now - start >= WindowSeconds;
    }

    void Reset(uint32_t now)
    {
        start = 0;
        casts = 0;
        outOfGroup = 0;
        sameTarget = 0;
        for (auto i = lastCastAt.begin(); i != lastCastAt.end();)
        {
            if (now - i->second >= SameTargetSeconds)
                i = lastCastAt.erase(i);
            else
                ++i;
        }
    }
};
}
