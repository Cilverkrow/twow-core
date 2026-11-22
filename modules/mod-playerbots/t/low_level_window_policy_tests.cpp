// AiPlayerbot.Grind.LowLevelWindow (OB-10 22.11.2026): the mode check, the population (cohort, roster on its own, level 1..10,
// no change for a GrindMode 1 gap filler), the window per level 1..10 = the GrindMode 1 window, and the per-cohort minute line
// (LowLevelWindowPolicy.h). Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "LowLevelWindowPolicy.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

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

// MaNGOS::XP::GetGrayLevel (Formulas.h:34-42) for the levels tested here, so the test needs no game header.
std::uint32_t Gray(std::uint32_t level)
{
    if (level <= 5)
        return 0;
    if (level <= 39)
        return level - 5 - level / 10;
    return level - 1 - level / 5;
}
}

int main()
{
    using namespace ai::low_level_window;

    // 1) The mode: 0..3 valid, the shared cohort mode; anything else is off.
    {
        Require(ValidMode(0) && ValidMode(1) && ValidMode(2) && ValidMode(3), "modes 0..3 valid");
        Require(!ValidMode(4) && !ValidMode(255) && !ValidMode(0xFFFFFFFFu), "4+ invalid");
        Require(MaxLevel == 10, "the window lever stops at level 10 (OB-10)");
    }

    // 2) The population: cohort mode, roster bot on its own, level 1..10, never a gap filler.
    {
        std::uint32_t const odd = 1001, even = 1002;
        Require(!Applies(0, odd, true, 5, false) && !Applies(0, even, true, 5, false), "mode 0: no bot");
        Require(Applies(1, odd, true, 5, false) && Applies(1, even, true, 5, false), "mode 1: every bot");
        Require(Applies(2, odd, true, 5, false) && !Applies(2, even, true, 5, false), "mode 2: cohort B (odd) only");
        Require(!Applies(3, odd, true, 5, false) && Applies(3, even, true, 5, false), "mode 3: cohort A (even) only");
        Require(!Applies(4, odd, true, 5, false) && !Applies(4, even, true, 5, false), "an unknown mode acts like 0");
        Require(!Applies(1, odd, false, 5, false), "not a roster bot on its own (no QuestFirstProgression / a real master): off");
        Require(!Applies(1, odd, true, 5, true) && !Applies(2, odd, true, 5, true), "a GrindMode 1 gap filler: no change");
        Require(!Applies(1, odd, true, 0, false), "level 0: off");
        for (std::uint32_t level = 1; level <= 10; ++level)
            Require(Applies(1, odd, true, level, false), "levels 1..10: on");
        Require(!Applies(1, odd, true, 11, false) && !Applies(1, odd, true, 60, false), "above level 10: the old logic");
        for (std::uint32_t mode = 0; mode <= 5; ++mode)
            for (std::uint32_t guid = 1; guid <= 8; ++guid)
                Require(Applies(mode, guid, true, 5, false) == ai::cohort::Applies(mode <= 3 ? mode : 0, guid),
                    "the same cohort as the shared rule (QuestBook, escort) for every mode / guid");
    }

    // 3) The window per level 1..10: the GrindMode 1 window [max(gray + 1, L - 5 or 1), L + 1] (called, not copied).
    {
        std::uint32_t const expectLow[11] = { 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 5 };
        for (std::uint32_t level = 1; level <= 10; ++level)
        {
            Require(WindowLow(level, Gray(level)) == expectLow[level], "window low per level (L1-5: 1; L6 2 .. L10 5)");
            Require(WindowHigh(level) == level + 1, "window high L + 1");
            Require(WindowLow(level, Gray(level)) == ai::quest_grind_mode::GapWindowLowLevel(level, Gray(level)) &&
                WindowHigh(level) == ai::quest_grind_mode::GapWindowHighLevel(level), "the same window as GrindMode 1");
            Require(WindowLow(level, Gray(level)) <= WindowHigh(level), "never an empty window at L1-10");
            Require(WindowLow(level, Gray(level)) > Gray(level), "never a grey creature level in the window");
            Require(ai::quest_grind_mode::InGapWindow(WindowLow(level, Gray(level)), WindowHigh(level), level),
                "the bot's own level is inside");
        }
        // L5: [1, 6] instead of the durability window [3, 3] (trace 9d36421e).
        Require(WindowLow(5, Gray(5)) == 1 && WindowHigh(5) == 6, "L5 window [1, 6]");
    }

    // 4) The minute line: per cohort, omitted when every count is 0.
    {
        std::array<std::uint32_t, SlotCount> counts = {};
        Require(FormatMinuteLine(counts).empty(), "all 0: no line");
        Require(Slot(Event::Fetches, 2) == 0 && Slot(Event::Fetches, 1) == 1, "fetches: a = even, b = odd");
        Require(Slot(Event::None, 2) == 2 && Slot(Event::None, 1) == 3, "none slots");
        Require(Slot(Event::Trips, 2) == 4 && Slot(Event::Trips, 1) == 5, "trips slots");
        counts[Slot(Event::Fetches, 1)] = 3;
        counts[Slot(Event::Trips, 1)] = 2;
        counts[Slot(Event::None, 1)] = 1;
        Require(FormatMinuteLine(counts) ==
            "[LowLevelWindow] minute fetches_a=0 fetches_b=3 none_a=0 none_b=1 trips_a=0 trips_b=2", "line format");
        std::array<std::uint32_t, SlotCount> one = {};
        one[SlotCount - 1] = 1;
        Require(!FormatMinuteLine(one).empty(), "the last slot alone gives a line");
    }

    std::cout << "low_level_window_policy_tests: all passed\n";
    return 0;
}
