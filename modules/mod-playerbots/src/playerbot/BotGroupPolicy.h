#pragma once

namespace ai::bot_group
{
// twow-repo#365: bot-bot groups (at most MaxBots roster bots, level-coherent)
// with a group quest log. Design: twow-repo docs/design/bot-groups.md.
//
// Step 1 is observation only: this policy classifies a group the way the later
// formation rule will, and the result is only logged ([BotGroup], behind
// AiPlayerbot.BotGroups.Diagnostics). Nothing here admits, rejects or removes.

// A party holds five; one bot-bot group is at least two bots.
inline unsigned ClampMaxBots(int configured)
{
    if (configured < 2)
        return 2;
    if (configured > 5)
        return 5;
    return static_cast<unsigned>(configured);
}

enum class Verdict
{
    PlayerLed,   // a real player leads: player groups keep today's rules
    Fits,        // bot-led, within MaxBots and the level window
    TooManyBots, // bot-led, more bots than MaxBots
    LevelWindow, // bot-led, highest minus lowest member level above the window
};

// botMembers counts bots only; levelSpread is max - min level over all members.
inline Verdict Classify(bool leaderIsRealPlayer, unsigned botMembers, unsigned levelSpread,
    unsigned maxBots, unsigned levelWindow)
{
    if (leaderIsRealPlayer)
        return Verdict::PlayerLed;
    if (botMembers > maxBots)
        return Verdict::TooManyBots;
    if (levelSpread > levelWindow)
        return Verdict::LevelWindow;
    return Verdict::Fits;
}

inline char const* VerdictName(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::PlayerLed:   return "player_led";
        case Verdict::Fits:        return "fits";
        case Verdict::TooManyBots: return "too_many_bots";
        case Verdict::LevelWindow: return "level_window";
    }
    return "unknown";
}
}
