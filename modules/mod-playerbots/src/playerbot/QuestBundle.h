#pragma once

#include "playerbot/strategy/values/TravelValues.h"

// A3 step 2 QB-B (AiPlayerbot.QuestBook.Bundle, default 0; OB-00 order 08.11.2026). Plan: ws-10/cli-quest-organic/a3/
// step2-questbook/QB-BUILD-PLAN-20261107.md, section 11. Pure rules: QuestBundlePolicy.h.
class Player;
class PlayerbotAI;

namespace ai
{
class PlayerTravelInfo;   // TravelMgr.h; WorldPosition and TravelDestination come with TravelValues.h
}

namespace ai::questbundle
{
// The async quest fetch (RequestQuestTravelTargetAction's job, off the bot thread), after the whole-map giver fallback: for a bot the
// switch applies to (PlayerTravelInfo::HasQuestBundle), the giver sites are scored (distinct quests within BundleRadius over route +
// accept time); with two sites or more their points go to the nearest partition in score order (after what it already holds, so
// turn-ins and objectives stay first). Points of every other purpose keep their place. Reads only the snapshot. Every other bot:
// nothing changes (returns at once).
void RankQuestBundles(PartitionedTravelList& list, PlayerTravelInfo const& info);

// SetBestTarget on the bot thread, right after a pick: one [QuestBundle] line when the pick is a quest giver and the switch applies
// to the bot (an active quest book in the Bundle cohort); the bundle is recounted from the choice list with the same rule.
// Otherwise nothing.
void NotePick(Player* bot, PlayerbotAI* ai, TravelDestination const* destination, WorldPosition const* position,
    PartitionedTravelList const& choices);
}
