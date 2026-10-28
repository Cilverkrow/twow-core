#pragma once

#include <cstddef>
#include <cstdint>

class PlayerbotAI;

// twow-repo#627/#628 (s7r 26.10.2026, AiPlayerbot.BotGroups.WorldThreadDefer, default 0): the world-thread group managers'
// accept and leave wrote their log lines synchronously - bot_events.csv through PlayerbotAIConfig::log (a mutex all bot
// threads share, fflush per line) and the [BotGroup] membership line through sLog. Inside a Scope on the world thread those
// writes are handed to the bot instead (PlayerbotAI::PostDeferredLog) and written in its next UpdateAI on its own thread.
// The target is thread-local: map threads never see a scope of the world thread.
namespace ai::deferred_log
{
// The bot that takes this thread's log lines; null outside a scope (then everything is written at once, as before).
PlayerbotAI* Target();

class Scope
{
public:
    // on = false: no scope (the key off) - the lines are written at once.
    Scope(PlayerbotAI* ai, bool on);
    ~Scope();
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;

private:
    PlayerbotAI* previous = nullptr;
    bool active = false;
};

// A bot keeps at most this many lines; more are counted as dropped (a bot that never updates must not grow without bound).
constexpr std::size_t kMaxLines = 64;

// Whether one more line fits; pure.
inline bool Fits(std::size_t queued)
{
    return queued < kMaxLines;
}
}
