#pragma once

#include "CohortPolicy.h"
#include "QuestGrindModePolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::low_level_window
{
// AiPlayerbot.Grind.LowLevelWindow (OB-10 22.11.2026 build OK, trace research1/L10-WINDOW-LEVER-TRACE-20261122 9d36421e):
// a low-level quest-first bot's grind travel uses the GrindMode 1 level window [max(gray + 1, L - 5), L + 1]
// (QuestGrindModePolicy.h GapWindowLowLevel / GapWindowHighLevel, the template test of GrindTravelDestination::IsPossible and
// the area gate of IsLocationLevelValid) instead of the durability window, which is one level wide at L5 and empty below.
// The money test (MinLootGold > 0) stays. Only the window: none of GrindMode 1's other rules (R1 request gate, R2 trip clock,
// own map, R4 on-site filter) come with it. Pure rules, no game types, no locks, no config reads.
//
// The mode is the shared cohort mode (CohortPolicy.h): 0 off (default), 1 all bots, 2 cohort B (odd low guid), 3 cohort A
// (even low guid); any other value is 0 (the loader writes one error line).
constexpr std::uint32_t MaxLevel = 10;

inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

// The population: a persistent roster bot of QuestFirstProgression on its own (no real player master), level 1..MaxLevel,
// in the mode's cohort. A bot GrindMode 1 already gives the window (gapGrind) gets nothing from this switch - no change.
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow, bool rosterOnItsOwn, std::uint32_t level, bool gapGrind)
{
    return !gapGrind && rosterOnItsOwn && level >= 1u && level <= MaxLevel &&
        cohort::Applies(ValidMode(mode) ? mode : cohort::ModeOff, guidLow);
}

// The window of a bot this switch applies to: the same rule as GrindMode 1 (called, not copied).
inline std::uint32_t WindowLow(std::uint32_t level, std::uint32_t grayLevel)
{
    return quest_grind_mode::GapWindowLowLevel(level, grayLevel);
}

inline std::uint32_t WindowHigh(std::uint32_t level)
{
    return quest_grind_mode::GapWindowHighLevel(level);
}

// [LowLevelWindow] minute line, per cohort (a = even low guid, b = odd): the wiring proof of an A/B (treated > 0, control 0).
//   fetches  grind fetch results of bots the switch applies to (each one asked the window),
//   none     of them: no grind target chosen,
//   trips    of them: a window grind trip set as the bot's travel target.
enum class Event : std::uint8_t
{
    Fetches,
    None,
    Trips,
};
constexpr std::size_t EventCount = 3;
constexpr std::size_t SlotCount = 2 * EventCount;

inline std::size_t Slot(Event event, std::uint32_t guidLow)
{
    return std::size_t(event) * 2 + (cohort::InB(guidLow) ? 1 : 0);
}

inline std::string FormatMinuteLine(std::array<std::uint32_t, SlotCount> const& counts)
{
    bool any = false;
    for (std::uint32_t const count : counts)
        any = any || count != 0;
    if (!any)
        return std::string();
    auto const pair = [&counts](Event event, char const* name)
    {
        std::size_t const slot = std::size_t(event) * 2;
        return std::string(" ") + name + "_a=" + std::to_string(counts[slot]) + " " + name + "_b=" + std::to_string(counts[slot + 1]);
    };
    return "[LowLevelWindow] minute" + pair(Event::Fetches, "fetches") + pair(Event::None, "none") + pair(Event::Trips, "trips");
}
}
