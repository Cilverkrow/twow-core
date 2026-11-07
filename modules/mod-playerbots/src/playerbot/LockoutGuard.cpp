#include "playerbot/playerbot.h"
#include "playerbot/LockoutGuard.h"
#include "playerbot/LockoutPolicy.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Maps/MapPersistentStateMgr.h"

#include <array>
#include <atomic>

// L1 (owner 07.11.2026): see LockoutGuard.h. The counters are relaxed atomics: incremented on bot, map and world threads,
// taken once a minute on the world thread.
namespace ai::lockout_guard
{
namespace
{
std::array<std::atomic<std::uint32_t>, kCounters> counters{};

std::uint32_t Take(Counter c)
{
    return counters[c].exchange(0, std::memory_order_relaxed);
}

// The bot's own bind on mapId (its own data; no group or foreign bind).
lockout::Bind OwnBind(Player* bot, std::uint32_t mapId)
{
    lockout::Bind bind;
    if (InstancePlayerBind const* pBind = bot->GetBoundInstance(mapId))
    {
        if (pBind->state)
        {
            bind.instanceId = pBind->state->GetInstanceId();
            bind.perm = pBind->perm;
        }
    }
    return bind;
}
}

void Count(Counter c)
{
    if (c < kCounters)
        counters[c].fetch_add(1, std::memory_order_relaxed);
}

void CountJoin(bool reset, bool inviterReal)
{
    Count(!reset ? KeepBot : (inviterReal ? ResetReal : ResetBot));
}

bool SplitToward(Player* bot, std::uint32_t mapId, std::uint32_t targetInstanceId)
{
    if (!bot)
        return false;
    MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(mapId);
    if (!entry || !entry->IsDungeon())
        return false;
    return lockout::LockSplit(OwnBind(bot, mapId), targetInstanceId);
}

bool RelaySplit(Player* bot, std::uint32_t triggerId, Player* target)
{
    if (!bot || !target || target == bot)
        return false;
    AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(triggerId);
    if (!at)
        return false;
    std::uint32_t const mapId = at->destination.mapId;
    if (target->GetMapId() != mapId)   // only toward a player who stands on M (scalar read)
        return false;
    return SplitToward(bot, mapId, target->GetInstanceId());
}

bool RelayWait(Player* bot, std::uint32_t triggerId, Player* target)
{
    if (!bot || !target || target == bot || !target->IsBeingTeleportedFar())
        return false;
    AreaTriggerTeleport const* at = sObjectMgr.GetAreaTriggerTeleport(triggerId);
    if (!at)
        return false;
    std::uint32_t const mapId = at->destination.mapId;
    // On its way to M, not yet on M (scalar reads; the ack sets the map before it clears the far-teleport flag).
    if (target->GetTeleportDest().mapId != mapId || target->GetMapId() == mapId)
        return false;
    MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(mapId);
    if (!entry || !entry->IsDungeon())
        return false;
    lockout::Bind const own = OwnBind(bot, mapId);
    return own.instanceId && own.perm;
}

void MinuteTick(std::uint32_t diff)
{
    static std::uint32_t elapsedMs = 0;   // world thread only
    elapsedMs += diff;
    if (elapsedMs < 60000)
        return;
    elapsedMs = 0;

    lockout::Counts c;
    c.resetReal = Take(ResetReal);
    c.resetBot = Take(ResetBot);
    c.keepBot = Take(KeepBot);
    c.splitDecline = Take(SplitDecline);
    c.splitEntry = Take(SplitEntry);
    c.leaderCarryReal = Take(LeaderCarryReal);
    c.leaderCarryBot = Take(LeaderCarryBot);
    if (!c.Any())   // a missing minute means every field was 0
        return;
    sLog.outBasic("%s", lockout::MinuteLine(sPlayerbotAIConfig.botGroupsResetRaidsRealInviterOnly, c).c_str());
}
}
