#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ai::rescue_hub
{
// twow-repo#575 RG-a (AiPlayerbot.QuestRescue.HomeHubs, OB-00 08.11.2026 decision A with widening; design
// ws-10/cli-pathfinding/a3-step4-route/DESIGN-RG-A-HOMEHUBS-20261108.md; finding FINDING-RESCUE-CONTINENT-TRAP-20261108.md d215404a):
// the quest rescue's first answer is an anchor table of (race, level band) rows. Pure rules, no game types, no config reads, no
// logging.
//
// Why: QuestRescueTargets had no target for (1) every gated bot below L5 inside its start zone (R1 StartZoneMinLevel: result=none)
// and (2) night elves / undead L5-10 (only other races' start points, all erased by E6 + Z1a ZoneExit.RescueHomeContinent).
// own_hub keeps a bot below L5 in its start zone (owner start-zone rule 04.11.: leave at L5-7, never below L5); next_zone takes
// night elf / undead from L5 to the first hub of the next zone on the home continent.
//
// Anchors: OB-50 table ws-00/projekt-durchleuchtung/ob50-anker-spawns (SUMS 7c52ff88, pin 2k, entry + guid + map checked against the
// DB). Innkeepers only: the start valleys and the hubs (Goldshire, Kharanos, Dolanaar, Brill, Razor Hill, Bloodhoof) have no flight
// master. Entries >= 50000 / spawn guids >= 2.5 M are taken as Turtle additions (OB-50's assumption, no vanilla DB to compare).
// High elf (OB-00 08.11.): own hub up to L9, no next-zone row (no 5-10 hub on the Eastern Kingdoms; no EPL anchor) - from L10
// the legacy rules apply. A data row, not a race special case, so the owner's band table (Z1) can replace the table later.

// Cohort mode (CohortPolicy.h): 0 off, 1 all, 2 cohort B, 3 cohort A; anything else is off.
inline std::uint32_t ClampMode(int value)
{
    return value >= 0 && value <= 3 ? std::uint32_t(value) : 0u;
}

enum class Step : std::uint8_t
{
    Legacy = 0,   // no row: the unchanged legacy targets
    OwnHub,       // the race's start-zone hub (stays in the start zone)
    NextZone,     // the first hub of the next zone on the home continent (leaves the start zone: R1 may gate it)
    Count
};

inline char const* StepName(Step s)
{
    switch (s)
    {
        case Step::Legacy: return "legacy";
        case Step::OwnHub: return "own_hub";
        case Step::NextZone: return "next_zone";
        default: return "unknown";
    }
}

constexpr std::size_t MaxEntries = 2;

struct Row
{
    std::uint32_t race;
    std::uint32_t minLevel;
    std::uint32_t maxLevel;
    Step step;
    float minDistance;                                // a spawn closer than this to the bot is not a rescue
    std::array<std::uint32_t, MaxEntries> entries;    // creature entries, 0 = unused
};

constexpr float OwnHubMinDistance = 100.0f;       // the hub lies inside the start zone: the 1000 yd legacy distance would drop it
constexpr float NextZoneMinDistance = 1000.0f;    // = quest_search::RescueMinDistance

// OB-50 anker-pro-volk.tsv (SUMS 7c52ff88): entry / spawn guid / place.
constexpr std::array<Row, 11> Table{{
    { 1, 1, 4, Step::OwnHub, OwnHubMinDistance, { 295, 0 } },         // human: Innkeeper Farley 2556037, Goldshire
    { 3, 1, 4, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },        // dwarf: Innkeeper Belm 199, Kharanos
    { 7, 1, 4, Step::OwnHub, OwnHubMinDistance, { 1247, 0 } },        // gnome: same start point as the dwarf
    { 4, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6736, 0 } },        // night elf: Innkeeper Keldamyr 46343, Dolanaar
    { 4, 5, 10, Step::NextZone, NextZoneMinDistance, { 6737, 0 } },   // night elf: Innkeeper Shaussiy 37069, Auberdine
    { 2, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },        // orc: Innkeeper Grosk 7671, Razor Hill
    { 8, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6928, 0 } },        // troll: same start point as the orc
    { 5, 1, 4, Step::OwnHub, OwnHubMinDistance, { 5688, 0 } },        // undead: Innkeeper Renee 28474, Brill
    { 5, 5, 10, Step::NextZone, NextZoneMinDistance, { 6739, 0 } },   // undead: Innkeeper Bates 17865, The Sepulcher
    { 6, 1, 4, Step::OwnHub, OwnHubMinDistance, { 6747, 0 } },        // tauren: Innkeeper Kauth 24774, Bloodhoof Village
    { 10, 1, 9, Step::OwnHub, OwnHubMinDistance, { 80227, 61831 } },  // high elf: Ashanya 2580824 (Brinthilien), Tiriel 2581602 (Alah'Thalas)
}};

constexpr std::uint32_t HighElfRace = 10;   // = ai::start_zone::HighElfRace (StartZonePolicy.h)

// The row for a bot, or nullptr (= legacy). Goblins have no row: their island rules (CustomRaceStartZones) decide. The high-elf row
// needs CustomRaceStartZones (customStarts): without it high elves live at the proxy start Northshire and avoid the Thalassian
// Highlands, so the legacy targets apply (SZM adoption review 10.11.2026).
inline Row const* RowFor(std::uint32_t race, std::uint32_t level, bool customStarts)
{
    if (race == HighElfRace && !customStarts)
        return nullptr;
    for (Row const& row : Table)
        if (row.race == race && level >= row.minLevel && level <= row.maxLevel)
            return &row;
    return nullptr;
}

// A next_zone row leaves the start zone: while R1 (StartZoneMinLevel) gates the level for the bot, R1 decides instead.
inline bool Usable(Row const* row, bool startZoneGated)
{
    return row && !(row->step == Step::NextZone && startZoneGated);
}

// Realm-wide counters for the [RescueHubMin] minute line (relaxed atomics; sums over a window are exact).
struct Counters
{
    std::array<std::atomic<std::uint32_t>, std::size_t(Step::Count)> hub{};   // targets found, per step
    std::atomic<std::uint32_t> fallback{0};                                 // a row matched, no spawn left: legacy follows
    std::atomic<std::uint32_t> gated{0};                                    // a next_zone row held back by R1
    std::atomic<std::uint64_t> lastLogSeconds{0};

    void Hub(Step step)
    {
        if (step < Step::Count)
            hub[std::size_t(step)].fetch_add(1, std::memory_order_relaxed);
    }

    std::uint32_t TakeHub(Step step)
    {
        return hub[std::size_t(step)].exchange(0, std::memory_order_relaxed);
    }

    // True once per minute for one caller.
    bool LogDue(std::uint64_t nowSeconds)
    {
        std::uint64_t last = lastLogSeconds.load(std::memory_order_relaxed);
        if (nowSeconds < last + 60)
            return false;
        return lastLogSeconds.compare_exchange_strong(last, nowSeconds, std::memory_order_relaxed);
    }
};

inline Counters& SharedCounters()
{
    static Counters counters;
    return counters;
}
}
