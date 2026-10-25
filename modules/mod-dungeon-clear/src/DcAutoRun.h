/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_AUTO_RUN_H
#define _DC_AUTO_RUN_H

class Player;
class PlayerbotAI;

// twow-repo#629 B4/D2 (CLI-GRP 25.10.2026, DungeonClear.AutoRun, default 0): dungeon clear for a group a playerbots manager
// formed - all members bots, no human, GM or self-bot owner who could type `dc on`. The manager reaches it through
// playerbot/DungeonRunBridge.h (hooks registered here at config load), never through DC symbols.
//   start: marks the members for the strategy gate (installed on their own map threads), then `dc on` on the leader tank
//          with the event source "dc auto" once every member carries both DC strategies - values only, no ChangeStrategy
//          from the world thread (the .dc test crash notes in DcTestRunJob.cpp).
//   stop:  `dc off` (source "dc auto") on the run's leader, marks cleared.
// "dc auto" is accepted by the dc commands' authorization only with AutoRun = 1 and a bot-only group. Every run end
// (all cleared, a wipe, dc off) is posted to the bridge mailbox and clears the marks. AutoRun 0: nothing registered,
// nothing accepted, no behaviour change.
namespace DcAutoRun
{
    char const* const Source = "dc auto";

    void SetEnabled(bool on);   // from OnAfterConfigLoad (also (un)registers the bridge hooks)
    bool Enabled();

    // Every group member has a bot AI and none is a real player (a self-bot is one): nobody could own `dc on`.
    bool BotOnlyGroup(Player* bot);

    // DisableDungeonClear: post the end, clear the members' gate marks.
    void OnRunEnded(PlayerbotAI* leaderAI, Player* leader, char const* reason);
}

#endif  // _DC_AUTO_RUN_H
