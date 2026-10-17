#pragma once

// twow-repo#597 (TICK-01): AhBot::Update had no enabled check. Without an ahbot.conf (live 14.10.2026: "AhBot is
// Disabled. No configuration file ...") AhBotConfig::Initialize returns before it reads updateInterval, so the
// interval stays 0 and every world tick schedules an auction check: one detached boost::thread, one synchronous
// "mail ... like 'AH Proposition%'" query and three log lines (~20 thread starts a second, 53 % of all log lines, right
// before every stall >= 2 s). Switch AiPlayerbot.AhBot.RequireConfig (default 0 = as before), read from aiplayerbot.conf
// because it has to work exactly when ahbot.conf is missing: with 1 Update goes idle while the AhBot config is not
// loaded or AhBot is not enabled. Pure logic only; the integration is pinned by
// t/ahbot_guard_597_source_contract_tests.cmake.

namespace ahbot::guard
{
    // True when AhBot::Update must return before anything else (no queued work, no thread, no query, no line).
    // Fails closed: with the switch on, only a loaded AND enabled config lets Update run.
    inline bool SkipUpdate(bool requireConfig, bool configLoaded, bool enabled)
    {
        return requireConfig && !(configLoaded && enabled);
    }

    // The update= token of the one [AhBotGuard] startup line.
    inline char const* UpdateMode(bool requireConfig, bool configLoaded, bool enabled)
    {
        return SkipUpdate(requireConfig, configLoaded, enabled) ? "idle" : "scheduled";
    }
}
