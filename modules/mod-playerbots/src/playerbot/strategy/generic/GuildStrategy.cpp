#include "playerbot/playerbot.h"
#include "GuildStrategy.h"

using namespace ai;

void GuildStrategy::InitNonCombatTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("offer petition nearby", 4.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "random",
        NextAction::array(0, new NextAction("guild manage nearby", 4.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "petition signed",
        NextAction::array(0, new NextAction("turn in petition", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "buy tabard",
        NextAction::array(0, new NextAction("buy tabard", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "leave large guild",
        NextAction::array(0, new NextAction("guild leave", 4.0f), NULL)));

    // twow-repo#485: guild note of roster bots (AiPlayerbot.RosterGuild.GuildNote, default off).
    triggers.push_back(new TriggerNode(
        "roster guild note",
        NextAction::array(0, new NextAction("roster guild note", 4.0f), NULL)));

    // twow-repo#485 / #518: role report for the role fill of roster guilds (default off).
    triggers.push_back(new TriggerNode(
        "roster guild role",
        NextAction::array(0, new NextAction("roster guild role", 4.0f), NULL)));

    // Hotfix 9.1 (twow-repo#485, AiPlayerbot.RosterGuild.FoundTravel, default off): founding trip to
    // a guild master and the charter offered to the online bots of the plan guild.
    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("roster guild found trip", 4.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("roster guild offer remote", 4.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("guild craft order", 10.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("guild share item", 9.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "often",
        NextAction::array(0, new NextAction("guild ah buy", 1.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "very often",
        NextAction::array(0, new NextAction("guild accept quest order", 11.0f), NULL)));
}
