#pragma once

#include "CohortPolicy.h"
#include "QuestAcceptPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ai::quest_band
{
// twow-repo#601 S5a (owner D1 19.10.2026: no gap grind, no standing; OB-00 D9 variant b): a persistent roster bot on its
// own whose zone has no acceptable quest and no open turn-in left steps to the next quest hub of hub-ladder.tsv. The trip
// has a wall-clock end decided once at entry; nothing extends it; an expiry is no failure; the second expiry of an
// episode asks for a forward rescue (at most one rescue per bot per hour, idle rescues included).
// Pure rules, no game types, no locks, no config reads (S5A-DESIGN.md).

// ---- switch ---------------------------------------------------------------------------------------------------------
// AiPlayerbot.QuestFirstProgression.BandLadder: 0 = off, 1 = all bots, 2 = only cohort B (odd low guid), 3 = only
// cohort A (even low guid). Any other value acts like 0.
constexpr std::uint32_t ModeOff = 0;
constexpr std::uint32_t ModeAll = 1;
constexpr std::uint32_t ModeOdd = 2;
constexpr std::uint32_t ModeEven = 3;

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return mode == ModeAll || (mode == ModeOdd && cohort::InB(guidLow)) || (mode == ModeEven && !cohort::InB(guidLow));
}

struct ScopeFacts
{
    bool applies = false;       // Applies(mode, guid)
    bool rosterMember = false;  // persistent roster
    bool questFirst = false;    // AiPlayerbot.QuestFirstProgression.Enabled
    bool realMaster = false;
    bool grouped = false;
    std::uint32_t level = 0;
    std::uint32_t maxLevel = 0;
};

inline bool InScope(ScopeFacts const& f)
{
    return f.applies && f.rosterMember && f.questFirst && !f.realMaster && !f.grouped && f.maxLevel && f.level < f.maxLevel;
}

// ---- ladder rows ----------------------------------------------------------------------------------------------------
enum class Route : std::uint8_t { First, Walk, Flight };

// Keys of hub-ladder.tsv: "1".."10" (race, level 1-12) and "A0","A1","H0","H1" (faction + continent, level 13+).
constexpr std::uint32_t KeyCount = 15;   // index 0 = none, 1..10 races, 11 A0, 12 A1, 13 H0, 14 H1
constexpr std::uint32_t RaceKeyMaxLevel = 12;
constexpr std::uint32_t MaxLevelBits = 64;

inline std::uint32_t KeyIndex(std::string const& key)
{
    if (key.size() == 2 && (key[0] == 'A' || key[0] == 'H') && (key[1] == '0' || key[1] == '1'))
        return 11 + (key[0] == 'H' ? 2 : 0) + std::uint32_t(key[1] - '0');
    if (key.empty() || key.size() > 2 || key.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    std::uint32_t const race = std::uint32_t(std::atoi(key.c_str()));
    return (race >= 1 && race <= 10 && key[0] != '0') ? race : 0;
}

inline bool IsRaceKey(std::uint32_t keyIndex) { return keyIndex >= 1 && keyIndex <= 10; }

// The key for a bot of this race / faction ('A'/'H') on this map at this level; 0 = no key (other maps: S6).
inline std::uint32_t KeyFor(std::uint32_t race, char faction, std::uint32_t mapId, std::uint32_t level)
{
    if (level <= RaceKeyMaxLevel)
        return (race >= 1 && race <= 10) ? race : 0;
    if (mapId > 1 || (faction != 'A' && faction != 'H'))
        return 0;
    return 11 + (faction == 'H' ? 2 : 0) + mapId;
}

struct Rung
{
    std::uint32_t keyIndex = 0;
    std::uint32_t wave0 = 0;
    std::uint32_t minLevel = 0;
    std::uint32_t maxLevel = 0;
    std::uint32_t rank = 0;
    char faction = 0;
    std::uint32_t hubId = 0;
    std::uint32_t mapId = 0;
    std::uint32_t zoneId = 0;
    float x = 0.f, y = 0.f, z = 0.f;
    Route route = Route::Walk;
    std::uint16_t raceMask = 0xFFFF;    // first faction band: races whose from_l12 leg is walk
    std::string key;                    // for logs
    std::string hubName;                // for logs
};

// ---- header ---------------------------------------------------------------------------------------------------------
enum Column { ColStartKey, ColWave0, ColFaction, ColContinent, ColMinLevel, ColMaxLevel, ColRank, ColHubId, ColHubName,
    ColMapId, ColZoneId, ColX, ColY, ColZ, ColHubKind, ColGiverCount, ColRoute, ColTransportIds, ColReliability,
    ColEvidence, ColNextHubId, ColumnCount };

struct Header
{
    int col[ColumnCount];
    std::uint32_t fields = 0;
};

inline std::vector<std::string> SplitTabs(std::string const& line)
{
    std::vector<std::string> out;
    std::string::size_type start = 0;
    for (;;)
    {
        std::string::size_type const tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos)
            break;
        start = tab + 1;
    }
    if (!out.empty() && !out.back().empty() && out.back().back() == '\r')
        out.back().pop_back();
    return out;
}

