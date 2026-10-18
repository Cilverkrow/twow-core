#pragma once

#include "playerbot/DirectTargetPolicy.h"
#include "playerbot/TravelMgr.h"

#include <atomic>
#include <ctime>
#include <string>

class SpellEntry;

namespace ai
{
    // twow-repo#594 / P3-b: what the direct-target destinations share - their class, and the failure memory of every
    // bot (relaxed atomics: the destination is shared by all map threads). Never Grind or Boss: no kill trip.
    class DirectTargetTag
    {
    public:
        explicit DirectTargetTag(direct_targets::Kind kind) : kind(kind) {}
        virtual ~DirectTargetTag() = default;

        direct_targets::Kind GetKind() const { return kind; }
        bool IsDisabled() const
        {
            return direct_targets::CooldownRunning(uint32(time(nullptr)), disabledUntil.load(std::memory_order_relaxed));
        }
        uint32 GetFailures() const { return failures.load(std::memory_order_relaxed); }
        uint32 GetArrivals() const { return arrivals.load(std::memory_order_relaxed); }
        uint32 NoteFailure() { return failures.fetch_add(1, std::memory_order_relaxed) + 1; }
        uint32 NoteArrival() { return arrivals.fetch_add(1, std::memory_order_relaxed) + 1; }
        // True for the one call that disabled it (for DisableSeconds); the counts start again from 0.
        bool Disable(uint32 now)
        {
            uint32 until = disabledUntil.load(std::memory_order_relaxed);
            if (direct_targets::CooldownRunning(now, until) ||
                !disabledUntil.compare_exchange_strong(until, direct_targets::DisabledUntil(now), std::memory_order_relaxed))
                return false;
            failures.store(0, std::memory_order_relaxed);
            arrivals.store(0, std::memory_order_relaxed);
            return true;
        }
        uint8 GetTeamMask() const { return teamMask; }
        void SetTeamMask(uint8 mask) { teamMask = mask; }
    private:
        direct_targets::Kind kind;
        std::atomic<uint32> failures{ 0 };
        std::atomic<uint32> arrivals{ 0 };
        std::atomic<uint32> disabledUntil{ 0 };
        uint8 teamMask = direct_targets::TeamBoth;
    };

    // An anvil / forge (Station) or a cooking fire (Cooking) of one zone; the key carries focus, zone and team.
    class StationTravelDestination : public EntryTravelDestination, public DirectTargetTag
    {
    public:
        StationTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry);

        uint32 GetFocusId() const { return direct_targets::StationFocusOf(GetEntry()); }
        uint32 GetZoneId() const { return direct_targets::StationZoneOf(GetEntry()); }

