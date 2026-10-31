#pragma once

#include <algorithm>
#include <cstdint>

namespace ai::damage_log
{
// twow-repo#653 (OB-00 31.10.2026; AiPlayerbot.ActionLog.DamageSampleEvery, default 5): the action log's DMG_DEALT / DMG_TAKEN lines
// were every 5th damage event of a bot with ONE counter for dealt and taken, and a pet's, totem's or guardian's damage was never
// logged. That confounded the #653 fight timelines (a second attacker that joins at once showed its first logged hit ~10 s late)
// and made 29 of 56 "no damage dealt" fights a logging gap. Now dealt and taken have their own counter, the rate is a key (1 = every
// event, for the action-log sample bots of behaviour runs - more lines with a flush each, never a perf statement), and a bot's
// minion's damage goes to the owner's log as DMG_DEALT with via=. Pure rules, no game types.
constexpr std::uint32_t DefaultEvery = 5;
constexpr std::uint32_t MaxEvery = 100;

// The configured rate: below 1 is the default, above MaxEvery is MaxEvery.
inline std::uint32_t Every(std::int32_t configured)
{
    if (configured < 1)
        return DefaultEvery;
    return std::min<std::uint32_t>(MaxEvery, std::uint32_t(configured));
}

// The n-th event of a counter (n counted from 1) is written when n is a multiple of the rate; rate 1 writes every event.
inline bool Due(std::uint32_t n, std::uint32_t every)
{
    return every <= 1 || n % every == 0;
}
}