// Columns are found by name, extra columns are ignored; false when one of the 21 rev2 names is missing.
inline bool ParseHeader(std::string const& line, Header& header)
{
    static char const* const names[ColumnCount] = { "start_key", "wave0", "faction", "continent", "min_level",
        "max_level", "rank", "hub_id", "hub_name", "map_id", "zone_id", "x", "y", "z", "hub_kind", "giver_count",
        "route_from_previous", "transport_ids", "reliability", "evidence", "next_hub_id" };
    std::vector<std::string> const fields = SplitTabs(line);
    header.fields = std::uint32_t(fields.size());
    for (int c = 0; c < ColumnCount; ++c)
    {
        header.col[c] = -1;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (fields[i] == names[c])
            {
                header.col[c] = int(i);
                break;
            }
        if (header.col[c] < 0)
            return false;
    }
    return true;
}

// ---- rows -----------------------------------------------------------------------------------------------------------
enum class RowVerdict : std::uint8_t { Use, SkipParse, SkipReliability, SkipWave0, SkipRoute, SkipFromL12,
    DegradedTransport, DegradedNoTaxiCheat };

inline char const* RowVerdictName(RowVerdict v)
{
    switch (v)
    {
        case RowVerdict::Use: return "use";
        case RowVerdict::SkipParse: return "parse";
        case RowVerdict::SkipReliability: return "reliability";
        case RowVerdict::SkipWave0: return "wave0";
        case RowVerdict::SkipRoute: return "route";
        case RowVerdict::SkipFromL12: return "from_l12";
        case RowVerdict::DegradedTransport: return "transport_ids";
        case RowVerdict::DegradedNoTaxiCheat: return "no_taxi_cheat";
    }
    return "?";
}

inline bool ParseUint(std::string const& s, std::uint32_t& out)
{
    if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    out = std::uint32_t(std::strtoul(s.c_str(), nullptr, 10));
    return true;
}

inline bool ParseFloat(std::string const& s, float& out)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    double const v = std::strtod(s.c_str(), &end);
    if (!end || *end || !std::isfinite(v) || std::fabs(v) > 100000.0)
        return false;
    out = float(v);
    return true;
}

// The value of `name=` in the evidence column ("a=1; from_l12=1:1194>walk,3:478>flight; ..."); "" when absent.
inline std::string EvidenceValue(std::string const& evidence, std::string const& name)
{
    std::string::size_type pos = 0;
    while ((pos = evidence.find(name + "=", pos)) != std::string::npos)
    {
        if (pos == 0 || evidence[pos - 1] == ' ' || evidence[pos - 1] == ';')
        {
            std::string::size_type const start = pos + name.size() + 1;
            std::string::size_type const end = evidence.find(';', start);
            return evidence.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        pos += name.size();
    }
    return std::string();
}

// Races whose leg into a first faction band ("<race>:<hub>><class>,...", CLI-PATH 19.10.2026) is a walk, or a flight when
// the bots may use taxis. Races 9/10 read from_l12_w0 when custom race start zones are on, from_l12 otherwise. A race
// missing from the list, and any other class or a suffixed one ("none(pred_unusable)", "flight(r1_unusable)"), is not
// allowed (fail closed).
inline std::uint16_t FromL12Mask(std::string const& evidence, bool crsz, bool allowFlight)
{
    std::uint16_t mask = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        std::string const list = EvidenceValue(evidence, pass ? "from_l12_w0" : "from_l12");
        std::string::size_type start = 0;
        while (start < list.size())
        {
            std::string::size_type const comma = list.find(',', start);
            std::string const item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            start = comma == std::string::npos ? list.size() : comma + 1;
            std::string::size_type const colon = item.find(':');
            std::string::size_type const arrow = item.find('>');
            std::uint32_t race = 0;
            if (colon == std::string::npos || arrow == std::string::npos || !ParseUint(item.substr(0, colon), race) ||
                race < 1 || race > 15)
                continue;
            bool const customRace = race == 9 || race == 10;
            if ((pass == 1) != (customRace && crsz))
                continue;
            std::string const cls = item.substr(arrow + 1);
            if (cls == "walk" || (allowFlight && cls == "flight"))
                mask = std::uint16_t(mask | (1u << race));
        }
    }
    return mask;
}

