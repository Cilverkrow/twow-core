#include "playerbot/playerbot.h"
#include "UnstuckAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TeleportCastWait.h"   // twow-repo#333 finding 4: hearthstone cast wait

bool UnstuckAction::Execute(Event& event)
{
    std::string source = event.getSource();
    Player* bot = ai->GetBot();
    Player* master = ai->GetMaster();

    // twow-repo#333 finding 4 (TeleportCastPolicy.h): each run starts from the react delay. A hearthstone the bot already
    // prepares is waited for - its cooldown (item cheat) made "action useful" false, and the long-stuck branch repopped the
    // bot mid-cast. A started one holds for its cast: the engine sets the AI wait to this action's duration, and the next
    // movement interrupted the cast.
    uint32 const reactDelay = sPlayerbotAIConfig.reactDelay;
    SetDuration(reactDelay);
    auto const hearthPrepared = [&]()
    {
        uint32 const waitMs = ai::teleport_cast::PreparedWaitMs(bot, ai::teleport_cast::kHearthstone, reactDelay);
        if (waitMs)
            SetDuration(waitMs);
        return waitMs != 0;
    };
    auto const hearth = [&]()
    {
        if (!ai->DoSpecificAction("hearthstone", event, true))
            return false;
        SetDuration(ai::teleport_cast::HoldMs(ai::teleport_cast::PreparedWaitMs(bot, ai::teleport_cast::kHearthstone, reactDelay),
            ai->GetAIInternalUpdateDelay(), reactDelay));
        return true;
    };

    // Default action if no specific source is matched
    if (source.empty())
    {
        ai->TellDebug(master, "Unstuck: No specific source, resetting.", "debug unstuck");
        return ai->DoSpecificAction("reset", event, true);
    }

    // Handle move stuck scenarios
    if (source.find("move stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Move stuck detected, resetting.", "debug unstuck");
        return ai->DoSpecificAction("reset", event, true);
    }

    // Handle long move stuck scenarios
    if (source.find("move long stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Long move stuck detected, attempting hearthstone or repop.", "debug unstuck");
        if (hearthPrepared())
            return true;
        if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive())
        {
            return hearth();
        }
        else
        {
            return ai->DoSpecificAction("repop", event, true);
        }
    }

    // Handle combat stuck scenarios
    if (source.find("combat stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Combat stuck detected, resetting position.", "debug unstuck");
        return ai->DoSpecificAction("reset", event, true);
    }

    // Handle long combat stuck scenarios
    if (source.find("combat long stuck") != std::string::npos)
    {
        ai->TellDebug(master, "Unstuck: Long combat stuck detected, attempting hearthstone or repop.", "debug unstuck");
        if (hearthPrepared())
            return true;
        if (AI_VALUE2(bool, "action useful", "hearthstone") && bot->IsAlive())
        {
            return hearth();
        }
        else
        {
            return ai->DoSpecificAction("repop", event, true);
        }
    }

    // Fallback to reset if no specific condition is met
    ai->TellDebug(master, "Unstuck: Fallback to reset action.", "debug unstuck");
    return ai->DoSpecificAction("reset", event, true);
}
