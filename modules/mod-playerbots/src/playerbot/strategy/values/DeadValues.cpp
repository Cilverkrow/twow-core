
#include "playerbot/playerbot.h"
#include "DeadValues.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/GraveyardSelectionPolicy.h"
#include "playerbot/ReviveChoicePolicy.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/CohortPolicy.h"
#include "playerbot/CorpseRunFirstPolicy.h"   // twow-repo#575 A3 step 4 S4-1: Revive.CorpseRunFirst

using namespace ai;

GuidPosition GraveyardValue::Calculate()
{
    WorldPosition refPosition = bot, botPos(bot);

    if (getQualifier() == "master")
    {
        if (ai->GetGroupMaster() && ai->IsSafe(ai->GetGroupMaster()) && ai->GetGroupMaster()->GetMapId() == bot->GetMapId())
        {
            refPosition = ai->GetGroupMaster();
        }
    }
    else if (getQualifier() == "travel")
    {
        auto travelTarget = AI_VALUE(TravelTarget*, "travel target");

        if (travelTarget && travelTarget->GetPosition() && travelTarget->GetPosition()->getMapId() == bot->GetMapId())
        {
            refPosition = *travelTarget->GetPosition();
        }
    }
    else if (getQualifier() == "another closest appropriate")
    {
        //just get ANOTHER nearest appropriate for level (neutral or same team zone)
        if (auto anotherAppropriate = GetAnotherAppropriateClosestGraveyard())
        {
            return GuidPosition(0, anotherAppropriate);
        }
    }

    WorldSafeLocsEntry const* ClosestGrave = bot->GetMap()->GetGraveyardManager().GetClosestGraveYard(
        refPosition.getX(),
        refPosition.getY(),
        refPosition.getZ(),
        refPosition.getMapId(),
        bot->GetTeam(),
        bot->GetLevel()     // twow-repo#575 F-b V2: the same level-limited graveyard the core releases the bot to
    );

    if (!ClosestGrave)
    {
        sLog.outDetail(
            "ERROR: Unable to find closest graveyard in GraveyardValue, will return GuidPosition() which is 0,0,0 - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        return GuidPosition();
    }

    return GuidPosition(0, ClosestGrave);
}

WorldSafeLocsEntry const* GraveyardValue::GetAnotherAppropriateClosestGraveyard() const
{
    // near
    float distNear = std::numeric_limits<float>::max();
    WorldSafeLocsEntry const* entryNear = nullptr;

    // far
    WorldSafeLocsEntry const* entryFar = nullptr;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
        return nullptr;

    uint32 botMapId = corpse->GetMapId();
    uint32 botZoneId = corpse->GetZoneId();

    for (auto mapValues : sWorld.GetGraveyardManager().GetGraveyardMap())
    {
        uint32 locId = mapValues.first;
        GraveYardData const& graveyardData = mapValues.second;

        //skip non-neutral or hostile graveyards
        if (graveyardData.team != bot->GetTeam() && graveyardData.team != TEAM_BOTH_ALLOWED)
            continue;

        WorldSafeLocsEntry const* graveyardCoreEntry = sWorldSafeLocsStore.LookupEntry(graveyardData.safeLocId);
        if (!graveyardCoreEntry)
            continue;

        //skip different maps (no need for other continents)
        if (graveyardCoreEntry->map_id != botMapId)
            continue;

        uint32 graveyardZoneId = sTerrainMgr.GetZoneId(graveyardCoreEntry->map_id, graveyardCoreEntry->x, graveyardCoreEntry->y, graveyardCoreEntry->z);
        auto graveyardAreaEntry = GetAreaEntryByAreaID(graveyardZoneId);

        //skip same zone
        if (graveyardZoneId == botZoneId)
            continue;

        if (!graveyardAreaEntry)
            continue;

        //skip higher level zones and zones without a known level
        if (!graveyard_policy::IsZoneLevelAppropriate((uint32)graveyardAreaEntry->area_level, bot->GetLevel()))
            continue;

        float dist = WorldPosition(corpse).sqDistance(graveyardCoreEntry);

        //skip graveyards too far from the corpse (#276: Elwynn death, Hillsbrad revive)
        if (!graveyard_policy::IsWithinAlternateDistance(sqrt(dist), sPlayerbotAIConfig.maxAlternateGraveyardDistance))
            continue;

        if (dist < distNear)
        {
            distNear = dist;
            entryNear = graveyardCoreEntry;
        }
    }

    if (entryNear)
        return entryNear;

    return entryFar;
}

GuidPosition BestGraveyardValue::Calculate()
{
    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        sLog.outDetail(
            "ERROR: Unable to find closest graveyard in BestGraveyardValue, will return GuidPosition() which is 0,0,0 - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
        return GuidPosition();
    }

    uint32 deathCount = AI_VALUE(uint32, "death count");

    //attempt to revive at other same map graveyards which are not enemy territory
    if (!ai->HasActivePlayerMaster() && deathCount >= DEATH_COUNT_BEFORE_TRYING_ANOTHER_GRAVEYARD)
    {
        GuidPosition anotherGraveyard = AI_VALUE2(GuidPosition, "graveyard", "another closest appropriate");
        if (anotherGraveyard)
        {
            return anotherGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find another closest appropriate graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    //Revive near master.
    if ((ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
        ai->GetGroupMaster() && ai->GetGroupMaster() != bot)
    {
        GuidPosition masterGraveyard = AI_VALUE2(GuidPosition, "graveyard", "master");
        if (masterGraveyard)
        {
            return masterGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find master graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    //Revive near travel target if it's far away from last death, but never
    //implausibly far from the corpse (#276).
    if (AI_VALUE2(GuidPosition, "graveyard", "travel") && AI_VALUE2(GuidPosition, "graveyard", "travel").fDist(corpse) > sPlayerbotAIConfig.reactDistance &&
        graveyard_policy::IsWithinAlternateDistance(AI_VALUE2(GuidPosition, "graveyard", "travel").fDist(corpse), sPlayerbotAIConfig.maxAlternateGraveyardDistance))
    {
        GuidPosition travelGraveyard = AI_VALUE2(GuidPosition, "graveyard", "travel");
        if (travelGraveyard)
        {
            return travelGraveyard;
        }
        sLog.outDetail(
            "ERROR: Unable to find travel graveyard in BestGraveyardValue, resorting to self graveyard - bot #%d %s:%d <%s>",
            bot->GetGUIDLow(),
            bot->GetTeam() == ALLIANCE ? "A" : "H",
            bot->GetLevel(),
            bot->GetName()
        );
    }

    return AI_VALUE2(GuidPosition, "graveyard", "self");
}

// twow-repo#575 A3 step 4 S4-1 (AiPlayerbot.Revive.CorpseRunFirst, CorpseRunFirstPolicy.h): the spirit healer only with a reason,
// else the corpse run (the reclaim itself runs through ReviveFromCorpseAction::isUseful). One [CorpseRun] line per death and
// decision, a [CorpseRunMin] line per minute; the counters make real loops at the corpse visible (reason=loop), next to the
// unchanged [DeathLoop] evacuation and the death count > 15 repop.
static bool CorpseRunFirstHealer(PlayerbotAI* ai, Player* bot, uint8 durability)
{
    namespace cf = ai::corpse_first;
    uint32 const now = uint32(time(nullptr));
    Corpse* corpse = bot->GetCorpse();
    // Not released yet (no corpse, no ghost): the spirit healer is not useful before the release anyway; nothing to decide.
    if (!corpse && !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return true;

    cf::Input in;
    in.hasCorpse = corpse != nullptr;
    in.sickness = ai->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS, bot);
    in.durability = durability;
    in.maxSeconds = sPlayerbotAIConfig.reviveCorpseRunFirstMaxSeconds;
    uint32 deathAt = 0;
    float corpseDistance = -1.0f;
    if (corpse)
    {
        deathAt = uint32(corpse->GetGhostTime());
        in.deadSeconds = now > deathAt ? now - deathAt : 0;
        in.deathsNear = cf::DeathsNear(ai->GetCorpseFirstDeaths(), now, corpse->GetMapId(), corpse->GetPositionX(), corpse->GetPositionY());
        corpseDistance = WorldPosition(bot).fDist(corpse);
    }

    cf::Reason const reason = cf::Decide(in);
    cf::Counters& counters = cf::SharedCounters();
    if (uint32 const kind = ai->MarkCorpseFirstDecision(deathAt, reason))
    {
        if (kind == 1)
            counters.deaths.fetch_add(1, std::memory_order_relaxed);
        counters.decided[std::size_t(reason)].fetch_add(1, std::memory_order_relaxed);
        sLog.outBasic("[CorpseRun] bot=%u cohort=%c level=%u map=%u zone=%u decision=%s reason=%s dead_s=%u corpse_d=%.0f deaths_10m=%u first=%u",
            bot->GetGUIDLow(), ai::cohort::Letter(bot->GetGUIDLow()), bot->GetLevel(), bot->GetMapId(), bot->GetZoneId(),
            reason == cf::Reason::Corpse ? "corpse" : "healer", cf::ReasonName(reason), in.deadSeconds, corpseDistance, in.deathsNear,
            kind == 1 ? 1u : 0u);
    }

    if (counters.LogDue(now))
    {
        uint32 v[std::size_t(cf::Reason::Count)];
        uint32 any = 0;
        for (std::size_t i = 0; i < std::size_t(cf::Reason::Count); ++i)
            any |= v[i] = counters.decided[i].exchange(0, std::memory_order_relaxed);
        uint32 const deaths = counters.deaths.exchange(0, std::memory_order_relaxed);
        uint32 const reclaimed = counters.reclaimed.exchange(0, std::memory_order_relaxed);
        uint32 const ghostStuck = counters.ghostStuck.exchange(0, std::memory_order_relaxed);
        if (any || deaths || reclaimed || ghostStuck)
            sLog.outBasic("[CorpseRunMin] deaths=%u corpse=%u no_corpse=%u sickness=%u durability=%u loop=%u timeout=%u reclaimed=%u ghost_stuck=%u",
                deaths, v[std::size_t(cf::Reason::Corpse)], v[std::size_t(cf::Reason::NoCorpse)], v[std::size_t(cf::Reason::Sickness)],
                v[std::size_t(cf::Reason::Durability)], v[std::size_t(cf::Reason::Loop)], v[std::size_t(cf::Reason::Timeout)], reclaimed, ghostStuck);
    }

    return reason != cf::Reason::Corpse;
}

bool ShouldSpiritHealerValue::Calculate()
{
    uint32 deathCount = AI_VALUE(uint32, "death count");
    uint8 durability = AI_VALUE(uint8, "durability");

    if (ai->HasActivePlayerMaster()) //Only use spirit healers with direct command with active master.
        return false;

    // twow-repo#575 A3 step 4 S4-1 (AiPlayerbot.Revive.CorpseRunFirst): the rules below stay for every other bot.
    if (ai::cohort::Applies(sPlayerbotAIConfig.reviveCorpseRunFirst, bot->GetGUIDLow()))
        return CorpseRunFirstHealer(ai, bot, durability);

    //Nothing to lose
    if (ai->HasAura(SPELL_ID_PASSIVE_RESURRECTION_SICKNESS, bot) || durability < 10)
        return true;

    //Died too many times
    if (deathCount > DEATH_COUNT_BEFORE_REVIVING_AT_SPIRIT_HEALER)
        return true;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse)
    {
        //if no corpse (?) then definitely should revive at spirit healer
        return true;
    }

    uint32 deadTime = time(nullptr) - corpse->GetGhostTime();

    //Dead for a long time
    if (deadTime > 10 * MINUTE && deathCount > 1)
        return true;

    //Dead for a long time
    if (deadTime > 20 * MINUTE)
        return true;

    // Hotfix 8.34 (twow-repo#544): the two shortcuts below (enemies near the corpse, graveyard in
    // sight) only from the second death in a row for a roster bot on its own above level 10.
    bool const shortcutAllowed = revive_choice::ShortcutToSpiritHealerAllowed(sPlayerbotAIConfig.revivePreferCorpseRun,
        sRandomPlayerbotMgr.IsPersistentRosterMember(bot->GetGUIDLow()) && !ai->HasRealPlayerMaster(), bot->GetLevel(), deathCount);

    //If there are enemies near grave and corpse we go to corpse first.
    if (AI_VALUE2(bool, "manual bool", "enemies near graveyard"))
        return false;

    //Enemies near corpse so try grave first.
    if (shortcutAllowed && AI_VALUE2(bool, "manual bool", "enemies near corpse"))
        return true;

    GuidPosition graveyard = AI_VALUE(GuidPosition, "best graveyard");

    float corpseDistance = WorldPosition(bot).fDist(corpse);
    float graveYardDistance = WorldPosition(bot).fDist(graveyard);
    bool corpseInSight = corpseDistance < sPlayerbotAIConfig.sightDistance;
    bool graveInSight = graveYardDistance < sPlayerbotAIConfig.sightDistance;
    bool enemiesNear = !AI_VALUE(std::list<ObjectGuid>, "possible targets").empty();

    if (enemiesNear)
    {
        if (graveInSight)
        {
            SET_AI_VALUE2(bool, "manual bool", "enemies near graveyard", true);
            return false;
        }
        if (corpseInSight && shortcutAllowed)
        {
            SET_AI_VALUE2(bool, "manual bool", "enemies near corpse", true);
            return true;
        }
    }

    //If grave is near and no ress sickness go there.
    if (shortcutAllowed && graveInSight && !corpseInSight && ai->HasCheat(BotCheatMask::repair))
        return true;

    //Stick to corpse.
    return false;
}