// Transport ids of a flight row: TaxiPath.dbc path ids (hub-ladder INTERFACE rev2 point 3, CLI-PATH 19.10.2026), bare or
// typed "tp:N". Anything else ("-", "go:", text) fails closed; the loader checks each id against the server's taxi paths.
inline bool FlightTaxiPaths(std::string const& ids, std::vector<std::uint32_t>& taxiPaths)
{
    taxiPaths.clear();
    if (ids.empty() || ids == "-")
        return false;
    std::string::size_type start = 0;
    while (start <= ids.size())
    {
        std::string::size_type const comma = ids.find(',', start);
        std::string const item = ids.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        std::uint32_t id = 0;
        std::string const num = item.compare(0, 3, "tp:") == 0 ? item.substr(3) : item;
        if (!ParseUint(num, id) || !id)
            return false;
        taxiPaths.push_back(id);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return !taxiPaths.empty();
}

// One data row -> verdict (priority order of S5A-DESIGN.md §3.2 step 6, pure part; duplicates, nodes, zones and taxi
// paths are checked by the loader). `rung` is filled from SkipReliability on. allowFlight = the roster bots' cheat mask has
// the taxi bit.
inline RowVerdict ParseRow(std::vector<std::string> const& fields, Header const& h, bool crsz, bool allowFlight, Rung& rung,
    std::vector<std::uint32_t>& taxiPaths)
{
    if (fields.size() != h.fields)
        return RowVerdict::SkipParse;
    auto f = [&](Column c) -> std::string const& { return fields[std::size_t(h.col[c])]; };
    rung = Rung();
    rung.key = f(ColStartKey);
    rung.keyIndex = KeyIndex(rung.key);
    rung.hubName = f(ColHubName);
    std::string const& faction = f(ColFaction);
    if (!rung.keyIndex || faction.size() != 1 || (faction[0] != 'A' && faction[0] != 'H') ||
        !ParseUint(f(ColWave0), rung.wave0) || !ParseUint(f(ColMinLevel), rung.minLevel) ||
        !ParseUint(f(ColMaxLevel), rung.maxLevel) || !ParseUint(f(ColRank), rung.rank) ||
        !ParseUint(f(ColHubId), rung.hubId) || !ParseUint(f(ColMapId), rung.mapId) ||
        !ParseUint(f(ColZoneId), rung.zoneId) || !ParseFloat(f(ColX), rung.x) || !ParseFloat(f(ColY), rung.y) ||
        !ParseFloat(f(ColZ), rung.z))
        return RowVerdict::SkipParse;
    rung.faction = faction[0];
    if (!rung.minLevel || rung.minLevel > rung.maxLevel || rung.maxLevel >= MaxLevelBits || rung.rank < 1 ||
        rung.rank > 2 || !rung.zoneId)
        return RowVerdict::SkipParse;
    // a race key covers levels 1-12, a faction key 13+ (its hubs may lie on another map: other_map at choice time)
    bool const raceKey = IsRaceKey(rung.keyIndex);
    if (raceKey ? rung.maxLevel > RaceKeyMaxLevel : rung.minLevel <= RaceKeyMaxLevel)
        return RowVerdict::SkipParse;
    if (f(ColReliability) != "ok")
        return RowVerdict::SkipReliability;
    bool const customRace = rung.keyIndex == 9 || rung.keyIndex == 10;
    if (rung.wave0 != (customRace && crsz ? 1u : 0u))
        return RowVerdict::SkipWave0;
    std::string const& route = f(ColRoute);
    bool const firstBand = rung.minLevel == (raceKey ? 1u : RaceKeyMaxLevel + 1);
    if (route == "-" && firstBand)
        rung.route = Route::First;
    else if (route == "walk")
        rung.route = Route::Walk;
    else if (route == "flight")
        rung.route = Route::Flight;
    else
        return RowVerdict::SkipRoute;
    if (rung.route == Route::First && !raceKey)
    {
        rung.raceMask = FromL12Mask(f(ColEvidence), crsz, allowFlight);
        if (!rung.raceMask)
            return RowVerdict::SkipFromL12;
    }
    if (rung.route == Route::Flight && !FlightTaxiPaths(f(ColTransportIds), taxiPaths))
        return RowVerdict::DegradedTransport;
    if (rung.route == Route::Flight && !allowFlight)
        return RowVerdict::DegradedNoTaxiCheat;
    return RowVerdict::Use;
}

// ---- ladder ---------------------------------------------------------------------------------------------------------
struct Ladder
{
    std::vector<Rung> rungs;                            // usable rows only
    std::uint64_t levelBits[KeyCount][16] = {};         // [key][race] bit L: a usable row of that key covers L

    void Add(Rung const& r)
    {
        rungs.push_back(r);
        for (std::uint32_t race = 0; race < 16; ++race)
            if (r.raceMask & (1u << race))
                for (std::uint32_t l = r.minLevel; l <= r.maxLevel && l < MaxLevelBits; ++l)
                    levelBits[r.keyIndex][race] |= std::uint64_t(1) << l;
    }

    // Duplicate of (key, wave0, min, rank) - the loader skips the later row.
    bool Has(Rung const& r) const
    {
        for (Rung const& o : rungs)
            if (o.keyIndex == r.keyIndex && o.wave0 == r.wave0 && o.minLevel == r.minLevel && o.rank == r.rank)
                return true;
        return false;
    }

    // The giver filter's gate (O(1)): the bot's key has a usable row for its level and race.
    bool Covers(std::uint32_t keyIndex, std::uint32_t race, std::uint32_t level) const
    {
        return keyIndex && keyIndex < KeyCount && race < 16 && level < MaxLevelBits &&
            (levelBits[keyIndex][race] >> level & 1u);
    }
};

// ---- trigger --------------------------------------------------------------------------------------------------------
struct StepFacts
{
    bool emptyOffer = false;        // Classify(true, lastRejects, offered, 0).failureClass == EmptyOffer
    std::uint32_t dangerMap = 0;    // lastRejects.dangerMap
    std::uint32_t offered = 0;      // turn-ins the request offered ("hand in quests")
    std::uint32_t openTurnIns = 0;  // complete quests in the log whose turn-in is not parked
    std::int32_t priorFailures = 0; // "quest route failures" before this search's count
    std::uint32_t freeSlots = 0;
    std::uint32_t slotGate = 0;     // 5 or 10 (TravelMgr quest-possible gate)
    std::uint32_t emptyStreak = 0;  // "band empty streak": quest searches in a row without a route, this one included
    std::uint32_t streakSteps = 0;  // AiPlayerbot.QuestFirstProgression.BandStepAfterEmptySearches (0 = off)
};

enum class StepVerdict : std::uint8_t { NotDue, SkipLogFull, Due };

// F1 (OB-00 23.10.2026, candidate v46.6): 71 % of cohort B's failed searches were transient (death_suppressed,
// quest_loop_suppressed), so EmptyOffer never held, and every structural park reset "quest route failures" - the band
// step fired for 5 of 88 bots while QFO left the backoff empty (idle + stuck 54 %). The streak counts every failed
// search, whatever its class; only a found route or a band step resets it.
constexpr char const* BandEmptyStreakValue = "band empty streak";     // "manual int" of the bot's AI
constexpr std::uint32_t MaxStreakSteps = 10;                            // config range of BandStepAfterEmptySearches

inline bool StreakDue(StepFacts const& f)
{
    return f.streakSteps && f.emptyStreak >= f.streakSteps;
}

// The step line's trigger=: the EmptyOffer rule wins when both hold (today's step stays today's step).
inline char const* StepTrigger(StepFacts const& f)
{
    return f.emptyOffer && f.priorFailures >= 1 ? "empty_offer" : "streak";
}

// The zone is done: no open turn-in, nothing dangerous, and either no acceptable quest (the second empty search in a row)
// or N failed searches in a row (F1).
inline StepVerdict BandStepDue(StepFacts const& f)
{
    bool const emptyDue = f.emptyOffer && f.priorFailures >= 1;
    if (!(emptyDue || StreakDue(f)) || f.dangerMap || f.offered || f.openTurnIns)
        return StepVerdict::NotDue;
    if (f.freeSlots < f.slotGate)
        return StepVerdict::SkipLogFull;
    return StepVerdict::Due;
}

// Travel adopts the accept rule (QuestAcceptPolicy.h) for quest givers: no grey and no red quest; class quests stay.
inline bool GreyGiverSkip(std::uint32_t level, std::uint32_t questLevel, std::uint32_t grayLevel, bool classQuest)
{
    return quest_accept::SkipForRosterBot(true, level, questLevel, grayLevel, classQuest);
}

// OB-10 Q1 19.10.2026: a giver trip that turns inactive because its quest is now in the log (or done) under the giver
// filter is a success - it expires at once (no 60 s cooldown per accepted quest) and counts as giver_done.
inline bool GiverTripDone(bool giverFilter, bool questStatusNone)
{
    return giverFilter && !questStatusNone;
}

// ---- rung choice ----------------------------------------------------------------------------------------------------
constexpr std::uint32_t LeadLevels = 2;         // the next band is a candidate from next.min - 2 on
constexpr float HubClusterRadius = 300.f;       // a hub's givers lie within this many yards of its point
constexpr std::uint32_t LeftZoneMemory = 2;
constexpr std::uint32_t MaxCandidates = 4;

enum class Reject : std::uint8_t { None, NoBand, RaceRoute, OtherMap, SameZone, LeftZone, InRadius, Suppressed,
    HubAbove };

inline char const* RejectName(Reject r)
{
    switch (r)
    {
        case Reject::None: return "none";
        case Reject::NoBand: return "no_band";
        case Reject::RaceRoute: return "race_route";
        case Reject::OtherMap: return "other_map";
        case Reject::SameZone: return "same_zone";
        case Reject::LeftZone: return "left_zone";
        case Reject::InRadius: return "in_radius";
        case Reject::Suppressed: return "suppressed";
        case Reject::HubAbove: return "hub_above";
    }
    return "?";
}

struct RungFacts
{
    std::uint32_t race = 0;
    char faction = 0;
    std::uint32_t mapId = 0;
    std::uint32_t level = 0;
    std::uint32_t zoneId = 0;                           // bot->GetZoneId(): top-level zone, never an area id
    std::uint32_t leftZones[LeftZoneMemory] = {};
    std::uint32_t leftBandMin = 0;                      // band (min level) the bot was in when it left them
    float x = 0.f, y = 0.f;
    float searchedRadius = 0.f;                         // GiverRadius(level, stage of the failed search)
    std::uint32_t sameZoneLead = 0;                     // AiPlayerbot.QuestFirstProgression.BandLadderSameZoneNext (0 = off)
};

struct Candidate
{
    std::int32_t index = -1;                            // into Ladder::rungs
    Reject reject = Reject::None;
};

inline float Dist2D(float ax, float ay, float bx, float by)
{
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

// Min level of the bot's current band (0 = none) - the band memory of the left zones is tied to it.
inline std::uint32_t CurrentBandMin(Ladder const& ladder, std::uint32_t keyIndex, std::uint32_t level)
{
    for (Rung const& r : ladder.rungs)
        if (r.keyIndex == keyIndex && r.minLevel <= level && level <= r.maxLevel)
            return r.minLevel;
    return 0;
}

// Up to 4 candidates in order: current band rank 1, rank 2, next band rank 1, rank 2; each with its first reject.
inline std::vector<Candidate> NextRung(Ladder const& ladder, RungFacts const& f)
{
    std::vector<Candidate> out;
    std::uint32_t const key = KeyFor(f.race, f.faction, f.mapId, f.level);
    if (!key)
        return out;
    std::uint32_t const curMin = CurrentBandMin(ladder, key, f.level);
    std::uint32_t curMax = 0;
    for (Rung const& r : ladder.rungs)
        if (r.keyIndex == key && r.minLevel == curMin && curMin)
            curMax = std::max(curMax, r.maxLevel);
    // the next band: smallest min above the current band (or the level), under the key of that min
    std::uint32_t const bound = curMin ? curMax : f.level;
    std::uint32_t nextKey = KeyFor(f.race, f.faction, f.mapId, bound + 1);
    std::uint32_t nextMin = 0;
    for (int pass = 0; pass < 2 && !nextMin; ++pass)
    {
        for (Rung const& r : ladder.rungs)
            if (r.keyIndex == nextKey && r.minLevel > bound && (!nextMin || r.minLevel < nextMin))
                nextMin = r.minLevel;
        if (!nextMin && IsRaceKey(nextKey))
            nextKey = KeyFor(f.race, f.faction, f.mapId, RaceKeyMaxLevel + 1);
        else
            break;
    }
    std::uint32_t const nextMinAny = nextMin;
    if (nextMin && f.level + LeadLevels < nextMin)
        nextMin = 0;
    auto addBand = [&](std::uint32_t bandKey, std::uint32_t bandMin)
    {
        if (!bandMin)
            return;
        for (std::uint32_t rank = 1; rank <= 2 && out.size() < MaxCandidates; ++rank)
            for (std::size_t i = 0; i < ladder.rungs.size(); ++i)
            {
                Rung const& r = ladder.rungs[i];
                if (r.keyIndex != bandKey || r.minLevel != bandMin || r.rank != rank)
                    continue;
                Candidate c;
                c.index = std::int32_t(i);
                bool leftZone = false;
                for (std::uint32_t z : f.leftZones)
                    leftZone = leftZone || (z && z == r.zoneId);
                if (f.race >= 16 || !(r.raceMask & (1u << f.race)))
                    c.reject = Reject::RaceRoute;
                else if (r.mapId != f.mapId)
                    c.reject = Reject::OtherMap;
                else if (r.zoneId == f.zoneId)
                    c.reject = Reject::SameZone;
                else if (leftZone && f.leftBandMin == curMin)
                    c.reject = Reject::LeftZone;
                else if (f.searchedRadius > 0.f && Dist2D(f.x, f.y, r.x, r.y) + HubClusterRadius <= f.searchedRadius)
                    c.reject = Reject::InRadius;
                out.push_back(c);
                break;
            }
    };
    addBand(key, curMin);
    // F2 (BandLadderSameZoneNext, OB-00 23.10.2026): every rung of the current band lies in the zone the bot has just found
    // done (candidate v46.6: same_zone 391 of 600 no_rung skips) - the next band is a candidate from sameZoneLead levels below
    // its min instead of LeadLevels. The game-side veto on a hub clearly above the bot (QuestBand.cpp HubAbove) still applies.
    bool allSameZone = !out.empty();
    for (Candidate const& c : out)
        allSameZone = allSameZone && c.reject == Reject::SameZone;
    if (!nextMin && nextMinAny && f.sameZoneLead && allSameZone && f.level + f.sameZoneLead >= nextMinAny)
        nextMin = nextMinAny;
    addBand(nextKey, nextMin);
    return out;
}

// The first reject among the candidates, for skip reason=no_rung detail= (no_band when there was no candidate).
inline Reject FirstReject(std::vector<Candidate> const& candidates)
{
    for (Candidate const& c : candidates)
        if (c.reject != Reject::None)
            return c.reject;
    return Reject::NoBand;
}

// ---- trip clock -----------------------------------------------------------------------------------------------------
constexpr std::uint32_t MinTripSeconds = 300;
constexpr std::uint32_t MaxTripSeconds = 2700;
constexpr float ArrivalRadius = 50.f;

// 120 s plus three times the straight walk at 7 yd/s, clamped to 5-45 min (OB-00 Q5 19.10.2026: start value).
inline std::uint32_t BandTripSeconds(float dist2D)
{
    double const raw = 120.0 + 3.0 * double(std::max(0.f, dist2D)) / 7.0;
    return std::uint32_t(std::min<double>(MaxTripSeconds, std::max<double>(MinTripSeconds, raw)));
}

// TravelTarget::SetStatus of a band trip: READY / TRAVEL / WORK last exactly the time left, COOLDOWN at most that;
// 1 ms once ended; the time left is capped, so a clock step back fails closed. Status values are TravelStatus.
constexpr std::uint8_t StatusReady = 2;
constexpr std::uint8_t StatusTravel = 3;
constexpr std::uint8_t StatusWork = 4;
constexpr std::uint8_t StatusCooldown = 5;

inline std::uint32_t TripStatusMs(std::uint8_t status, std::uint32_t statusMs, std::uint32_t tripEnd, std::uint32_t now)
{
    if (status != StatusReady && status != StatusTravel && status != StatusWork && status != StatusCooldown)
        return statusMs;
    std::uint32_t const left = tripEnd > now ? std::min(tripEnd - now, MaxTripSeconds + 60) : 0;
    if (!left)
        return 1;
    if (status == StatusCooldown)
        return statusMs ? std::min(statusMs, left * 1000) : left * 1000;
    return left * 1000;
}

enum class TripVerdict : std::uint8_t { Running, Orphan, ScopeLost, ExpiredEnd, ExpiredCooldown, ClockShortened };

// OB-10 19.10.2026 (option b): a status clock that runs out well before the trip end was shortened from outside (SetExpireIn,
// e.g. the focus command): not the trip's own failure - it ends as replaced (detail=clock_shortened), counted on its own, no
// rescue count, no work timeout. Within this slack of the end it is the end itself (the ms timer runs ahead of time()).
constexpr std::uint32_t ClockSlackSeconds = 5;

struct TripFacts
{
    bool hubDestination = false;    // the target's destination is a band hub
    std::uint32_t tripEnd = 0;      // TravelTarget::bandTripEnd
    bool scopeOk = false;           // TripScopeOk(bot, rung)
    bool cooldown = false;          // status COOLDOWN
    bool clockRanOut = false;       // READY / TRAVEL / WORK whose status clock ran out (SetStatus gave it the time left)
    std::uint32_t now = 0;
};

// Precedence Orphan > ScopeLost > ExpiredEnd > ExpiredCooldown > ClockShortened (or the end within the slack) > Running.
inline TripVerdict CheckTrip(TripFacts const& f)
{
    if (f.hubDestination && !f.tripEnd)
        return TripVerdict::Orphan;
    if (!f.tripEnd)
        return TripVerdict::Running;    // no band trip: nothing to end
    if (!f.hubDestination || !f.scopeOk)
        return TripVerdict::ScopeLost;
    if (f.now >= f.tripEnd)
        return TripVerdict::ExpiredEnd;
    if (f.cooldown)
        return TripVerdict::ExpiredCooldown;
    if (f.clockRanOut)
        return f.tripEnd - f.now > ClockSlackSeconds ? TripVerdict::ClockShortened : TripVerdict::ExpiredEnd;
    return TripVerdict::Running;
}

// ---- rescue ---------------------------------------------------------------------------------------------------------
constexpr std::uint32_t RescueExpiries = 2;
constexpr std::uint32_t RescueWindowSeconds = 3600;     // OB-00 Q6: idle rescues count (max(lastBand, lastQuest))
constexpr std::uint32_t RescueHandOverSeconds = 600;    // a band rescue the world thread did not take by then drops

inline bool RescueDue(std::uint32_t expiries) { return expiries >= RescueExpiries; }

inline bool RescueAllowed(std::uint32_t lastRescue, std::uint32_t now)
{
    return !lastRescue || (now >= lastRescue && now - lastRescue >= RescueWindowSeconds);
}

inline std::uint64_t PackRescue(std::uint32_t rung, std::uint32_t requestedAt)
{
    return (std::uint64_t(requestedAt) << 32) | (std::uint64_t(rung) + 1);
}

// rung + 1 of a pending band rescue that is still fresh; 0 = none or dropped (`dropped` set when it was too old).
inline std::uint32_t UnpackRescue(std::uint64_t packed, std::uint32_t now, bool& dropped)
{
    dropped = false;
    if (!packed)
        return 0;
    std::uint32_t const at = std::uint32_t(packed >> 32);
    if (now < at || now - at > RescueHandOverSeconds)
    {
        dropped = true;
        return 0;
    }
    return std::uint32_t(packed & 0xFFFFFFFFu);
}

// ---- log lines ------------------------------------------------------------------------------------------------------
enum Event { EvStep, EvArrived, EvExpired, EvExpiredCooldown, EvRescueRequested, EvRescueLimit, EvRescueBlocked,
    EvRescue, EvSkipNoRung, EvSkipLogFull, EvScopeLost, EvReplaced, EvPark, EvOrphan, EvGiverDone, EvClockShortened, EventCount };

inline std::string FormatMinute(char cohort, std::uint32_t const (&c)[EventCount])
{
    static char const* const names[EventCount] = { "steps", "arrived", "expired", "expired_cooldown", "rescue_req",
        "rescue_limit", "rescue_blocked", "rescues", "skip_no_rung", "skip_log_full", "scope_lost", "replaced", "park",
        "orphan", "giver_done", "clock_shortened" };
    std::uint32_t sum = 0;
    for (std::uint32_t v : c)
        sum += v;
    if (!sum)
        return std::string();
    std::string out = std::string("[QuestBand] state=minute cohort=") + cohort;
    for (int i = 0; i < EventCount; ++i)
        out += std::string(" ") + names[i] + "=" + std::to_string(c[i]);
    return out;
}

inline char const* RouteName(Route r)
{
    return r == Route::First ? "first" : (r == Route::Walk ? "walk" : "flight");
}

struct LineHead
{
    std::uint32_t bot = 0, level = 0, fromZone = 0, toHub = 0;
    char cohort = 'A';
};

inline std::string Head(char const* state, LineHead const& h)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "[QuestBand] state=%s bot=%u level=%u cohort=%c from_zone=%u to_hub=%u", state,
        h.bot, h.level, h.cohort, h.fromZone, h.toHub);
    return buf;
}

