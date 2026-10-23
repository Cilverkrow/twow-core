#include "playerbot/playerbot.h"
#include "playerbot/PosStuckPolicy.h"

// twow-repo#594 (AiPlayerbot.Path.PosStuck, OB-00 / OB-10 23.10.2026): the only place where the position guard moves a bot.
// TravelTarget::ObservePosStuck decides (switch, population, episode series, rate limit); TravelMgr.cpp itself stays free of
// teleports (the quest-first contract pins that the turn-in recovery never teleports). Unstuck = back to the progress anchor
// on the same map; Teleport = the bot's own home bind, without the hearthstone cooldown - never the start-zone rescue (#587).
void ai::pos_stuck::ApplyMove(Player* bot, Step step, float x, float y, float z)
{
    if (!bot || step == Step::Park)
        return;

    if (step == Step::Unstuck)
    {
        bot->NearTeleportTo(x, y, z, bot->GetOrientation());
        return;
    }

    bot->TeleportToHomebind(0, false);
}
