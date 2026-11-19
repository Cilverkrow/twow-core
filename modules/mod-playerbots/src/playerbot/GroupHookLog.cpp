#include "playerbot/playerbot.h"
#include "playerbot/GroupHookLog.h"
#include "playerbot/GroupHookLine.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Group/Group.h"
#include "ObjectMgr.h"
#include "ScriptObjects.h"
#include "ScriptMgr.h"

#include <atomic>
#include <cstdint>
#include <string>

// T30 v1.1 V2 (OB-00 19.11.2026): the [GroupHook] lines, one per group membership change (format and semantics:
// GroupHookLine.h). The core's group hooks (src/game/Group/Group.cpp) fire on the thread that changes the group (world or a map
// thread). The observer reads only the group's own fields there - ids, member slots, leader guid - and never resolves a player
// (no player lookup, no player pointer), and writes with sLog.outBasic directly: the deferred log would drop lines (per-bot cap),
// write them only at the bot's next UpdateAI (lost on logout) with the write time, and route the disband lines of all members to
// one bot.
// sPlayerbotAIConfig.botGroupsDiagnostics is read on map threads too (a plain bool set at config load, as for the other group
// observers); the snapshot runs only when Diagnostics = 1 on the first world tick - a later reload to 1 writes no snapshot.
namespace ai::group_hook
{
namespace
{
std::atomic<std::uint64_t> gSeq{0};

std::uint64_t NextSeq()
{
    return gSeq.fetch_add(1, std::memory_order_relaxed) + 1;
}

void Write(char const* ev, std::uint32_t bot, Group const* group, std::uint32_t members, std::uint32_t other, std::string const& cause)
{
    std::string const line = FormatLine(ev, NextSeq(), bot, group->GetId(), members, group->GetLeaderGuid().GetCounter(), other,
        cause.c_str());
    sLog.outBasic("%s", line.c_str());
}

class GroupHookObserver : public GroupScript
{
public:
    GroupHookObserver() : GroupScript("PlayerbotGroupHookObserver") {}

    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        if (!sPlayerbotAIConfig.botGroupsDiagnostics || !group || group->isBGGroup())
            return;
        Write("add", guid.GetCounter(), group, group->GetMembersCount(), 0, "add");
    }
    void OnRemoveMember(Group* group, ObjectGuid guid, uint8 method) override
    {
        if (!sPlayerbotAIConfig.botGroupsDiagnostics || !group || group->isBGGroup())
            return;
        Write("remove", guid.GetCounter(), group, group->GetMembersCount(), 0, RemoveCause(method));
    }
    void OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid) override
    {
        if (!sPlayerbotAIConfig.botGroupsDiagnostics || !group || group->isBGGroup())
            return;
        Write("leader", newLeaderGuid.GetCounter(), group, group->GetMembersCount(), oldLeaderGuid.GetCounter(), "change");
    }
    void OnDisband(Group* group) override
    {
        if (!sPlayerbotAIConfig.botGroupsDiagnostics || !group || group->isBGGroup())
            return;
        // Group::Disband calls the hook before it clears the slots: every member still in the group leaves here.
        std::uint32_t const members = group->GetMembersCount();
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            Write("remove", slot.guid.GetCounter(), group, members, 0, "disband");
    }
};

bool gSnapshotDone = false;   // world thread only
}

void AddGroupHookObserver()
{
    new GroupHookObserver();
}

void SnapshotOnce()
{
    if (gSnapshotDone)
        return;
    gSnapshotDone = true;
    if (!sPlayerbotAIConfig.botGroupsDiagnostics)
        return;
    // World thread, all maps idle (PlayerbotWorldScript::OnUpdate runs after sMapMgr.Update): the group map is not changing.
    for (auto itr = sObjectMgr.GetGroupMapBegin(); itr != sObjectMgr.GetGroupMapEnd(); ++itr)
    {
        Group const* group = itr->second;
        if (!group || group->isBGGroup())
            continue;
        std::uint32_t const members = group->GetMembersCount();
        for (Group::MemberSlot const& slot : group->GetMemberSlots())
            Write("snapshot", slot.guid.GetCounter(), group, members, 0, "startup");
    }
}
}
