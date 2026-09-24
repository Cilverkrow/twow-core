#pragma once

#include <cstdint>

namespace ai::master_wait
{
// #276: a dead bot defers to an active real-player master (who may resurrect
// it) only for a bounded time. Owner decision 2026-09-24: 3 minutes in the
// open world, 10 minutes in dungeons and raids, as
// AiPlayerbot.DeadWaitForRealMasterSeconds / ...InstanceSeconds.
// 0 keeps the old unbounded wait.
inline std::uint32_t LimitFor(bool dungeonOrRaid, std::uint32_t worldSeconds, std::uint32_t instanceSeconds)
{
    return dungeonOrRaid ? instanceSeconds : worldSeconds;
}

inline bool IsExpired(std::int64_t secondsSinceDeath, std::uint32_t limitSeconds)
{
    return limitSeconds > 0 && secondsSinceDeath >= static_cast<std::int64_t>(limitSeconds);
}

// Seconds since the recorded death; falls back to the ghost time when the
// death itself was not observed (e.g. the bot logged in dead).
inline std::int64_t SecondsSinceDeath(std::int64_t now, std::int64_t deathTime, std::int64_t ghostTime)
{
    std::int64_t const since = deathTime > 0 ? deathTime : ghostTime;
    return since > 0 && now >= since ? now - since : 0;
}
}
