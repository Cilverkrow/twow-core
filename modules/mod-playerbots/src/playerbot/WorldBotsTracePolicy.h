#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

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
    // twow-repo#597 (TICK-01, AiPlayerbot.WorldBotsHookTrace): the WorldScript update hooks of the bot
    // modules, every world tick (not only the UpdateAIInternal passes).
    std::uint64_t hookUs = 0;          // PlayerbotWorldScript::OnUpdate as a whole
    std::uint64_t remoteUs = 0;        // RunQueuedRemoteCommands
    std::uint64_t ahbotUs = 0;         // auctionbot.Update
    std::uint64_t dungeonClearUs = 0;  // DungeonClearReaperScript::OnUpdate (own WorldScript)

    void AddHook(std::uint64_t hook, std::uint64_t remote, std::uint64_t ahbot)
    {
        hookUs += hook;
        remoteUs += remote;
        ahbotUs += ahbot;
    }

    void AddDungeonClear(std::uint64_t us) { dungeonClearUs += us; }

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

    // twow-repo#597 (TICK-01): appended at the end of the [WorldBots] line with
    // AiPlayerbot.WorldBotsHookTrace; empty when off, so the line stays byte-identical.
    std::string HookSuffix(bool on) const
    {
        if (!on)
            return std::string();
        char buf[128];
        std::snprintf(buf, sizeof(buf), " hook_ms=%u remote_ms=%u ahbot_ms=%u dungeonclear_ms=%u",
            std::uint32_t(hookUs / 1000), std::uint32_t(remoteUs / 1000), std::uint32_t(ahbotUs / 1000),
            std::uint32_t(dungeonClearUs / 1000));
        return std::string(buf);
    }
};
}
