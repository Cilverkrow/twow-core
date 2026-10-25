/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcAutoRun.h"

#include <atomic>
#include <ctime>
#include <string>

#include "Group.h"
#include "Map.h"
#include "Player.h"

#include "Playerbots.h"
#include "PlayerbotAI.h"
#include "playerbot/DungeonRunBridge.h"

#include "DcStrategyGate.h"
#include "Ai/Dungeon/DungeonClear/Util/DcLeaderSignal.h"
#include "Ai/Dungeon/DungeonClear/Util/DcRun.h"

namespace
{
    std::atomic<bool> g_enabled{ false };

    char const* const kNonCombat = "dungeon clear";
    char const* const kCombat = "dungeon clear combat";

    template <typename F>
    void ForMembers(Player* bot, F&& f)
    {
        if (Group* group = bot ? bot->GetGroup() : nullptr)
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->getSource())
                    f(member);
    }

    ai::dungeon_run::Start StartHook(Player* any, std::string* why)
    {
        using ai::dungeon_run::Start;
        auto refuse = [why](Start s, char const* text) { if (why) *why = text; return s; };
        if (!g_enabled.load())
            return refuse(Start::Off, "DungeonClear.AutoRun = 0");
        if (!any || !DcAutoRun::BotOnlyGroup(any))
            return refuse(Start::Off, "not a bot-only group");

        // every member on the same dungeon map, none mid-teleport (the manager asks again on its next pass)
        Map* map = any->FindMap();
        if (!map || !map->IsDungeon() || any->IsBeingTeleported())
            return refuse(Start::Pending, "not in the dungeon yet");
        bool together = true;
        ForMembers(any, [&](Player* m) { together = together && !m->IsBeingTeleported() && m->FindMap() == map; });
        if (!together)
            return refuse(Start::Pending, "members still zoning");

        Player* leader = DcLeaderSignal::FindLeaderTank(any);
        PlayerbotAI* leaderAI = leader ? GET_PLAYERBOT_AI(leader) : nullptr;
        if (!leaderAI)
            return refuse(Start::Refused, "no living tank bot to lead");
        if (DcRun::Of(leaderAI).enabled)
            return Start::Running;

        // the strategies first, on the members' own map threads (gate); `dc on` below then writes values only
        bool installed = true;
        ForMembers(any, [&](Player* m) {
            DcStrategyGate::RequestAutoRun(m->GetObjectGuid());
            PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
            installed = installed && ai && ai->HasStrategy(kNonCombat, BOT_STATE_NON_COMBAT) &&
                        ai->HasStrategy(kCombat, BOT_STATE_COMBAT);
        });
        if (!installed)
            return refuse(Start::Pending, "DC strategies being installed");

        leaderAI->DoSpecificAction("dc on", Event(DcAutoRun::Source, "", nullptr), true);
        if (DcRun::Of(leaderAI).enabled)
            return Start::Running;
        return refuse(Start::Refused, "dc on did not take (see 'DC command refused' in the log)");
    }

    void StopHook(Player* any, char const* why)
    {
        if (!g_enabled.load() || !any)
            return;
        if (Player* leader = DcLeaderSignal::FindLeaderTank(any))
            if (PlayerbotAI* leaderAI = GET_PLAYERBOT_AI(leader))
                if (DcRun::Of(leaderAI).enabled)
                    leaderAI->DoSpecificAction("dc off", Event(DcAutoRun::Source, why ? why : "", nullptr), true);
        ForMembers(any, [](Player* m) { DcStrategyGate::ClearAutoRun(m->GetObjectGuid()); });
    }
}

namespace DcAutoRun
{
    void SetEnabled(bool on)
    {
        g_enabled.store(on);
        ai::dungeon_run::Hooks& hooks = ai::dungeon_run::Registered();
        hooks.start = on ? &StartHook : nullptr;
        hooks.stop = on ? &StopHook : nullptr;
    }

    bool Enabled() { return g_enabled.load(); }

    bool BotOnlyGroup(Player* bot)
    {
        if (!bot || !bot->GetGroup())
            return false;
        bool botsOnly = true;
        ForMembers(bot, [&](Player* m) {
            PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
            botsOnly = botsOnly && ai && !ai->IsRealPlayer();
        });
        return botsOnly;
    }

    void OnRunEnded(PlayerbotAI* leaderAI, Player* leader, char const* reason)
    {
        if (!g_enabled.load() || !leaderAI || !leader || !BotOnlyGroup(leader))
            return;
        ai::dungeon_run::Mailbox().Note(leader->GetGUIDLow(), reason ? reason : "", uint32(time(nullptr)));
        ForMembers(leader, [](Player* m) { DcStrategyGate::ClearAutoRun(m->GetObjectGuid()); });
    }
}
