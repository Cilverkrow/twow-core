#include "playerbot/BotDialogueProvider.h"

#include "Log.h"

#include <atomic>
#include <ctime>
#include <deque>
#include <exception>
#include <mutex>

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

BotDialogueAnswer RunBotDialogueProvider(BotDialogueRequest const& request)
{
    BotDialogueProvider const fn = g_provider.load(std::memory_order_acquire);
    if (!fn)
        return BotDialogueAnswer();

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

    return BotDialogueAnswer();
}

char const* BotDialogueCommandText(BotDialogueCommand command)
{
    // The whole mapping, in one place, as literals. Nothing here is assembled
    // from anything a provider sent: an enum value selects one of these strings
    // or it selects nothing, so the text HandleCommand parses is always text
    // this file wrote.
    //
    // "do equip upgrades" rather than "equip upgrades" because the bare phrase
    // has no chat trigger in this tree -- ChatActionContext registers the
    // ACTION, and the only way chat reaches it is the "do " prefix, which is
    // also what EquipUpgradesAction::GetHelp tells a player to type. Note that
    // the action itself still refuses to run for a non-random bot unless
    // AiPlayerbot.AutoEquipUpgradeLoot is on; that gate is the operator's and
    // is left exactly where it is.
    switch (command)
    {
        case BotDialogueCommand::Follow:        return "follow";
        case BotDialogueCommand::Stay:          return "stay";
        case BotDialogueCommand::Flee:          return "flee";
        case BotDialogueCommand::Attack:        return "attack";
        case BotDialogueCommand::EquipUpgrades: return "do equip upgrades";
        case BotDialogueCommand::None:          return "";
    }

    // A value from a newer provider than this build. Silence, not a guess.
    return "";
}

namespace
{
    struct PendingCommand
    {
        BotDialogueCommandRequest request;
        std::time_t queuedAt = 0;
    };

    // Written from chat workers, read from bot ticks: a mutex, not an atomic,
    // because the thing being shared is a container rather than a word.
    std::mutex g_commandsMutex;
    std::deque<PendingCommand> g_commands;
}

void QueueBotDialogueCommand(BotDialogueCommandRequest const& request)
{
    // Three things a command cannot be executed without, checked here so that
    // an incomplete one costs nothing on the world thread. A speaker of zero is
    // the case that matters: it means the chat path could not resolve who
    // spoke, and there is then nobody to execute the command AS -- which is the
    // only way this feature is safe.
    if (request.command == BotDialogueCommand::None || !request.botGuidLow || !request.speakerGuidLow)
        return;

    if (!*BotDialogueCommandText(request.command))
    {
        sLog.outError("BotDialogueProvider: provider asked for command %u, which this build does not know; ignored",
            uint32_t(request.command));
        return;
    }

    PendingCommand pending;
    pending.request = request;
    pending.queuedAt = std::time(nullptr);

    std::lock_guard<std::mutex> lock(g_commandsMutex);
    while (g_commands.size() >= kMaxPendingBotDialogueCommands)
    {
        sLog.outError("BotDialogueProvider: %u commands already pending; dropping the oldest",
            uint32_t(g_commands.size()));
        g_commands.pop_front();
    }
    g_commands.push_back(pending);
}

bool TakeBotDialogueCommand(uint32_t botGuidLow, BotDialogueCommandRequest& out)
{
    if (!botGuidLow)
        return false;

    std::time_t const now = std::time(nullptr);

    std::lock_guard<std::mutex> lock(g_commandsMutex);
    for (auto it = g_commands.begin(); it != g_commands.end();)
    {
        if (now - it->queuedAt > std::time_t(kBotDialogueCommandTtlSeconds))
        {
            // Dropped wherever it is found rather than only at the front: a
            // command for a bot that never ticks again would otherwise sit
            // between two live ones forever.
            it = g_commands.erase(it);
            continue;
        }

        if (it->request.botGuidLow == botGuidLow)
        {
            out = it->request;
            g_commands.erase(it);
            return true;
        }

        ++it;
    }

    return false;
}
