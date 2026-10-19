// twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder): QuestBandPolicy.h - switch, scope, ladder parsing,
// trigger, rung choice, trip clock, rescue and the lines.
#include "QuestBandPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

// hub-ladder.tsv rev2 (sha256 3e8ff26f..., "# version=1 date=2026-10-18"), header and rows copied from the file.
std::string const HeaderLine = "start_key\twave0\tfaction\tcontinent\tmin_level\tmax_level\trank\thub_id\thub_name\tmap_id\t"
    "zone_id\tx\ty\tz\thub_kind\tgiver_count\troute_from_previous\ttransport_ids\treliability\tevidence\tnext_hub_id";

std::string Row(std::string const& key, std::string const& wave0, std::string const& faction, std::string const& minL,
    std::string const& maxL, std::string const& rank, std::string const& hub, std::string const& map,
    std::string const& zone, std::string const& x, std::string const& y, std::string const& route,
    std::string const& ids, std::string const& reliability, std::string const& evidence = "g=6")
{
    return key + "\t" + wave0 + "\t" + faction + "\t0\t" + minL + "\t" + maxL + "\t" + rank + "\t" + hub + "\thub " + hub +
        "\t" + map + "\t" + zone + "\t" + x + "\t" + y + "\t1.0\tboth\t5\t" + route + "\t" + ids + "\t" + reliability + "\t" +
        evidence + "\t-";
}
}