inline std::string FormatStep(LineHead const& h, Rung const& r, float dist, std::uint32_t tripSeconds, float searchedR,
    std::int32_t failures)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), " to_zone=%u key=%s band=%u-%u rank=%u route=%s dist=%.0f trip_s=%u searched_r=%.0f failures=%d",
        r.zoneId, r.key.c_str(), r.minLevel, r.maxLevel, r.rank, RouteName(r.route), dist, tripSeconds, searchedR, failures);
    return Head("step", h) + buf;
}

inline std::string FormatArrived(LineHead const& h, std::uint32_t tookSeconds)
{
    return Head("arrived", h) + " took_s=" + std::to_string(tookSeconds);
}

inline std::string FormatExpired(LineHead const& h, bool cooldown, std::uint32_t expiries, float distLeft)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), " reason=%s expiries=%u dist_left=%.0f", cooldown ? "cooldown" : "end", expiries, distLeft);
    return Head("expired", h) + buf;
}

inline std::string FormatRescue(LineHead const& h, char const* result)
{
    return Head("rescue", h) + " result=" + result;
}

inline std::string FormatSkip(LineHead const& h, char const* reason, char const* detail)
{
    return Head("skip", h) + " reason=" + reason + " detail=" + detail;
}

// OB-10 Q3 19.10.2026: a log full of quests is skipped and counted, never cleared before S7 (group/elite quests together);
// the line shows how many of the held quests are group or elite quests.
inline std::string FormatSkipLogFull(LineHead const& h, std::uint32_t freeSlots, std::uint32_t group, std::uint32_t elite)
{
    return Head("skip", h) + " reason=log_full detail=free_slots:" + std::to_string(freeSlots) + " group=" +
        std::to_string(group) + " elite=" + std::to_string(elite);
}

