
#include "playerbot/playerbot.h"
#include "PassiveMultiplier.h"
#include <mutex>

using namespace ai;

std::list<std::string> PassiveMultiplier::allowedActions;
std::list<std::string> PassiveMultiplier::allowedParts;

PassiveMultiplier::PassiveMultiplier(PlayerbotAI* ai) : Multiplier(ai, "passive")
{
    // Audit A29 (twow-repo#563): strategies are built on the bots' region threads; the static lists were
    // filled by whichever came first while others already iterated them. Filled exactly once now.
    static std::once_flag listsOnce;
    std::call_once(listsOnce, []()
    {
        allowedActions.push_back("co");
        allowedActions.push_back("nc");
        allowedActions.push_back("load ai");
        allowedActions.push_back("save ai");
        allowedActions.push_back("list ai");
        allowedActions.push_back("reset ai");
        allowedActions.push_back("reset strats");
        allowedActions.push_back("reset values");
        allowedActions.push_back("check mount state");
        allowedActions.push_back("accept invitation");
        allowedActions.push_back("join");
        allowedActions.push_back("lfg");

        allowedParts.push_back("follow");
        allowedParts.push_back("stay");
        allowedParts.push_back("chat shortcut");
    });
}

float PassiveMultiplier::GetValue(Action* action) 
{
    if (!action)
		return 1.0f;

    std::string name = action->getName();

    for (std::list<std::string>::iterator i = allowedActions.begin(); i != allowedActions.end(); i++)
    {
        if (name == *i)
            return 1.0f;
    }

    for (std::list<std::string>::iterator i = allowedParts.begin(); i != allowedParts.end(); i++)
    {
        if (name.find(*i) != std::string::npos)
            return 1.0f;
    }

    return 0;
}
