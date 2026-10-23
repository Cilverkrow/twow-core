#include "playerbot/playerbot.h"
#include "playerbot/PosStuckPolicy.h"

// twow-repo#594 (AiPlayerbot.Path.PosStuck, OB-00 / OB-10 23.10.2026): the only place where the position guard moves a bot.
// TravelTarget::ObservePosStuck decides (switch, population, episode series, stood still, rate limit); TravelMgr.cpp itself
// stays free of teleports (the quest-first contract pins that the turn-in recovery never teleports). Unstuck = back to the
// progress anchor on the same map; Teleport = the bot's own home bind, without the hearthstone cooldown, and only when the
// bind lies on the bot's map and zone (OB-10 review: after a #587 rebind it can lie in another race's start area) - never
// the start-zone rescue (#587).
ai::pos_stuck::MoveResult ai::pos_stuck::ApplyMove(Player* bot, Step step, float x, float y, float z)
{
    if (!bot || step == Step::Park)
        return MoveResult::None;

    if (step == Step::Unstuck)
    {
        bot->NearTeleportTo(x, y, z, bot->GetOrientation());
        return MoveResult::Moved;
    }

    uint32 const bindMapId = bot->GetHomebindMapId();
    uint32 const bindZoneId = bindMapId == bot->GetMapId() ?
        sTerrainMgr.GetZoneId(bindMapId, bot->GetHomebindX(), bot->GetHomebindY(), bot->GetHomebindZ()) : 0;
    if (!BindNear(bindMapId, bindZoneId, bot->GetMapId(), bot->GetZoneId()))
        return MoveResult::BindFar;

    bot->TeleportToHomebind(0, false);
    return MoveResult::Moved;
}