// Loader lines (boot only).
inline std::string FormatLadderOff(char const* reason, std::string const& path)
{
    return std::string("[HubLadder] state=off reason=") + reason + " path=" + path;
}

inline std::string FormatRowSkipped(bool degraded, std::uint32_t line, Rung const& r, char const* reason)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[HubLadder] state=%s line=%u key=%s wave0=%u band=%u-%u rank=%u hub=%u reason=%s",
        degraded ? "row_degraded" : "row_skipped", line, r.key.c_str(), r.wave0, r.minLevel, r.maxLevel, r.rank, r.hubId, reason);
    return buf;
}

struct LoadCounts
{
    std::uint32_t bytes = 0, fnv = 0, rows = 0, usable = 0, skipReliability = 0, skipRoute = 0, skipFromL12 = 0,
        skipWave0 = 0, degraded = 0, skipNode = 0, skipZone = 0, skipParse = 0, skipDup = 0, crsz = 0, mode = 0;
};

inline std::string FormatLoaded(std::string const& path, LoadCounts const& c)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf), "[HubLadder] state=loaded path=%s version=1 bytes=%u fnv=%08x rows=%u usable=%u "
        "skip_reliability=%u skip_route=%u skip_from_l12=%u skip_wave0=%u degraded=%u skip_node=%u skip_zone=%u "
        "skip_parse=%u skip_dup=%u crsz=%u mode=%u", path.c_str(), c.bytes, c.fnv, c.rows, c.usable, c.skipReliability,
        c.skipRoute, c.skipFromL12, c.skipWave0, c.degraded, c.skipNode, c.skipZone, c.skipParse, c.skipDup, c.crsz, c.mode);
    return buf;
}

// FNV-1a 32 of the file bytes (the boot proof of which ladder an arm ran with).
inline std::uint32_t Fnv1a(std::string const& bytes, std::uint32_t h = 2166136261u)
{
    for (unsigned char ch : bytes)
        h = (h ^ ch) * 16777619u;
    return h;
}
}
