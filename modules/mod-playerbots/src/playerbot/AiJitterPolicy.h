#pragma once

#include <cstdint>

namespace ai::jitter
{
// twow-repo#541 (deep dive: spikes per region update, OB-00 go 10.10.2026 for jitter):
// the AI waits fixed delays (ReactDelay, GlobalCooldown, PassiveDelay, RepeatDelay) that count
// down with the same map diff for every bot of a region, so bots that come into phase - a login
// wave, `rndbot park ... here`, a shared fight - keep running their AI in the same tick. A
// symmetric spread of +-pct per delay keeps the mean and pulls them apart.
// AiPlayerbot.AiDelayJitterPct, default 0 = off.
constexpr std::uint32_t MaxPct = 50;
constexpr std::uint32_t RollRange = 1000;   // roll in [0, RollRange]

// delay +- pct, roll 0 -> -pct, RollRange / 2 -> +0, RollRange -> +pct.
inline std::uint32_t JitteredDelay(std::uint32_t delay, std::uint32_t pct, std::uint32_t roll)
{
    if (!pct || !delay)
        return delay;
    if (pct > MaxPct)
        pct = MaxPct;
    if (roll > RollRange)
        roll = RollRange;
    std::int64_t const span = std::int64_t(delay) * pct / 100;
    std::int64_t const offset = (std::int64_t(roll) * 2 * span) / RollRange - span;
    std::int64_t const result = std::int64_t(delay) + offset;
    return result < 0 ? 0 : std::uint32_t(result);
}

// A parked bot thinks every intervalMs. Without a spread every bot parked by one command would
// think in the same tick, every interval. The last-update stamp is moved back by
// roll / RollRange of an interval, so the first update lands anywhere in the next interval.
// 0 keeps the meaning "update now"; the stamp is never 0.
inline std::uint32_t SpreadLastUpdate(std::uint32_t nowMs, std::uint32_t intervalMs, std::uint32_t roll)
{
    if (roll > RollRange)
        roll = RollRange;
    std::uint32_t const back = std::uint32_t((std::uint64_t(intervalMs) * roll) / RollRange);
    std::uint32_t const stamp = nowMs - back;
    return stamp ? stamp : 1;
}

// One-off random delay of the first AI update after login, in [0, maxMs].
inline std::uint32_t StartOffset(std::uint32_t maxMs, std::uint32_t roll)
{
    if (roll > RollRange)
        roll = RollRange;
    return std::uint32_t((std::uint64_t(maxMs) * roll) / RollRange);
}
}
