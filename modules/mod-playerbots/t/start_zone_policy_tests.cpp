#include "StartZonePolicy.h"

#include <cstdlib>
#include <iostream>

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
    using namespace ai::start_zone;

    // E4: points by zone box. Goblin start, Taxxlo, Fanzo's machine -> Horde; high-elf start, Rommath, the pier -> Alliance.
    Require(TeamOfPoint(1, -233.0f, -7177.0f) == StartTeam::Horde, "goblin start is Horde");
    Require(TeamOfPoint(1, -327.0f, -7466.5f) == StartTeam::Horde, "Taxxlo (5537) is Horde");
    Require(TeamOfPoint(1, -570.7f, -7849.9f) == StartTeam::Horde, "machine 50597 is Horde");
    Require(TeamOfPoint(1, 819.0f, -5006.3f) == StartTeam::None, "Sparkwater Port is no start zone");
    Require(TeamOfPoint(1, 878.0f, -5068.5f) == StartTeam::None, "Nert is no start zone");
    Require(TeamOfPoint(0, 3212.6f, -2501.4f) == StartTeam::Alliance, "high-elf start is Alliance");
    Require(TeamOfPoint(0, 4312.5f, -3067.9f) == StartTeam::Alliance, "Rommath (Alah'Thalas) is Alliance");
    Require(TeamOfPoint(0, AlahDockX, AlahDockY) == StartTeam::Alliance, "Windrunner Pier is Alliance");
    Require(TeamOfPoint(0, 2280.0f, -5300.0f) == StartTeam::None, "Light's Hope is no start zone");
    Require(TeamOfPoint(0, -233.0f, -7177.0f) == StartTeam::None, "wrong map");

    // E4: only an entry with all points in one start zone is tagged.
    {
        TeamFold fold;
        Require(fold.Uniform() == StartTeam::None, "no points: none");
        fold.Add(StartTeam::Horde);
        fold.Add(StartTeam::Horde);
        Require(fold.Uniform() == StartTeam::Horde, "all island points: Horde");
        fold.Add(StartTeam::None);
        Require(fold.Uniform() == StartTeam::None, "inside and outside: none");
    }
    {
        TeamFold fold;
        fold.Add(StartTeam::None);
        fold.Add(StartTeam::Alliance);
        Require(fold.Uniform() == StartTeam::None, "outside first, then inside: none");
    }
    Require(KeepStartZonePoint(false, StartTeam::None, StartTeam::None), "off: outside point kept");
    Require(!KeepStartZonePoint(false, StartTeam::Horde, StartTeam::Horde), "off: island point dropped");
    Require(KeepStartZonePoint(true, StartTeam::Horde, StartTeam::Horde), "on: uniform island entry kept");
    Require(!KeepStartZonePoint(true, StartTeam::Horde, StartTeam::None), "on: island point of a mixed entry dropped");
    Require(KeepStartZonePoint(true, StartTeam::None, StartTeam::None), "on: outside point of a mixed entry kept");

    // E5: faction filter.
    Require(DestinationAllows(StartTeam::None, true) && DestinationAllows(StartTeam::None, false), "untagged: everyone");
    Require(DestinationAllows(StartTeam::Horde, false) && !DestinationAllows(StartTeam::Horde, true), "island: Horde only");
    Require(DestinationAllows(StartTeam::Alliance, true) && !DestinationAllows(StartTeam::Alliance, false), "highlands: Alliance only");

    // E5 / plan 0.2: area-level exemption, own start zone (and Sparkwater for goblins) up to level 12.
    Require(ZoneLevelExempt(true, 9, 1, 1, -327.0f, -7466.5f), "goblin L1 at Taxxlo");
    Require(ZoneLevelExempt(true, 9, 5, 1, 878.0f, -5068.5f), "goblin L5 at Nert (4021 = 36)");
    Require(!ZoneLevelExempt(true, 9, 13, 1, 878.0f, -5068.5f), "goblin L13: no exemption");
    Require(!ZoneLevelExempt(false, 9, 5, 1, 878.0f, -5068.5f), "off: no exemption");
    Require(!ZoneLevelExempt(true, 2, 5, 1, 878.0f, -5068.5f), "orc: no exemption at Sparkwater");
    Require(!ZoneLevelExempt(true, 2, 5, 1, -327.0f, -7466.5f), "orc: no exemption on the island");
    Require(ZoneLevelExempt(true, 10, 12, 0, 3762.0f, -2366.0f), "high elf L12 at Braedin");
    Require(!ZoneLevelExempt(true, 1, 5, 0, 3762.0f, -2366.0f), "human: no exemption in the highlands");
    Require(!ZoneLevelExempt(true, 10, 5, 1, 878.0f, -5068.5f), "high elf: no Sparkwater exemption");

    // E1 / E2 / E3.
    Require(FactoryUsesProxyStart(9, false) && FactoryUsesProxyStart(10, false), "off: proxy starts");
    Require(!FactoryUsesProxyStart(9, true) && !FactoryUsesProxyStart(10, true), "on: real starts");
    Require(!FactoryUsesProxyStart(2, false), "orc: never moved");
    Require(RepopUsesHomebind(9, 5, false), "off: goblin repops at homebind");
    Require(!RepopUsesHomebind(9, 10, true), "on: goblin L10 repops at the real start");
    Require(RepopUsesHomebind(10, 11, true), "on: high elf L11 repops at homebind");
    Require(!RepopUsesHomebind(1, 5, false), "human: race start as before");
    Require(TeleportSkipsZone(5536, 9, false) && TeleportSkipsZone(5225, 10, false), "off: zones skipped for everyone");
    Require(TeleportSkipsZone(2040, 10, false), "off: Alah'Thalas skipped too");
    Require(!TeleportSkipsZone(5536, 9, true) && TeleportSkipsZone(5536, 2, true), "on: island for goblins only");
    Require(!TeleportSkipsZone(2040, 10, true) && TeleportSkipsZone(5225, 1, true), "on: highlands for high elves only");
    Require(!TeleportSkipsZone(14, 2, false), "Durotar untouched");

    // E7 / E8.
    Require(IsScriptedTaxi(311) && IsScriptedTaxi(322) && !IsScriptedTaxi(303), "scripted taxis");
    Require(FlightGated(true, 303, 49) && FlightGated(true, 304, 10), "Light's Hope closed below 50");
    Require(!FlightGated(true, 303, 50) && !FlightGated(false, 303, 10) && !FlightGated(true, 311, 5), "open at 50, off, others");
    Require(WalkOutGated(true, 19, 0, 3762.0f, -2366.0f, 0, 2280.0f, -5300.0f), "L19: no walk out");
    Require(!WalkOutGated(true, 20, 0, 3762.0f, -2366.0f, 0, 2280.0f, -5300.0f), "L20: walk out");
    Require(!WalkOutGated(true, 5, 0, 3762.0f, -2366.0f, 0, 4312.5f, -3067.9f), "inside the zone: open");
    Require(!WalkOutGated(true, 5, 0, 2280.0f, -5300.0f, 0, 3762.0f, -2366.0f), "walk in: open");
    Require(!WalkOutGated(false, 5, 0, 3762.0f, -2366.0f, 0, 2280.0f, -5300.0f), "off: open");

    // Q1: the round trip.
    Require(QuestFlightPath(true, 9, 3, true, 3000.0f, true, false) == IslandPath, "41169 on the island: out");
    Require(QuestFlightPath(true, 9, 3, true, 3000.0f, false, true) == 0, "41170 on the island: stay");
    Require(QuestFlightPath(true, 9, 5, false, 87.0f, false, true) == ReturnPath, "41170 at Nert: back");
    Require(QuestFlightPath(true, 9, 5, false, 87.0f, true, false) == 0, "41169 at Sparkwater: hand in first");
    Require(QuestFlightPath(true, 9, 11, false, 87.0f, false, true) == 0, "41170 at L11: no return");
    Require(QuestFlightPath(true, 9, 5, false, 1600.0f, false, true) == 0, "41170 far from Durotar: no return");
    Require(QuestFlightPath(false, 9, 5, true, 3000.0f, true, false) == 0, "off: none");
    Require(QuestFlightPath(true, 2, 5, true, 3000.0f, true, false) == 0, "orc: none");
    Require(MachineOf(IslandPath).x == IslandMachineX && MachineOf(ReturnPath).x == PortMachineX, "machine by path");
    Require(IslandLeaveAllowed(true, false) && !IslandLeaveAllowed(false, false) && IslandLeaveAllowed(false, true),
        "8.29 roster rule, every bot when on");

    // S1: the ship.
    Require(ShipCandidate(true, 10, 0, 3762.0f, -2366.0f), "high elf in the highlands");
    Require(!ShipCandidate(false, 10, 0, 3762.0f, -2366.0f) && !ShipCandidate(true, 1, 0, 3762.0f, -2366.0f), "off / human");
    Require(!ShipCandidate(true, 10, 1, AuberdineDockX, AuberdineDockY), "already in Auberdine");
    Require(ShouldTakeShip(9, 0, true), "41259 in the log");
    Require(ShouldTakeShip(12, 0, false), "level 12");
    Require(!ShouldTakeShip(11, 0, false), "level 11, busy: stay");
    Require(ShouldTakeShip(10, ShipIdleSeconds, false) && !ShouldTakeShip(9, ShipIdleSeconds, false), "idle from level 10");

    std::cout << "start_zone_policy_tests passed\n";
    return 0;
}
