#pragma once

#include <cstdint>

namespace ai::resurrect_delay
{
// twow-repo#575 H3 (AiPlayerbot.Revive.DelayResurrectRequest; A3-HARM-UPSTREAM, OB-00 09.11.2026). Pure rules, no game types,
// no config reads.
//
// The key reads as a mode so that its acceptance unit can ride along in CLI-GRP's in-realm cohort test T30-1 (OB-00
// 09.11.2026, option (a)): 0 = off (default), 1 = every bot, 2 = only bots with guid bit 1 set, 3 = only bots with guid bit 1
// clear. Any other value acts like 0.
//
// Why bit 1 and not the CohortPolicy.h parity: T30-1 splits its own arms by parity (bit 0, CohortPolicy cohort A / B). A split
// by bit 1 is independent of it, so one realm carries a balanced 2x2 (each H3 group holds half of each T30 cohort). Values 2
// and 3 here are therefore NOT the CohortPolicy "cohort B / cohort A" modes (switch audit twow-repo#679: a key with its own
// meaning of 2 says so in the .dist). A bot never changes its group.
constexpr std::uint32_t ModeOff = 0;
constexpr std::uint32_t ModeAll = 1;
constexpr std::uint32_t ModeOnlyBit1Set = 2;
constexpr std::uint32_t ModeOnlyBit1Clear = 3;

inline std::uint32_t ClampMode(std::int32_t mode)
{
    return mode >= 0 && mode <= 3 ? std::uint32_t(mode) : ModeOff;
}

inline bool Bit1Set(std::uint32_t guidLow)
{
    return ((guidLow >> 1) & 1u) != 0;
}

// True when the delayed registration applies to the bot with this low guid.
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return mode == ModeAll || (mode == ModeOnlyBit1Set && Bit1Set(guidLow)) ||
           (mode == ModeOnlyBit1Clear && !Bit1Set(guidLow));
}
}