int main()
{
    using namespace ai::quest_band;

    // ---- switch: 0 off, 1 all, 2 odd (cohort B), 3 even (cohort A), anything else off --------------------------------
    for (std::uint32_t guid : { 1u, 2u, 7u, 8u })
    {
        bool const odd = guid & 1u;
        Require(!Applies(0, guid), "mode 0 is off");
        Require(Applies(1, guid), "mode 1 applies to every bot");
        Require(Applies(2, guid) == odd, "mode 2 = odd low guids (cohort B)");
        Require(Applies(3, guid) == !odd, "mode 3 = even low guids (cohort A)");
        Require(!Applies(4, guid) && !Applies(99, guid) && !Applies(0xFFFFFFFFu, guid), "other values act like 0");
        // the same truth table as R01CohortPolicy.h for 1-3 (literal: that header is not on release/9.x yet)
        bool const r01[4] = { true, true, odd, !odd };
        for (std::uint32_t mode = 1; mode <= 3; ++mode)
            Require(Applies(mode, guid) == r01[mode], "modes 1-3 equal the S1Cohort / S3Cohort rule");
        Require(ai::cohort::Letter(guid) == (odd ? 'B' : 'A'), "cohort letter B = odd");
    }

    // ---- scope ----------------------------------------------------------------------------------------------------
    {
        ScopeFacts ok;
        ok.applies = ok.rosterMember = ok.questFirst = true;
        ok.level = 20;
        ok.maxLevel = 60;
        Require(InScope(ok), "roster bot on its own below max level");
        ScopeFacts f = ok; f.applies = false; Require(!InScope(f), "switch / cohort off");
        f = ok; f.rosterMember = false; Require(!InScope(f), "not a roster member");
        f = ok; f.questFirst = false; Require(!InScope(f), "quest-first off");
        f = ok; f.realMaster = true; Require(!InScope(f), "real master");
        f = ok; f.grouped = true; Require(!InScope(f), "grouped");
        f = ok; f.level = 60; Require(!InScope(f), "at max level");
        f = ok; f.maxLevel = 0; Require(!InScope(f), "max level unknown: fail closed");
    }

    // ---- keys -----------------------------------------------------------------------------------------------------
    Require(KeyIndex("1") == 1 && KeyIndex("10") == 10 && KeyIndex("A0") == 11 && KeyIndex("A1") == 12 &&
        KeyIndex("H0") == 13 && KeyIndex("H1") == 14, "key indices");
    Require(!KeyIndex("") && !KeyIndex("0") && !KeyIndex("11") && !KeyIndex("01") && !KeyIndex("A2") && !KeyIndex("x") &&
        !KeyIndex("123"), "unknown keys are 0");
    Require(KeyFor(2, 'H', 1, 1) == 2 && KeyFor(2, 'H', 1, 12) == 2, "L1-12: the race key");
    Require(KeyFor(2, 'H', 1, 13) == 14 && KeyFor(1, 'A', 0, 30) == 11 && KeyFor(4, 'A', 1, 30) == 12, "L13+: faction + map");
    Require(!KeyFor(1, 'A', 530, 30) && !KeyFor(1, 'A', 36, 20) && !KeyFor(0, 'A', 0, 5) && !KeyFor(1, 'X', 0, 20),
        "other maps, no race, no faction: no key");

    // ---- header ---------------------------------------------------------------------------------------------------
    Header h;
    Require(ParseHeader(HeaderLine, h) && h.fields == 21 && h.col[ColStartKey] == 0 && h.col[ColNextHubId] == 20,
        "the rev2 header");
    {
        Header h2;
        Require(ParseHeader("extra\t" + HeaderLine + "\tmore", h2) && h2.col[ColStartKey] == 1 && h2.fields == 23,
            "columns by name, extra columns ignored");
        std::string missing = HeaderLine;
        missing.replace(missing.find("\treliability"), 12, "");
        Require(!ParseHeader(missing, h2), "a missing column fails");
        Require(!ParseHeader("", h2), "an empty header fails");
        Require(ParseHeader(HeaderLine + "\r", h2), "a CRLF line end is tolerated");
    }

    // ---- rows -----------------------------------------------------------------------------------------------------
    std::vector<std::uint32_t> taxi;
    auto parse = [&](std::string const& line, bool crsz, bool allowFlight, Rung& r) {
        return ParseRow(SplitTabs(line), h, crsz, allowFlight, r, taxi);
    };
    {
        Rung r;
        // real rows: Human start (first band), Goldshire (walk), Stormwind (degraded)
        Require(parse(Row("1", "0", "A", "1", "5", "1", "793", "0", "12", "-8949.95", "-132.493", "-", "-", "ok"), false, false, r) ==
            RowVerdict::Use && r.route == Route::First && r.keyIndex == 1 && r.zoneId == 12 && r.raceMask == 0xFFFF,
            "race key first band '-' is usable");
        Require(parse(Row("1", "0", "A", "6", "9", "1", "717", "0", "12", "-9462.66", "16.19", "walk", "-", "ok"), false, false, r) ==
            RowVerdict::Use && r.route == Route::Walk && r.minLevel == 6 && r.maxLevel == 9, "walk row is usable");
        Require(parse(Row("1", "0", "A", "6", "9", "2", "1311", "0", "1519", "-8867.79", "673.67", "walk", "-", "degraded"), false, false, r) ==
            RowVerdict::SkipReliability, "reliability != ok is skipped");
        Require(parse(Row("1", "0", "A", "6", "9", "1", "717", "0", "12", "-9462.66", "16.19", "-", "-", "ok"), false, false, r) ==
            RowVerdict::SkipRoute, "'-' outside the first band is skipped");
        Require(parse(Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "-9.0", "1.0", "ship", "141", "ok"), false, true, r) ==
            RowVerdict::SkipRoute, "ship / zeppelin / portal routes are skipped");
        // parse failures
        Require(parse(Row("1", "0", "A", "0", "5", "1", "793", "0", "12", "1", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "level 0");
        Require(parse(Row("1", "0", "A", "6", "5", "1", "793", "0", "12", "1", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "min > max");
        Require(parse(Row("1", "0", "A", "1", "5", "3", "793", "0", "12", "1", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "rank 3");
        Require(parse(Row("1", "0", "A", "1", "5", "1", "793", "0", "12", "abc", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "x not numeric");
        Require(parse(Row("1", "0", "A", "1", "5", "1", "793", "0", "0", "1", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "zone 0");
        Require(parse(Row("Q1", "0", "A", "1", "5", "1", "793", "0", "12", "1", "1", "-", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "unknown key");
        Require(parse(Row("1", "0", "A", "10", "14", "1", "793", "0", "12", "1", "1", "walk", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "race key above 12");
        Require(parse(Row("A0", "0", "A", "10", "14", "1", "793", "0", "12", "1", "1", "walk", "-", "ok"), false, false, r) == RowVerdict::SkipParse, "faction key below 13");
        Require(parse(Row("1", "0", "A", "1", "5", "1", "793", "0", "12", "1", "1", "-", "-", "ok") + "\tx", false, false, r) ==
            RowVerdict::SkipParse, "column count differs from the header");
        // a faction key may hold a hub on the other continent (A0 13-14 r2 Auberdine, map 1): parsed, rejected at choice time
        Require(parse(Row("A0", "0", "A", "15", "19", "1", "162", "1", "148", "6406.5", "515.4", "walk", "-", "ok"), false, false, r) ==
            RowVerdict::Use && r.mapId == 1, "faction key hub on another map parses");
    }
    // wave0 per custom race start zones
    {
        Rung r;
        std::string const g0 = Row("9", "0", "H", "1", "5", "1", "1028", "1", "14", "1", "1", "-", "-", "ok");
        std::string const g1 = Row("9", "1", "H", "1", "5", "1", "1839", "1", "5536", "1", "1", "-", "-", "ok");
        Require(parse(g0, false, false, r) == RowVerdict::Use && parse(g1, false, false, r) == RowVerdict::SkipWave0, "CRSZ 0: wave0 0 rows");
        Require(parse(g0, true, false, r) == RowVerdict::SkipWave0 && parse(g1, true, false, r) == RowVerdict::Use, "CRSZ 1: wave0 1 rows");
        Require(parse(Row("1", "1", "A", "1", "5", "1", "793", "0", "12", "1", "1", "-", "-", "ok"), true, false, r) ==
            RowVerdict::SkipWave0, "other keys: wave0 must be 0");
    }
    // flight rows: TaxiPath ids bare or tp:N (CLI-PATH 19.10.2026), the taxi cheat decides
    {
        Rung r;
        std::string const fl = Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "-9219.4", "-2149.0", "flight", "107,33", "ok");
        Require(parse(fl, false, true, r) == RowVerdict::Use && taxi.size() == 2 && taxi[0] == 107 && taxi[1] == 33,
            "bare ids are TaxiPath ids");
        Require(parse(fl, false, false, r) == RowVerdict::DegradedNoTaxiCheat, "no taxi cheat: degraded");
        Require(parse(Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "1", "1", "flight", "tp:107", "ok"), false, true, r) ==
            RowVerdict::Use && taxi.size() == 1 && taxi[0] == 107, "typed tp:N");
        Require(parse(Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "1", "1", "flight", "-", "ok"), false, true, r) ==
            RowVerdict::DegradedTransport, "flight without ids");
        Require(parse(Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "1", "1", "flight", "107,go:5", "ok"), false, true, r) ==
            RowVerdict::DegradedTransport, "a go: token on a flight row");
        Require(parse(Row("A0", "0", "A", "20", "24", "1", "826", "0", "44", "1", "1", "flight", "0", "ok"), false, true, r) ==
            RowVerdict::DegradedTransport, "id 0");
    }
    // first faction band: per-race legs from from_l12 (A0 13-14 r1 Sentinel Hill of rev2)
    {
        std::string const ev = "g=3; nodes=live439; from_l12=1:1194>walk,3:478>flight,7:478>flight,10:1194>walk; from_l12_w0=10:1856>flight";
        Require(EvidenceValue(ev, "from_l12") == "1:1194>walk,3:478>flight,7:478>flight,10:1194>walk", "evidence value");
        Require(EvidenceValue(ev, "from_l12_w0") == "10:1856>flight" && EvidenceValue(ev, "nope").empty(), "w0 value, absent");
        Require(FromL12Mask(ev, false, false) == ((1u << 1) | (1u << 10)), "walk only: races 1 and 10");
        Require(FromL12Mask(ev, false, true) == ((1u << 1) | (1u << 3) | (1u << 7) | (1u << 10)), "with taxis: 1, 3, 7, 10");
        Require(FromL12Mask(ev, true, false) == (1u << 1), "CRSZ 1, walk only: race 10 reads w0 (flight) -> only 1");
        Require(FromL12Mask(ev, true, true) == ((1u << 1) | (1u << 3) | (1u << 7) | (1u << 10)), "CRSZ 1 with taxis");
        Require(FromL12Mask("from_l12=2:1068>none(pred_unusable),8:1068>walk", false, true) == (1u << 8), "suffixed class not allowed");
        Require(FromL12Mask("from_l12_w0=9:1839>flight(r1_unusable)", true, true) == 0, "w0 suffix not allowed");
        Require(FromL12Mask("g=1", false, true) == 0 && FromL12Mask("from_l12=x:1>walk,99:1>walk", false, true) == 0,
            "no list / bad races: nobody");

        Rung r;
        Require(ParseRow(SplitTabs(Row("A0", "0", "A", "13", "14", "1", "1194", "0", "40", "-10653.3", "1166.4", "-", "-", "ok", ev)),
            h, false, false, r, taxi) == RowVerdict::Use && r.route == Route::First && r.raceMask == ((1u << 1) | (1u << 10)),
            "A0 13-14 r1 carries the race mask");
        Require(ParseRow(SplitTabs(Row("H1", "0", "H", "13", "14", "2", "1531", "1", "17", "-407.1", "-2645.2", "-", "-", "ok",
            "from_l12=2:1068>flight,5:9>zeppelin")), h, false, false, r, taxi) == RowVerdict::SkipFromL12, "no walking race: skipped");
    }

    // ---- ladder: coverage bitmap, duplicates ------------------------------------------------------------------------
    Ladder ladder;
    auto add = [&](std::string const& line) {
        Rung r;
        RowVerdict const v = parse(line, false, false, r);
        Require(v == RowVerdict::Use, "fixture row usable");
        Require(!ladder.Has(r), "fixture row unique");
        ladder.Add(r);
    };
    // orc / troll (key 2), Durotar 14 -> Barrens 17
    add(Row("2", "0", "H", "1", "5", "1", "1028", "1", "14", "-618.5", "-4251.7", "-", "-", "ok"));
    add(Row("2", "0", "H", "6", "9", "1", "1068", "1", "14", "340.4", "-4686.3", "walk", "-", "ok"));
    add(Row("2", "0", "H", "10", "12", "1", "1068", "1", "14", "340.4", "-4686.3", "walk", "-", "ok"));
    add(Row("2", "0", "H", "10", "12", "2", "1531", "1", "17", "-407.1", "-2645.2", "walk", "-", "ok"));
    add(Row("H1", "0", "H", "13", "14", "1", "1531", "1", "17", "-407.1", "-2645.2", "-", "-", "ok",
        "from_l12=2:1068>walk,8:1068>walk"));
    add(Row("H1", "0", "H", "15", "19", "1", "1597", "1", "17", "-890.0", "-3770.0", "walk", "-", "ok"));
    add(Row("H1", "0", "H", "20", "24", "1", "2001", "1", "406", "900.0", "900.0", "walk", "-", "ok"));
    add(Row("H1", "0", "H", "20", "24", "2", "2002", "0", "33", "-14000.0", "400.0", "walk", "-", "ok"));
    // humans (key 1) and A0
    add(Row("1", "0", "A", "1", "5", "1", "793", "0", "12", "-8949.95", "-132.49", "-", "-", "ok"));
    add(Row("A0", "0", "A", "25", "29", "1", "826", "0", "44", "-9219.4", "-2149.0", "walk", "-", "ok"));
    add(Row("A0", "0", "A", "25", "29", "2", "300", "0", "11", "-3500.0", "-800.0", "walk", "-", "ok"));
    {
        Rung dup;
        parse(Row("2", "0", "H", "6", "9", "1", "9999", "1", "14", "1", "1", "walk", "-", "ok"), false, false, dup);
        Require(ladder.Has(dup), "same key, wave0, min, rank is a duplicate");
    }
    Require(ladder.Covers(2, 2, 1) && ladder.Covers(2, 2, 12) && ladder.Covers(2, 8, 7), "race key covers 1-12 for every race");
    Require(ladder.Covers(14, 2, 13) && ladder.Covers(14, 8, 14) && !ladder.Covers(14, 5, 13), "13-14 only for walking races");
    Require(ladder.Covers(14, 5, 15) && ladder.Covers(14, 5, 24) && !ladder.Covers(14, 5, 25), "H1 15-24, not 25");
    Require(!ladder.Covers(12, 4, 30) && !ladder.Covers(0, 1, 5) && !ladder.Covers(99, 1, 5) && !ladder.Covers(2, 99, 5) &&
        !ladder.Covers(2, 2, 64), "no rows / bad input: not covered");

    // ---- trigger --------------------------------------------------------------------------------------------------
    {
        StepFacts f;
        f.emptyOffer = true;
        f.priorFailures = 1;
        f.freeSlots = 12;
        f.slotGate = 5;
        Require(BandStepDue(f) == StepVerdict::Due, "zone done, second empty search: due");
        StepFacts g = f; g.emptyOffer = false; Require(BandStepDue(g) == StepVerdict::NotDue, "not EmptyOffer");
        g = f; g.dangerMap = 1; Require(BandStepDue(g) == StepVerdict::NotDue, "danger map");
        g = f; g.offered = 1; Require(BandStepDue(g) == StepVerdict::NotDue, "an offered turn-in");
        g = f; g.openTurnIns = 1; Require(BandStepDue(g) == StepVerdict::NotDue, "an open turn-in");
        g = f; g.priorFailures = 0; Require(BandStepDue(g) == StepVerdict::NotDue, "first empty search");
        g = f; g.freeSlots = 4; Require(BandStepDue(g) == StepVerdict::SkipLogFull, "log full");
        g = f; g.freeSlots = 4; g.emptyOffer = false; Require(BandStepDue(g) == StepVerdict::NotDue, "not due beats log full");
    }
    // giver filter = the accept rule; giver_done
    Require(GreyGiverSkip(20, 12, 13, false) && !GreyGiverSkip(20, 14, 13, false), "grey quest giver skipped, green kept");
    Require(GreyGiverSkip(20, 25, 13, false) && !GreyGiverSkip(20, 12, 13, true), "red skipped, class quest kept");
    Require(GiverTripDone(true, false) && !GiverTripDone(true, true) && !GiverTripDone(false, false), "giver_done only with the filter");

    // ---- rung choice ----------------------------------------------------------------------------------------------
    auto facts = [](std::uint32_t race, char faction, std::uint32_t map, std::uint32_t level, std::uint32_t zone, float x,
        float y, float radius) {
        RungFacts f;
        f.race = race; f.faction = faction; f.mapId = map; f.level = level; f.zoneId = zone; f.x = x; f.y = y;
        f.searchedRadius = radius;
        return f;
    };
    {
        // orc L10 in Durotar at Razor Hill, stage-1 radius 6000: r1 same zone, r2 Crossroads in radius, 13 > 10 + 2
        std::vector<Candidate> c = NextRung(ladder, facts(2, 'H', 1, 10, 14, 340.f, -4686.f, 6000.f));
        Require(c.size() == 2 && c[0].reject == Reject::SameZone && c[1].reject == Reject::InRadius, "orc L10: no rung");
        Require(FirstReject(c) == Reject::SameZone, "first reject reported");
        // stage 0 (2000 yd): Crossroads (2174 yd) is outside the searched radius
        c = NextRung(ladder, facts(2, 'H', 1, 10, 14, 340.f, -4686.f, 2000.f));
        Require(c.size() == 2 && c[1].reject == Reject::None && ladder.rungs[std::size_t(c[1].index)].hubId == 1531,
            "orc L10 stage 0: Crossroads");
        // L11: the next band 13-14 is a candidate (11 + 2 >= 13) under the faction key
        c = NextRung(ladder, facts(2, 'H', 1, 11, 14, 340.f, -4686.f, 2000.f));
        Require(c.size() == 3 && ladder.rungs[std::size_t(c[2].index)].keyIndex == 14 &&
            ladder.rungs[std::size_t(c[2].index)].minLevel == 13, "race key -> faction key at 12 -> 13");
        // an undead (race 5) without race-key rows: at L11 the faction key's 13-14 band is next, but it walks no leg there
        c = NextRung(ladder, facts(5, 'H', 1, 11, 85, 0.f, 0.f, 0.f));
        Require(c.size() == 1 && c[0].reject == Reject::RaceRoute, "race not walking into 13-14");
        c = NextRung(ladder, facts(5, 'H', 1, 13, 85, 0.f, 0.f, 0.f));
        Require(c.size() == 2 && c[0].reject == Reject::RaceRoute && c[1].reject == Reject::None &&
            ladder.rungs[std::size_t(c[1].index)].minLevel == 15, "L13 undead: 13-14 race_route, 15-19 next");
        // L20 tauren: band 20-24 r1 Thousand Needles, r2 on map 0 -> other_map; 25 is not within 2 levels
        c = NextRung(ladder, facts(6, 'H', 1, 20, 17, -890.f, -3770.f, 2000.f));
        Require(c.size() == 2 && c[0].reject == Reject::None && c[1].reject == Reject::OtherMap, "other map rejected");
        // ping-pong memory: the zone left in this band is not a candidate again, in another band it is
        RungFacts f = facts(6, 'H', 1, 20, 17, -890.f, -3770.f, 0.f);
        f.leftZones[0] = 406;
        f.leftBandMin = 20;
        c = NextRung(ladder, f);
        Require(c[0].reject == Reject::LeftZone, "left zone in the same band");
        f.leftBandMin = 15;
        c = NextRung(ladder, f);
        Require(c[0].reject == Reject::None, "left zone of an older band is allowed again");
        // human L26 in Redridge (zone 44): r1 Lakeshire same zone, r2 Menethil far away -> step
        c = NextRung(ladder, facts(1, 'A', 0, 26, 44, -9219.f, -2149.f, 6000.f));
        Require(c.size() == 2 && c[0].reject == Reject::SameZone && c[1].reject == Reject::None &&
            ladder.rungs[std::size_t(c[1].index)].hubId == 300, "human L26 Redridge -> Menethil");
        // no key (other map) and no band
        Require(NextRung(ladder, facts(1, 'A', 530, 26, 44, 0.f, 0.f, 0.f)).empty(), "outland: no key");
        Require(NextRung(ladder, facts(1, 'A', 0, 40, 44, 0.f, 0.f, 0.f)).empty() &&
            FirstReject(NextRung(ladder, facts(1, 'A', 0, 40, 44, 0.f, 0.f, 0.f))) == Reject::NoBand, "no band: no_band");
        Require(CurrentBandMin(ladder, 14, 17) == 15 && CurrentBandMin(ladder, 14, 30) == 0, "current band");
    }

    // ---- trip clock -----------------------------------------------------------------------------------------------
    Require(BandTripSeconds(0.f) == MinTripSeconds && BandTripSeconds(-5.f) == MinTripSeconds, "floor 300 s");
    Require(BandTripSeconds(2174.f) == 1051, "Razor Hill -> Crossroads: 120 + 3 * 2174 / 7");
    Require(BandTripSeconds(100000.f) == MaxTripSeconds, "cap 2700 s");
    {
        std::uint32_t const now = 1000000, end = now + 600;
        Require(TripStatusMs(StatusTravel, 0, end, now) == 600000 && TripStatusMs(StatusReady, 3600000, end, now) == 600000 &&
            TripStatusMs(StatusWork, 5000, end, now) == 600000, "READY / TRAVEL / WORK last the time left");
        Require(TripStatusMs(StatusCooldown, 60000, end, now) == 60000 && TripStatusMs(StatusCooldown, 900000, end, now) == 600000,
            "COOLDOWN at most the time left");
        Require(TripStatusMs(StatusTravel, 0, end, end) == 1 && TripStatusMs(StatusTravel, 0, end, end + 5) == 1, "1 ms once ended");
        Require(TripStatusMs(StatusTravel, 0, now + 100000, now) == (MaxTripSeconds + 60) * 1000, "a clock step back is capped");
        Require(TripStatusMs(0, 1, end, now) == 1 && TripStatusMs(6, 1, end, now) == 1 && TripStatusMs(1, 7, end, now) == 7,
            "NONE / PREPARE / EXPIRED unchanged");
    }
    {
        TripFacts f;
        f.hubDestination = true; f.tripEnd = 2000; f.scopeOk = true; f.now = 1000;
        Require(CheckTrip(f) == TripVerdict::Running, "running");
        TripFacts g = f; g.cooldown = true; Require(CheckTrip(g) == TripVerdict::ExpiredCooldown, "cooldown ends it");
        g = f; g.now = 2000; g.cooldown = true; Require(CheckTrip(g) == TripVerdict::ExpiredEnd, "end beats cooldown");
        g = f; g.scopeOk = false; g.now = 3000; Require(CheckTrip(g) == TripVerdict::ScopeLost, "scope beats end");
        g = f; g.tripEnd = 0; g.scopeOk = false; Require(CheckTrip(g) == TripVerdict::Orphan, "hub without an end: orphan first");
        g = f; g.hubDestination = false; Require(CheckTrip(g) == TripVerdict::ScopeLost, "end set on another destination");
        g = TripFacts(); g.now = 5; Require(CheckTrip(g) == TripVerdict::Running, "no band trip: nothing to end");
        // OB-10 (b): a status clock shortened from outside ends as clock_shortened; near the end it is the end itself
        g = f; g.clockRanOut = true; Require(CheckTrip(g) == TripVerdict::ClockShortened, "clock shortened 1000 s early");
        g = f; g.clockRanOut = true; g.now = 2000 - ClockSlackSeconds; Require(CheckTrip(g) == TripVerdict::ExpiredEnd, "within the slack: the end");
        g = f; g.clockRanOut = true; g.now = 2000 - ClockSlackSeconds - 1; Require(CheckTrip(g) == TripVerdict::ClockShortened, "just outside the slack");
        g = f; g.clockRanOut = true; g.cooldown = true; Require(CheckTrip(g) == TripVerdict::ExpiredCooldown, "cooldown beats the clock");
        g = f; g.clockRanOut = true; g.scopeOk = false; Require(CheckTrip(g) == TripVerdict::ScopeLost, "scope beats the clock");
    }

    // ---- rescue ---------------------------------------------------------------------------------------------------
    Require(!RescueDue(0) && !RescueDue(1) && RescueDue(2) && RescueDue(3), "second expiry");
    Require(RescueAllowed(0, 5) && !RescueAllowed(1000, 4599) && RescueAllowed(1000, 4600) && !RescueAllowed(5000, 4000),
        "one per hour; a clock step back fails closed");
    {
        bool dropped = true;
        std::uint64_t const p = PackRescue(7, 1000);
        Require(UnpackRescue(p, 1000, dropped) == 8 && !dropped && UnpackRescue(p, 1600, dropped) == 8, "fresh: rung + 1");
        Require(UnpackRescue(p, 1601, dropped) == 0 && dropped, "older than 600 s: dropped");
        Require(UnpackRescue(p, 999, dropped) == 0 && dropped, "from the future: dropped");
        Require(UnpackRescue(0, 1000, dropped) == 0 && !dropped, "none");
        Require(UnpackRescue(PackRescue(0, 1000), 1000, dropped) == 1, "rung 0 is not 'none'");
    }

    // ---- lines ----------------------------------------------------------------------------------------------------
    {
        LineHead lh;
        lh.bot = 4711; lh.level = 26; lh.cohort = 'B'; lh.fromZone = 44; lh.toHub = 300;
        Rung const& men = ladder.rungs[10];
        Require(FormatStep(lh, men, 5480.4f, 2468, 6000.f, 1) ==
            "[QuestBand] state=step bot=4711 level=26 cohort=B from_zone=44 to_hub=300 to_zone=11 key=A0 band=25-29 rank=2 "
            "route=walk dist=5480 trip_s=2468 searched_r=6000 failures=1", "step line");
        Require(FormatArrived(lh, 1200) == "[QuestBand] state=arrived bot=4711 level=26 cohort=B from_zone=44 to_hub=300 took_s=1200",
            "arrived line");
        Require(FormatExpired(lh, true, 2, 812.6f) ==
            "[QuestBand] state=expired bot=4711 level=26 cohort=B from_zone=44 to_hub=300 reason=cooldown expiries=2 dist_left=813",
            "expired line");
        Require(FormatRescue(lh, "requested") ==
            "[QuestBand] state=rescue bot=4711 level=26 cohort=B from_zone=44 to_hub=300 result=requested", "rescue line");
        lh.toHub = 0;
        Require(FormatSkip(lh, "no_rung", RejectName(Reject::InRadius)) ==
            "[QuestBand] state=skip bot=4711 level=26 cohort=B from_zone=44 to_hub=0 reason=no_rung detail=in_radius", "skip line");
        Require(FormatSkipLogFull(lh, 3, 2, 1) ==
            "[QuestBand] state=skip bot=4711 level=26 cohort=B from_zone=44 to_hub=0 reason=log_full detail=free_slots:3 group=2 elite=1",
            "log_full line with group / elite counts");
        std::uint32_t counts[EventCount] = {};
        Require(FormatMinute('A', counts).empty(), "no minute line without events");
        counts[EvStep] = 3; counts[EvGiverDone] = 9;
        Require(FormatMinute('A', counts) == "[QuestBand] state=minute cohort=A steps=3 arrived=0 expired=0 expired_cooldown=0 "
            "rescue_req=0 rescue_limit=0 rescue_blocked=0 rescues=0 skip_no_rung=0 skip_log_full=0 scope_lost=0 replaced=0 "
            "park=0 orphan=0 giver_done=9 clock_shortened=0", "minute line");
        Require(FormatLadderOff("no_file", "") == "[HubLadder] state=off reason=no_file path=", "off line");
        Require(FormatRowSkipped(false, 17, men, "no_node") ==
            "[HubLadder] state=row_skipped line=17 key=A0 wave0=0 band=25-29 rank=2 hub=300 reason=no_node", "row line");
        LoadCounts lc;
        lc.bytes = 51234; lc.fnv = 0xDEADBEEF; lc.rows = 134; lc.usable = 60; lc.mode = 2;
        Require(FormatLoaded("/opt/turtle/etc/hub-ladder.tsv", lc).find("[HubLadder] state=loaded path=/opt/turtle/etc/hub-ladder.tsv "
            "version=1 bytes=51234 fnv=deadbeef rows=134 usable=60 ") == 0, "loaded line");
        Require(RejectName(Reject::HubAbove) == std::string("hub_above") && RowVerdictName(RowVerdict::DegradedNoTaxiCheat) ==
            std::string("no_taxi_cheat"), "names");
    }
    Require(Fnv1a("") == 2166136261u && Fnv1a("a") == 0xE40C292Cu, "FNV-1a 32");

    std::cout << "quest_band_policy_tests passed\n";
    return 0;
}
