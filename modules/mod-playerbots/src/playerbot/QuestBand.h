#pragma once

#include "playerbot/TravelMgr.h"
#include "playerbot/QuestBandPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

// twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder, OB-00 D9 19.10.2026; S5A-DESIGN.md): the game side of the
// band step - the hub ladder store built once at boot (LoadLadder, fail closed), the band hub destination, the trip scope,
// the giver_done end and the band rescue. The pure rules are in QuestBandPolicy.h. Nothing here runs with BandLadder 0: the
// loader returns before any I/O, every entry point reads the switch first or a field only the band path writes.

namespace ai
{
    // A band hub of hub-ladder.tsv: one point (the hub's travel node position), never in TravelMgr's destinationMap, so no
    // fetch, purpose bit or level gate ever offers it (IsPossible false, fail closed). Its only user is the band step.
    class HubTravelDestination : public TravelDestination
    {
    public:
        explicit HubTravelDestination(uint32 rungIndex) : TravelDestination(), rung(rungIndex) {}

        uint32 GetRungIndex() const { return rung; }
        virtual bool IsBandHub() const override { return true; }
        virtual bool IsPossible(const PlayerTravelInfo& info) const override { return false; }
        // = ai::quest_band::TripScopeOk(bot, rung): the CheckStatus band block asks the same predicate.
        virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
        // Arrived within ArrivalRadius (50 yd) of the hub point on its map.
        virtual bool IsIn(const WorldPosition& pos, float radius = 0) const override;
        virtual std::string GetTitle() const override;
        virtual std::string GetShortName() const override { return "bandhub"; }

        void SetHubPoint(WorldPosition* point) { hubPoint = point; AddPoint(point); }
        WorldPosition* GetHubPoint() const { return hubPoint; }
    private:
        uint32 rung = 0;
        WorldPosition* hubPoint = nullptr;
    };

    namespace quest_band
    {
        // TravelTarget::EndBandTrip's argument for the arrival (the other ends pass a TripVerdict).
        constexpr std::uint8_t EndArrived = 0xFF;

        // Boot only (TravelMgr::LoadQuestTravelTable, world thread, after the travel nodes are final): reads the ladder file
        // behind the switch, fail closed; publishes the store last (release). Nothing with BandLadder 0.
        void LoadLadder();
        // The ladder was built and has at least one usable row (acquire).
        bool Built();
        // The usable rows (index = rung index) and their hub destinations; only valid while Built().
        Ladder const& GetLadder();
        HubTravelDestination* GetHub(uint32 rung);

        // The trip scope (O(1), no AI value): switch, cohort, InQuestBandScope (no group, no real master, below max level) and
        // the bot on the rung's map. detail (optional) = switch|master|group|max_level|map for skip reason=scope_lost.
        bool TripScopeOk(Player* bot, uint32 rung, char const** detail = nullptr);

        // OB-10 Q1 19.10.2026: a quest giver trip that turned inactive under the band giver filter because its quest is now in
        // the log or done. Reads the switch first; true = done (counted once as giver_done=, no line).
        bool GiverTripDoneFor(PlayerbotAI* ai, Player* bot, TravelDestination const* destination);

        // World thread (RandomPlayerbotMgr::ProcessQuestRescues): the hub position when the bot is on the hub's map, else the
        // plain rescue targets (result=fallback).
        std::vector<WorldLocation> HubRescueTargets(Player* bot, uint32 rung);
        // One band rescue line (result=teleport) for a band rescue teleport (world thread).
        void LogRescueTeleport(Player* bot, uint32 rung);

        // The head of every [QuestBand] line of this bot (cohort letter, from zone of the episode, hub id; 0 = none).
        LineHead HeadFor(Player* bot, uint32 fromZone, uint32 rung, bool hasRung);
    }
}
