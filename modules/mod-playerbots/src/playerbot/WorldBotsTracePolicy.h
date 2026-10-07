#pragma once

#include <cstdint>

namespace ai::world_bots
{
// twow-repo#541 (owner 07.10.2026 "das testen wir asap", OB-00 go for option A): what the bot module
// costs the serial world thread. RandomPlayerbotMgr::UpdateAIInternal runs every
// RandomBotUpdateInterval in the world thread; its parts are timed and summed per minute into one
// [WorldBots] line (AiPlayerbot.WorldBotsTrace, default 0). Log only, no behaviour change.
struct Window
{
    std::uint64_t minute = 0;        // minute of the window (seconds / 60), 0 = none yet
    std::uint32_t passes = 0;        // UpdateAIInternal passes
    std::uint64_t bots = 0;          // sum of desired bots over the passes
    std::uint64_t sessionsUs = 0;    // UpdateSessions (teleport ACKs, ghost recovery, logout)
    std::uint64_t processUs = 0;     // ProcessBot loop incl. logins
    std::uint64_t otherUs = 0;       // mem stores, quest rescues, activity, login waves, free bots
    std::uint64_t maxPassUs = 0;     // slowest single pass
    std::uint32_t logins = 0;        // bots logged in by the loop
    std::uint32_t teleportAcks = 0;  // far-teleport ACKs driven by UpdateSessions

    void Add(std::uint64_t desiredBots, std::uint64_t sessions, std::uint64_t process, std::uint64_t other,
        std::uint32_t loginCount, std::uint32_t acks)
    {
        ++passes;
        bots += desiredBots;
        sessionsUs += sessions;
        processUs += process;
        otherUs += other;
        std::uint64_t const pass = sessions + process + other;
        if (pass > maxPassUs)
            maxPassUs = pass;
        logins += loginCount;
        teleportAcks += acks;
    }

    // True when nowSeconds is in a later minute than the window: the caller logs the window, then
    // Reset(nowSeconds). The first call only opens the window.
    bool Due(std::uint64_t nowSeconds)
    {
        std::uint64_t const now = nowSeconds / 60;
        if (!minute)
        {
            minute = now;
            return false;
        }
        return now > minute;
    }

    void Reset(std::uint64_t nowSeconds)
    {
        *this = Window();
        minute = nowSeconds / 60;
    }

    // Share of the world thread in per mille: busy time of the window over 60 s.
    std::uint32_t PerMille() const
    {
        return std::uint32_t((sessionsUs + processUs + otherUs) / 60000);
    }
};
}