        virtual bool IsPossible(const PlayerTravelInfo& info) const override;
        virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
        virtual bool IsIn(const WorldPosition& pos, float radius = 0) const override
        {
            return OnMap(pos) && DistanceTo(pos) <= (radius ? radius : direct_targets::ArrivalRadius(direct_targets::StationRadius));
        }
        virtual std::string GetTitle() const override;
        virtual std::string GetShortName() const override { return "station"; }
    };

    // Shore points facing the fishing holes of one area (GatherFishing: FishAction acts on that purpose only).
    class ShoreFishTravelDestination : public GatherTravelDestination, public DirectTargetTag
    {
    public:
        ShoreFishTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry)
            : GatherTravelDestination(purpose, 0, entry), DirectTargetTag(direct_targets::Kind::Fishing) {}

        uint32 GetAreaId() const { return direct_targets::ShoreFishAreaOf(GetEntry()); }
        int32 GetReqSkill() const { return reqSkill; }
        void SetReqSkill(int32 skill) { reqSkill = skill; }

        virtual bool IsPossible(const PlayerTravelInfo& info) const override;
        virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
    private:
        int32 reqSkill = 0;
    };

    // The open-world approach point of a dungeon / raid entrance; bots do not zone in (no dungeon logic yet).
    class EntranceTravelDestination : public EntryTravelDestination, public DirectTargetTag
    {
    public:
        EntranceTravelDestination(TravelDestinationPurpose purpose, uint32 /*id*/, int32 entry);

        uint32 GetInstanceMapId() const { return direct_targets::EntranceMapOf(GetEntry()); }
        void SetLevels(uint32 minLevel, uint32 maxLevel, uint32 required, uint32 maxPlayer)
        {
            levelMin = minLevel; levelMax = maxLevel; requiredLevel = required; maxPlayerLevel = maxPlayer;
        }
        void SetRequiredPhase(uint32 phase) { requiredPhase = phase; }
        uint32 GetLevelMin() const { return levelMin; }
        uint32 GetLevelMax() const { return levelMax; }
        uint32 GetRequiredLevel() const { return requiredLevel; }

        virtual bool IsPossible(const PlayerTravelInfo& info) const override;
        virtual bool IsActive(Player* bot, const PlayerTravelInfo& info) const override;
        virtual bool IsIn(const WorldPosition& pos, float radius = 0) const override
        {
            return OnMap(pos) && DistanceTo(pos) <= (radius ? radius : direct_targets::ArrivalRadius(direct_targets::EntranceRadius));
        }
        virtual std::string GetTitle() const override;
        virtual std::string GetShortName() const override { return "entrance"; }
    private:
        uint32 levelMin = 0, levelMax = 0, requiredLevel = 0, maxPlayerLevel = 60, requiredPhase = 0;
    };

    namespace direct_targets
    {
        // Master and class switches as they are now (a reload changes them; the built flags only at boot).
        uint32 ConfiguredMask();
        bool IsLive(Kind kind);
        // The TravelStrategy row of a request purpose (Station also serves Cooking), at strategy init.
        bool PurposeLive(TravelDestinationPurpose purpose);
        // The "station craft" node of GatherStrategy.
        bool StationCraftLive();

        DirectTargetTag* TagOf(TravelDestination const* destination);

        // NeedTravelPurposeValue for Station / DungeonEntrance / RaidEntrance.
        bool NeedPurpose(PlayerbotAI* ai, TravelDestinationPurpose purpose);
        // The focus mask (FocusBit) of the station recipes this bot could skill up with now, cached NeedCacheSeconds.
        uint32 StationRecipeMask(PlayerbotAI* ai);
        // A focus recipe the bot can cast now for a skill-up: tools, reagents, and not backed off by the rule of the
        // "profession craft" trigger (failed spell until, spell and category cooldown, room for the product).
        bool StationRecipeUsable(PlayerbotAI* ai, SpellEntry const* spell, time_t now);

        // The dungeon level ranges loaded at boot (for the need: a level inside one of them).
        void NoteDungeonLevels(uint32 minLevel, uint32 maxLevel);
        bool DungeonLevelCovered(uint32 level);

        // Runtime hooks (map thread of the bot); every call site sits behind AnyBuilt().
        void NoteEmpty(PlayerbotAI* ai, std::string const& purpose);
        void OnChoice(PlayerbotAI* ai, TravelTarget& target);
        void OnArrive(TravelDestination* destination, PlayerbotAI* ai, WorldPosition const* position);
        void OnTravelFailure(PlayerbotAI* ai, TravelDestination* destination, char const* reason);
        // The end of a trip wherever the travel target leaves its destination: another destination (TravelTarget::SetTarget,
        // also the null target of a reset) or a status that ends it (cooldown, expired, none).
        void OnTargetChange(PlayerbotAI* ai, TravelDestination* from, TravelDestination* to);
        void OnTargetStatus(PlayerbotAI* ai, TravelDestination* destination, TravelStatus status);
        void OnStationCraft(PlayerbotAI* ai, TravelDestination* destination);
        // Direct-target trips are not announced in chat, like gathering trips (#333).
        bool IsSilentTrip(TravelDestination const* destination);
    }
}
