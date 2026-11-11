#pragma once

#include "playerbot/ManagerInboxPolicy.h"
#include "playerbot/GroupGatherPolicy.h"   // T33-0: PlaceFacts

class Player;
class PlayerbotAI;

// A3 step 6 T30-0 (design T30 v1.1 3.1; AiPlayerbot.BotGroups.Manager.StatusSeconds, default 0; measurement only): the facts a
// bot posts to the group manager's inbox, read on the bot's own map thread from its own state only - its own flags, its own
// objective, its own group (id, leader guid, size; never the member list or another Player*, M6) and its own quest log. The
// posts themselves are PlayerbotAI::PostManagerStatus (the one status hook).
namespace ai::manager_facts
{
// Status flags (alliance, alive, candidate, BG queue, instance map, busy, start-zone hold).
manager_inbox::StatusFlags ReadFlags(Player* bot, PlayerbotAI* ai);
// The objective key: the active travel target's quest objective (adhoc_group::CurrentObjective), its status, done flag and
// counter. T30-1 adds the key of a manager group (ManagedKeyOf).
manager_inbox::KeyFacts ReadKey(Player* bot);
// The bot's own group as it sees it.
manager_inbox::EdgeFacts ReadEdge(Player* bot);
// The interim GroupWish (F3 (a)): the S7 scan predicates for this bot alone; false = no wish (grouped, busy, nothing waits).
bool ReadWish(Player* bot, PlayerbotAI* ai, manager_inbox::WishFacts& out);
// T33-0 (design T33 v1.1 3.2, measurement only): the bot's own place - map, raw instance id, instanceable, x/y/z. The serial
// stays 0 (no directive record on the bot before the T02-GROUP handler).
group_mgr::PlaceFacts ReadPlace(Player* bot);
}
