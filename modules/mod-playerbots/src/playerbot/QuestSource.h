#pragma once

#include "playerbot/strategy/values/TravelValues.h"

// A3 step 2 QB-S (AiPlayerbot.QuestBook.SourceChoice, default 0; OB-00 07.11.2026). Plan: ws-10/cli-quest-organic/a3/
// step2-questbook/QB-BUILD-PLAN-20261107.md, section 7. Pure rules: QuestSourcePolicy.h.
class Player;
class PlayerbotAI;

namespace ai
{
class PlayerTravelInfo;   // TravelMgr.h; WorldPosition and TravelDestination come with TravelValues.h
}

namespace ai::questsource
{
// The async quest fetch (RequestQuestTravelTargetAction's job, off the bot thread), after the partitions are merged: for a bot the
// switch applies to (PlayerTravelInfo::HasQuestSourceChoice), the objective sources of each (quest, objective) are priced (route +
// expected work); the chosen one of each group goes to the nearest partition in XP-per-minute order (after what is already there,
// so turn-ins stay first), the others go to the farthest partition (a fallback when the chosen one fails IsActive). Points that are
// no quest objective, or of a quest without a snapshot, stay where they are. Reads only the snapshot and the load-time source facts.
// Every other bot: nothing changes (returns at once).
void RankQuestSources(PartitionedTravelList& list, PlayerTravelInfo const& info);

// SetBestTarget on the bot thread, right after a pick: one [QuestSource] line when the pick is a quest objective and the switch
// applies to the bot (an active quest book in the SourceChoice cohort). Otherwise nothing.
void NotePick(Player* bot, PlayerbotAI* ai, TravelDestination const* destination, WorldPosition const* position);
}
