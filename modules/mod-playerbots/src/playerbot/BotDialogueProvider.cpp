#include "playerbot/BotDialogueProvider.h"

#include "Log.h"

#include <atomic>
#include <exception>

namespace
{
    // Atomic, unlike the augmenter vector in AiContextAugment.cpp, and the
    // difference is not fussiness: that list is written and read on the world
    // thread only, while this one is WRITTEN at startup and READ from the chat
    // path's async workers. A plain pointer would be a data race the first time
    // a bot was spoken to during startup.
    std::atomic<BotDialogueProvider> g_provider{nullptr};
}

void RegisterBotDialogueProvider(BotDialogueProvider fn)
{
    if (!fn)
        return;

    BotDialogueProvider const previous = g_provider.exchange(fn, std::memory_order_release);
    if (previous)
    {
        // Two providers is a configuration mistake, not a supported layering:
        // a bot has one voice. Say so rather than letting the second one
        // silently win.
        sLog.outError("BotDialogueProvider: a second provider replaced the first; a bot has one voice");
    }
}

bool HasBotDialogueProvider()
{
    return g_provider.load(std::memory_order_acquire) != nullptr;
}

std::string RunBotDialogueProvider(BotDialogueRequest const& request)
{
    BotDialogueProvider const fn = g_provider.load(std::memory_order_acquire);
    if (!fn)
        return std::string();

    try
    {
        return fn(request);
    }
    catch (std::exception const& e)
    {
        // This runs on a detached-ish async worker. An escaping exception is
        // std::terminate for the whole worldserver, not a quiet bot.
        sLog.outError("BotDialogueProvider: provider threw: %s", e.what());
    }
    catch (...)
    {
        sLog.outError("BotDialogueProvider: provider threw an unknown exception");
    }

    return std::string();
}
