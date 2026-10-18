#include "DirectTargetPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// twow-repo#594 / P3-b: the pure rules of the direct-target destinations (DirectTargetPolicy.h).

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::direct_targets;

    // A) switches: nothing without the master, one bit per class.
    Require(SwitchMask(false, true, true, true, true, true) == 0, "no class without the master switch");
    Require(SwitchMask(true, false, false, false, false, false) == 0, "master alone builds nothing");
    Require(SwitchMask(true, true, false, true, false, true) == (Bit(Kind::Station) | Bit(Kind::Fishing) | Bit(Kind::Raid)),
            "one bit per class");
    Require(std::string(KindName(Kind::Cooking)) == "cooking" && std::string(KindName(Kind::Raid)) == "raid", "class names");

    // B) boot only: the first Initialize builds, a reload names a class it cannot build, once.
    Require(!AnyBuilt(), "nothing built before the loader");
    Require(!Live(SwitchMask(true, true, true, true, true, true), Kind::Station), "not live while not built");
    Require(NoteInitialize() == 1 && IsBootLoad(InitializeCalls().load()), "the boot call is the first one");
    Require(NoteReloadSwitches(SwitchMask(true, true, false, false, false, false), 1) == 0, "the boot reports nothing");
    Require(TakeSummaryFields().empty(), "no summary fields without a built class");
    SetBuilt(Kind::Station);
    Require(AnyBuilt(), "built after the loader");
    Require(Live(SwitchMask(true, true, false, false, false, false), Kind::Station), "built and on = live");
    Require(!Live(SwitchMask(true, false, false, false, false, false), Kind::Station), "a reload can switch a built class off");
    Require(NoteInitialize() == 2 && !IsBootLoad(InitializeCalls().load()), "a reload is not the boot");
    Require(NoteReloadSwitches(SwitchMask(true, true, true, false, false, false), 2) == Bit(Kind::Cooking),
            "a reload that switches on an unbuilt class names it");
    Require(NoteReloadSwitches(SwitchMask(true, true, true, false, false, false), 3) == 0, "named once");
    Require(!Live(SwitchMask(true, true, true, false, false, false), Kind::Cooking), "never live without the boot build");

    // C) keys: at or below -1e9, decodable, no legacy fish key (positive zone ids) collides.
    Require(StationKey(FocusAnvil, 1519) == -(1000000000 + 100000 + 1519), "station key = the design key (both teams)");
    Require(FamilyOf(StationKey(FocusForge, 12)) == Family::Station && StationFocusOf(StationKey(FocusForge, 12)) == FocusForge &&
            StationZoneOf(StationKey(FocusForge, 12)) == 12, "station key decodes");
    Require(StationTeamOf(StationKey(FocusCooking, 1637, TeamHorde)) == TeamHorde &&
            StationZoneOf(StationKey(FocusCooking, 1637, TeamHorde)) == 1637 &&
            StationFocusOf(StationKey(FocusCooking, 1637, TeamHorde)) == FocusCooking, "a one-team station key keeps focus and zone");
    Require(StationKey(FocusAnvil, 1519, TeamAlliance) != StationKey(FocusAnvil, 1519), "one-team points get their own destination");
    Require(StationTeamOf(StationKey(FocusAnvil, 1519)) == TeamBoth, "both teams by default");
    Require(FamilyOf(ShoreFishKey(331)) == Family::ShoreFish && ShoreFishAreaOf(ShoreFishKey(331)) == 331, "shore fish key");
    Require(FamilyOf(EntranceKey(429)) == Family::Entrance && EntranceMapOf(EntranceKey(429)) == 429, "entrance key");
    Require(!IsDirectKey(-7000035) && !IsDirectKey(1519) && !IsDirectKey(0), "GO entries, legacy fish zones and 0 are no keys");
    Require(FamilyOf(-999999999) == Family::None, "just above -1e9 is no key");
    Require(StationZoneFits(19999) && !StationZoneFits(20000), "zone ids from 20000 would wrap into the team slot");
    Require(StationZoneOf(StationKey(FocusAnvil, 19999, TeamHorde)) == 19999 && StationTeamOf(StationKey(FocusAnvil, 19999, TeamHorde)) == TeamHorde,
            "the largest zone id keeps its team");

    // D) snap classes and the open-edge rule.
    Require(ClassifySnap(true, 4.9f, -2.9f, true, 4.9f, -2.9f) == SnapClass::Ok, "snap_ok within 5 / 3");
    Require(ClassifySnap(false, 0.0f, 0.0f, true, 12.0f, 4.0f) == SnapClass::Far, "snap_far within 15 / 5");
    Require(ClassifySnap(false, 0.0f, 0.0f, true, 15.5f, 1.0f) == SnapClass::Drop, "beyond 15 yd: drop");
    Require(ClassifySnap(false, 0.0f, 0.0f, true, 10.0f, 5.5f) == SnapClass::Drop, "beyond 5 dz: drop");
    Require(ClassifySnap(false, 0.0f, 0.0f, false, 0.0f, 0.0f) == SnapClass::Drop, "nothing: drop");
    Require(!ComponentLarge(199, MainComponentPolys, false) && ComponentLarge(200, MainComponentPolys, false), "200-poly rule");
    Require(ComponentLarge(12, EntranceComponentPolys, true), "an open external edge counts as large");

    // E) station focus / trap / radius (radiusMin 2.0 + 0.5 margin) and the per-dist table.
    Require(FocusRange(12) == 10.0f && FocusRange(8) == 8.0f, "focus search capped at 10 yd");
    Require(FocusInRange(5.5f, 8) && !FocusInRange(5.6f, 8), "3D + 2.5 <= dist");
    Require(TrapClear(3.5f, 1.0f) && !TrapClear(3.4f, 1.0f), "2D >= trap + 2.5");
    Require(TrapClear(0.0f, 0.0f), "no trap, no rule");
    Band band = StationBand(8, 1.0f);
    Require(band.feasible && band.low == 3.5f && band.high == 5.5f && band.Middle() == 4.5f, "dist 8 / trap 1: band 3.5 - 5.5");
    Require(StationBand(10, 1.0f).feasible && StationBand(10, 2.0f).feasible, "dist 10 / trap 1 and 2: feasible");
    Require(!StationBand(10, 10.0f).feasible, "dist 10 / trap 10: impossible, dropped");
    band = StationBand(5, 0.0f);
    Require(band.feasible && band.high == 2.5f, "dist 5 without trap: within 2.5 yd");
    Require(DistBucket(5) == 0 && DistBucket(8) == 1 && DistBucket(10) == 2 && DistBucket(12) == 3, "dist buckets");
    Require(ArrivalRadius(StationRadius) == 2.5f && ArrivalRadius(EntranceRadius) == 3.5f, "arrival beyond the final move offset");

    // F) fishing: band [min + r, max - r], chance floor +45 (50 %).
    band = FishingBand(10.0f, 20.0f, 3.5f);
    Require(band.feasible && band.low == 13.5f && band.high == 16.5f, "band 13.5 - 16.5 for r = 3.5");
    Require(InBand(14.0f, band) && InBand(16.0f, band) && !InBand(17.0f, band) && !InBand(13.0f, band), "rings inside the band");
    Require(FishingChance(100, 55) == 50 && FishingChance(1, 300) == 0 && FishingChance(300, 1) == 100, "core chance, clamped");
    Require(FishingSkillOk(100, 55, 150) && !FishingSkillOk(99, 55, 150), "skill >= zone skill + 45");
    Require(!FishingSkillOk(150, 55, 150), "no skill-up at the cap");
    Require(!FishingSkillOk(100, 0, 150) && !FishingSkillOk(0, -10, 150), "no fishable zone, no skill");
    Require(FishingSkillOk(25, -20, 75) && !FishingSkillOk(24, -20, 75) && FishingChance(25, -20) == 50, "a -20 coast: 50 % from skill 25");
    Require(FishingSkillOk(1, -70, 75) && FishingChance(1, -70) == 76, "a -70 coast: every fisher");
    Require(ShoreDzOk(-1.0f) && ShoreDzOk(8.0f) && !ShoreDzOk(-1.1f) && !ShoreDzOk(8.1f), "shore dz within -1 .. +8");
    Require(ShoreBetter(15.0f, 3.0f, 14.0f, 0.0f), "closest to 15 yd first");
    Require(ShoreBetter(14.0f, 0.5f, 16.0f, 2.0f), "equal |d - 15|: smaller dz");

    // G) entrances: outside the trigger + 5 + 3, order large > outside > ok > far > distance.
    Require(EntranceExcludeMargin() == 8.0f, "core margin 5 + radiusMin 3");
    Require(EntranceLimit(0.0f) == 20.0f && EntranceLimit(10.0f) == 28.0f, "the outer ring stays reachable");
    EntranceProbe small{ false, true, SnapClass::Ok, 5.0f };
    EntranceProbe large{ true, true, SnapClass::Far, 25.0f };
    EntranceProbe inside{ true, false, SnapClass::Ok, 1.0f };
    EntranceProbe nearOk{ true, true, SnapClass::Ok, 30.0f };
    EntranceProbe nearer{ true, true, SnapClass::Ok, 12.0f };
    Require(EntranceBetter(large, small) && !EntranceBetter(small, large), "component >= 2000 first");
    Require(EntranceBetter(large, inside), "outside the margin before inside");
    Require(EntranceBetter(nearOk, large), "snap_ok before snap_far");
    Require(EntranceBetter(nearer, nearOk), "then the distance");

    // H) level fit and the level source names.
    Require(DungeonLevelFit(18, 15, 25, 10) && !DungeonLevelFit(14, 15, 25, 10) && !DungeonLevelFit(26, 15, 25, 10), "dungeon range");
    Require(!DungeonLevelFit(17, 15, 25, 18), "the required level raises the floor");
    Require(RaidLevelFit(60, 60, 50, true) && !RaidLevelFit(59, 60, 50, true) && !RaidLevelFit(60, 60, 50, false), "raids at 60, phase");
    Require(std::string(LevelSourceName(LevelSource::StoneArea)) == "stone_area" &&
            std::string(LevelSourceName(LevelSource::Required)) == "required", "level_src names");
    Require(FallbackLevelSpan == 10 && StoneMatchRadius == 600.0f, "fallback [required, required + 10], stones within 600 yd");

    // H2) stone matching: (a) the stone's area on the instance map, (b) the map's linked zone, (c) the nearest stone within
    // 600 yd of a trigger on the same map; stones without a max level never; else the fallback.
    std::vector<Stone> stones = {
        Stone{ 100, 0, 0, 10, 20, 500.0f, 0.0f },       // 0: an overworld area, 500 yd from the trigger
        Stone{ 200, 389, 1, 13, 18, 9000.0f, 0.0f },    // 1: its area lies on the instance map 389
        Stone{ 300, 1, 1, 15, 25, 0.0f, 0.0f },         // 2: area 300 = the linked zone of map 43
        Stone{ 400, 0, 0, 30, 0, 100.0f, 0.0f },        // 3: no max level
        Stone{ 500, 0, 0, 22, 30, 300.0f, 0.0f },       // 4: nearer to the trigger than 0
    };
    std::vector<TriggerSpot> const triggers = { TriggerSpot{ 0, 0.0f, 0.0f } };
    StoneMatch match = MatchStone(stones, 389, 300, triggers);
    Require(match.index == 1 && match.source == LevelSource::StoneArea, "(a) the stone area on the instance map first");
    match = MatchStone(stones, 43, 300, triggers);
    Require(match.index == 2 && match.source == LevelSource::LinkedZone, "(b) then the linked zone");
    match = MatchStone(stones, 43, 0, triggers);
    Require(match.index == 4 && match.source == LevelSource::NearStone, "(c) then the nearest stone with a range");
    match = MatchStone(stones, 43, 0, { TriggerSpot{ 1, 0.0f, 0.0f } });
    Require(match.index == 2 && match.source == LevelSource::NearStone, "(c) a stone on the trigger's map only");
    match = MatchStone(stones, 43, 0, { TriggerSpot{ 0, 1100.0f, 0.0f } });
    Require(match.index == 0 && match.source == LevelSource::NearStone, "(c) 600 yd still match");
    match = MatchStone(stones, 43, 0, { TriggerSpot{ 0, -601.0f, 0.0f } });
    Require(match.index == -1 && match.source == LevelSource::Required, "(c) nothing beyond 600 yd");
    std::uint32_t minLevel = 0, maxLevel = 0;
    LevelRange(match, stones, 24, minLevel, maxLevel);
    Require(minLevel == 24 && maxLevel == 34, "fallback [required, required + 10]");
    LevelRange(MatchStone(stones, 389, 0, triggers), stones, 10, minLevel, maxLevel);
    Require(minLevel == 13 && maxLevel == 18, "the stone's range");
    stones[1].areaMapId = NoMap;
    Require(MatchStone(stones, 389, 0, triggers).source == LevelSource::NearStone, "an unknown stone area is no instance match");

    // I) dedup cells, focus mask, cooldowns.
    Require(CellKey(0, 1, 10.0f, 10.0f) == CellKey(0, 1, 29.0f, 0.5f), "one 30-yd cell");
    Require(CellKey(0, 1, 10.0f, 10.0f) != CellKey(0, 1, 31.0f, 10.0f), "the next cell");
    Require(CellKey(0, 1, -10.0f, 10.0f) != CellKey(0, 1, 10.0f, 10.0f), "negative coordinates are their own cell");
    Require(CellKey(0, 1, 10.0f, 10.0f) != CellKey(1, 1, 10.0f, 10.0f) && CellKey(0, 1, 10.0f, 10.0f) != CellKey(0, 3, 10.0f, 10.0f),
            "per map and per focus");
    Require(FocusBit(FocusAnvil) == 1 && FocusBit(FocusForge) == 2 && FocusBit(FocusCooking) == 4 && FocusBit(2) == 0, "focus mask");
    Require(KindOfFocus(FocusCooking) == Kind::Cooking && KindOfFocus(FocusForge) == Kind::Station, "cooking is focus 4");
    Require(IsStationFocus(1) && IsStationFocus(3) && IsStationFocus(4) && !IsStationFocus(2), "station foci");
    Require(CooldownSeconds(Kind::Station) == 600 && CooldownSeconds(Kind::Cooking) == 600, "station / cooking: 10 min, shared");
    Require(CooldownSeconds(Kind::Dungeon) == 1800 && CooldownSeconds(Kind::Raid) == 3600, "dungeon 30, raid 60 min");
    Require(CooldownSeconds(Kind::Fishing) == 0, "fishing follows the gather purpose rules");
    Require(CooldownRunning(100, 101) && !CooldownRunning(101, 101) && !CooldownRunning(100, 0), "cooldown window");
    // The need is the trip's travel condition: the cooldown set at the pick must not end the bot's own open trip.
    Require(CooldownBlocksNeed(100, 700, false) && !CooldownBlocksNeed(100, 700, true), "the own open trip ignores the cooldown");
    Require(!CooldownBlocksNeed(701, 700, false), "no cooldown, no block");
    Require(SharesCooldown(Kind::Station, Kind::Cooking) && SharesCooldown(Kind::Cooking, Kind::Station) &&
            SharesCooldown(Kind::Raid, Kind::Raid) && !SharesCooldown(Kind::Dungeon, Kind::Raid) &&
            !SharesCooldown(Kind::Station, Kind::Fishing), "station and cooking share the cooldown");
    Require(EmptyCooldownSeconds == 300, "an empty request waits 5 min");

    // J) failure memory.
    Require(!ShouldDisable(2, 0), "two failures: not yet");
    Require(ShouldDisable(3, 1), "3 failures > 2 arrivals");
    Require(!ShouldDisable(3, 2) && !ShouldDisable(4, 2) && ShouldDisable(5, 2), "failures > 2 * arrivals");
    Require(DisableSeconds == 3600 && DisabledUntil(1000) == 4600 && CooldownRunning(4599, DisabledUntil(1000)) &&
            !CooldownRunning(4600, DisabledUntil(1000)), "disabled for an hour, not for the uptime");
    Require(!CountsAsFailure(true, true) && CountsAsFailure(true, false) && CountsAsFailure(false, true),
            "a travel expiry under the startup move budget is no failure");

    // K) team masks.
    Require(TeamMaskAfter(TeamBoth, true, false) == TeamHorde && TeamMaskAfter(TeamBoth, false, true) == TeamAlliance, "one hostile team");
    Require(TeamMaskAfter(TeamBoth, true, true) == 0, "hostile to both");
    Require(TeamAllowed(TeamHorde, false) && !TeamAllowed(TeamHorde, true), "team gate");

    // L) the minute summary: pinned field order, empty without a built class.
    std::uint32_t const picks[KindCount] = { 1, 2, 3, 4, 5 };
    std::uint32_t const empty[EmptySlots] = { 6, 7, 8 };
    Require(FormatSummaryFields(picks, empty, 9, 10, 11, 12) ==
            " picks=station:1,cooking:2,fishing:3,dungeon:4,raid:5 empty=station:6,dungeon:7,raid:8 crafts=9 no_focus=10 fail=11 disabled=12",
            "summary field order");
    Stats().picks[0].store(3);
    Stats().disabled.store(2);
    std::string const fields = TakeSummaryFields();
    Require(fields.find(" picks=station:3,") == 0 && fields.find(" disabled=2") != std::string::npos, "built: fields appended");
    Require(Stats().picks[0].load() == 0 && Stats().disabled.load() == 2, "counters reset, disabled is a total");

    // M) trip results.
    Require(std::string(TripResult(true, 2)) == "crafted" && std::string(TripResult(true, 0)) == "arrived" &&
            std::string(TripResult(false, 0)) == "inactive", "trip_end results");
    Require(std::string(TripResult(false, 0, "reset")) == "reset" && std::string(TripResult(true, 0, "replaced")) == "arrived" &&
            std::string(TripResult(true, 1, "ended")) == "crafted", "a trip that never arrived says how it ended");

    std::cout << "direct_target_policy_tests passed\n";
    return 0;
}
