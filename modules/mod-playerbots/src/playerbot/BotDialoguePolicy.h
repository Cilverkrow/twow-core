#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ai
{
    enum class BotDialogueRoute
    {
        Whisper,
        Party,
        Raid,
        Other
    };

    // The first live dialogue slice is intentionally narrow. A direct whisper
    // names exactly one bot; group traffic is accepted only from that bot's
    // current real-player master. Everything else stays on the deterministic
    // PlayerBot response path and consumes no provider capacity.
    inline bool IsBotDialogueRouteEligible(BotDialogueRoute route, bool speakerIsRealPlayer,
                                           bool speakerIsMaster)
    {
        if (!speakerIsRealPlayer)
            return false;

        if (route == BotDialogueRoute::Whisper)
            return true;

        return (route == BotDialogueRoute::Party || route == BotDialogueRoute::Raid) &&
               speakerIsMaster;
    }

    // Preserve the existing "typing took time" effect without letting a
    // short model reply disappear for tens of seconds. A max of zero keeps the
    // old unlimited behaviour for callers that do not opt into the bound.
    inline std::uint32_t BotDialogueDelayMs(std::size_t characters, std::uint32_t msPerCharacter,
                                           std::uint32_t elapsedMs, std::uint32_t maxDelayMs)
    {
        std::uint64_t const raw = static_cast<std::uint64_t>(characters) * msPerCharacter;
        std::uint64_t remaining = raw > elapsedMs ? raw - elapsedMs : 0;
        if (maxDelayMs)
            remaining = std::min<std::uint64_t>(remaining, maxDelayMs);
        return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, std::numeric_limits<std::uint32_t>::max()));
    }
}
