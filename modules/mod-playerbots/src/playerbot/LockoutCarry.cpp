#include "playerbot/playerbot.h"
#include "playerbot/LockoutCarry.h"
#include "playerbot/LockoutGuard.h"
#include "playerbot/LockoutPolicy.h"
#include "playerbot/BotSlots.h"
#include "Group/Group.h"
#include "Maps/MapPersistentStateMgr.h"

#include <mutex>
#include <unordered_map>

// leader_bind_carry (OB-00 07.11.2026): see LockoutCarry.h. Count only: nothing here changes a bind, a group, a teleport
// or a packet.
namespace ai::lockout_carry
{
namespace
{
// The player's own bind on its far teleport's dungeon map, kept until its next worldport ack or its logout.
struct Note
{
    std::uint32_t mapId = 0;
    lockout::Bind before;
};

std::mutex notesLock;
std::unordered_map<std::uint32_t, Note> notes;   // low guid -> note

bool IsDungeonMap(std::uint32_t mapId)
{
    MapEntry const* entry = sMapStorage.LookupEntry<MapEntry>(mapId);
    return entry && entry->IsDungeon();
}

lockout::Bind PlayerBindOn(Player* player, std::uint32_t mapId)
{
    lockout::Bind bind;
    if (InstancePlayerBind const* pBind = player->GetBoundInstance(mapId))
    {
        if (pBind->state)
        {
            bind.instanceId = pBind->state->GetInstanceId();
            bind.perm = pBind->perm;
        }
    }
    return bind;
}

lockout::Bind GroupBindOn(Group* group, std::uint32_t mapId)
{
    lockout::Bind bind;
    if (InstanceGroupBind const* gBind = group->GetBoundInstance(mapId))
    {
        if (gBind->state)
        {
            bind.instanceId = gBind->state->GetInstanceId();
            bind.perm = gBind->perm;
        }
    }
    return bind;
}
}

void NoteTeleport(Player* player, std::uint32_t mapId)
{
    if (!player || mapId == player->GetMapId())   // a near teleport: no map entry follows
        return;
    std::uint32_t const guid = player->GetGUIDLow();
    if (!IsDungeonMap(mapId))
    {
        std::lock_guard<std::mutex> lock(notesLock);
        notes.erase(guid);
        return;
    }
    Note note;
    note.mapId = mapId;
    note.before = PlayerBindOn(player, mapId);   // the player's own bind, on the thread that teleports it
    std::lock_guard<std::mutex> lock(notesLock);
    notes[guid] = note;
}

void OnEntry(Player* player)
{
    if (!player)
        return;
    Note note;
    {
        std::lock_guard<std::mutex> lock(notesLock);
        auto const it = notes.find(player->GetGUIDLow());
        if (it == notes.end())
            return;   // no far teleport noted: not counted
        note = it->second;
        notes.erase(it);
    }
    std::uint32_t const mapId = player->GetMapId();
    if (note.mapId != mapId || !IsDungeonMap(mapId))
        return;

    // The member's own binds first (its own data, on the thread of its ack).
    lockout::CarryEntry e;
    e.entered = player->GetInstanceId();
    e.selfBefore = note.before;
    e.selfAfter = PlayerBindOn(player, mapId);
    if (!lockout::EntryPermFromGroup(e.selfBefore, e.selfAfter, e.entered))
        return;

    if (!World::IsWorldThread())
        return;
    // World thread only from here (a far ack there runs with every map idle): the group's bind and the leader's own bind.
    Group* group = player->GetGroup();
    if (!group)
        return;
    ObjectGuid const leaderGuid = group->GetLeaderGuid();
    e.memberIsLeader = leaderGuid == player->GetObjectGuid();
    Player* leader = e.memberIsLeader ? player : sObjectMgr.GetPlayer(leaderGuid);
    if (!leader)
        return;   // leader not online: not classified, not counted
    e.leaderIsBot = !::IsRealPlayer(leader);
    e.leaderOnM = PlayerBindOn(leader, mapId);
    e.groupOnM = GroupBindOn(group, mapId);
    if (lockout::LeaderBindCarry(e))
        lockout_guard::Count(::IsRealPlayer(player) ? lockout_guard::LeaderCarryReal : lockout_guard::LeaderCarryBot);
}

void Forget(Player* player)
{
    if (!player)
        return;
    std::lock_guard<std::mutex> lock(notesLock);
    notes.erase(player->GetGUIDLow());
}
}
