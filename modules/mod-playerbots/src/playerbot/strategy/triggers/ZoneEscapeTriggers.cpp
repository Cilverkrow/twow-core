#include "playerbot/playerbot.h"
#include "ZoneEscapeTriggers.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/WorldPosition.h"

using namespace ai;

zone_escape::Facts ai::GatherZoneEscapeFacts(PlayerbotAI* ai)
{
    zone_escape::Facts facts;
    Player* bot = ai->GetBot();
    facts.enabled = sPlayerbotAIConfig.zoneEscapeEnabled;
    if (!facts.enabled || !bot)
        return facts;

    facts.rosterOnItsOwn = sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster();
    facts.alive = sServerFacade.IsAlive(bot);
    facts.inInstanceOrBattleground = bot->InBattleGround() || !WorldPosition(bot).isOverworld();
    if (!facts.rosterOnItsOwn || !facts.alive || facts.inInstanceOrBattleground)
        return facts;

    AiObjectContext* context = ai->GetAiObjectContext();
    // A sub-area without a level of its own takes its parent zone's level.
    facts.areaLevel = uint32(std::max<int32>(0, WorldPosition(bot).getAreaLevelOrParent()));
    facts.botLevel = bot->GetLevel();
    facts.inRestArea = bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);
    // Hearthing would not leave the zone anyway, and the start zone is where
    // a low-level bot belongs (train 5: L2 bot in a Durotar sub-area of level 8).
    WorldPosition const bind = AI_VALUE(WorldPosition, "home bind");
    if (AreaTableEntry const* area = bind.GetArea())
        facts.inHomeZone = bind.getMapId() == bot->GetMapId() && (area->zone ? area->zone : area->ID) == bot->GetZoneId();

    zone_escape::Step const lastStep = zone_escape::Step(AI_VALUE2(int, "manual int", "zone escape step"));
    facts.due = uint32(time(nullptr)) >= uint32(AI_VALUE2(time_t, "manual time", "zone escape")) +
        zone_escape::IntervalSeconds(lastStep, sPlayerbotAIConfig.zoneEscapeCooldownSeconds, sPlayerbotAIConfig.zoneEscapeRetrySeconds);
    // "hearthstone" is only useful when it is ready and its bind zone is not
    // itself clearly above the bot's level (#129).
    facts.hearthUsable = AI_VALUE2(bool, "action useful", "hearthstone");
    facts.hearthOnCooldown = !facts.hearthUsable && bot->HasItemCount(6948, 1) && !AI_VALUE2(bool, "spell ready", 8690) &&
        !homebind::IsZoneClearlyAboveLevel(uint32(std::max<int32>(0, bind.getAreaLevel())), facts.botLevel);
    return facts;
}

bool ZoneEscapeTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.zoneEscapeEnabled)
        return false;

    return zone_escape::Decide(GatherZoneEscapeFacts(ai)).step != zone_escape::Step::None;
}
