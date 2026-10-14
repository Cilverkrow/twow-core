#pragma once

// twow-repo#581 (QUEST-LOOP-01, owner 11.10.2026 via OB-00): bots looped on emptied quest camps. A quest objective
// destination goes inactive when no living target is in sight, the bot cools down for 60 s (respawn: 120-600 s) and
// picks the same camp again; nothing remembered the failure, and the #405 work-timeout memory is per square, so a bot
// rotating between a quest's targets never reached it.
//   QL-1 AiPlayerbot.QuestLoop.MaxFailures (0 = off): inactive drops and work timeouts of a quest's objectives count
//        per bot and QUEST; MaxFailures of them within WindowSeconds skip all objectives of that quest for
//        CooldownSeconds (turn-ins and givers are not affected).
//   QL-2 AiPlayerbot.QuestLoop.InactiveCooldownSeconds (0 = off): the cooldown after an inactive objective lasts at
//        least this long instead of the destination's 60 s.

#include <algorithm>
#include <cstdint>

namespace ai::quest_loop
{
    struct Record
    {
        std::uint8_t failures = 0;
        std::uint32_t windowStart = 0;
        std::uint32_t suppressUntil = 0;
    };

    inline bool IsSuppressed(Record const& record, std::uint32_t nowMs)
    {
        return record.suppressUntil && std::int32_t(record.suppressUntil - nowMs) > 0;
    }

    // Returns true when this failure suppresses the quest's objectives.
    inline bool RecordFailure(Record& record, std::uint32_t nowMs, std::uint32_t maxFailures, std::uint32_t windowMs,
        std::uint32_t cooldownMs)
    {
        if (!maxFailures)
            return false;
        if (IsSuppressed(record, nowMs))
            return false;
        if (record.suppressUntil || !record.failures || nowMs - record.windowStart >= windowMs)
            record = Record{ 0, nowMs, 0 };
        if (record.failures < 255)
            ++record.failures;
        if (record.failures < maxFailures)
            return false;
        record = Record{ 0, nowMs, nowMs + cooldownMs };
        if (!record.suppressUntil)
            record.suppressUntil = 1;   // a wrapped clock must not read as "never suppressed"
        return true;
    }

    // QL-2: the cooldown after an inactive objective, never shorter than the destination's own.
    inline std::uint32_t InactiveCooldownMs(std::uint32_t destinationCooldownMs, std::uint32_t minSeconds)
    {
        return std::max(destinationCooldownMs, minSeconds * 1000u);
    }
}
