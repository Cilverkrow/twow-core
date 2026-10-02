#pragma once

#include "FunserverRidingStages.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace ai::riding_stages
{
// twow-repo#295 (owner 2026-10-02): bots follow the riding stages of the players.
// They train riding at their trainer and buy their mounts with real gold; the
// factory hands out neither. The rules (ranks, families, speeds, prices) are the
// core's FunserverRidingStages.h; this header holds only the bot's decisions on
// top of them, behind the core switch Funserver.Riding.Stages.Enabled.

// Mount 1 (family 1) comes with the first stage, mount 2 (family 2) with the third.
std::uint32_t constexpr MOUNT1_LEVEL = FunserverRiding::RANK_LEVEL[0];
std::uint32_t constexpr MOUNT2_LEVEL = FunserverRiding::RANK_LEVEL[2];

// Money a bot keeps for riding: the rank its trainer would teach now (the
// "train cost" of the mount trainers) plus the mount it still lacks.
inline std::uint32_t MountBudgetCopper(std::uint32_t level, std::uint32_t trainCostCopper, bool hasMount, bool hasSwiftMount)
{
    std::uint64_t money = trainCostCopper;
    if (level >= MOUNT1_LEVEL && !hasMount)
        money += FunserverRiding::MOUNT1_PRICE_COPPER;
    if (level >= MOUNT2_LEVEL && !hasSwiftMount)
        money += FunserverRiding::MOUNT2_PRICE_COPPER;
    return money > std::numeric_limits<std::uint32_t>::max() ? std::numeric_limits<std::uint32_t>::max() : std::uint32_t(money);
}

// A vendor trip for a mount makes sense: family 1 from level 10 with riding 75
// when the bot has no mount, family 2 from level 40 with riding 225 when it has
// no swift one. The riding values are the item requirements, so a bot never
// travels for a mount it could not use.
inline bool MayBuyMount(std::uint32_t level, std::uint32_t ridingSkill, bool hasMount, bool hasSwiftMount)
{
    if (!hasMount && level >= MOUNT1_LEVEL && ridingSkill >= FunserverRiding::FAMILY1_REQUIRED_SKILL)
        return true;
    return !hasSwiftMount && level >= MOUNT2_LEVEL && ridingSkill >= FunserverRiding::FAMILY2_REQUIRED_SKILL;
}

// What an offered mount is worth, by the speed the server gives this bot on it,
// against the speeds of the mounts the bot has (shapeshift forms not counted):
// faster than all of them is an upgrade to buy, as fast as the best is kept,
// slower is not needed.
enum class MountOffer : std::uint8_t
{
    NotNeeded,
    Keep,
    Upgrade,
};

inline MountOffer ClassifyMountOffer(std::uint32_t offeredSpeed, std::vector<std::uint32_t> const& ownedSpeeds)
{
    if (!offeredSpeed)
        return MountOffer::NotNeeded;

    bool same = false;
    for (std::uint32_t const owned : ownedSpeeds)
    {
        if (owned > offeredSpeed)
            return MountOffer::NotNeeded;
        if (owned == offeredSpeed)
            same = true;
    }
    return same ? MountOffer::Keep : MountOffer::Upgrade;
}

// Run speed for the travel time budget. With riding stages a mounted bot moves
// up to 2.8 times as fast and dismounts for every fight on the way, so a budget
// taken while mounted ran out on the walk; the budget uses the unmounted run
// speed, or the slower one of a slowed bot. Without the switch: as before.
inline float TravelBudgetRunSpeed(bool stagesEnabled, float currentRunSpeed, float baseRunSpeed)
{
    if (!stagesEnabled)
        return currentRunSpeed;
    return (currentRunSpeed > 0.0f && currentRunSpeed < baseRunSpeed) ? currentRunSpeed : baseRunSpeed;
}
}
