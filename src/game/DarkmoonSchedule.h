#pragma once

// twow-repo#727 (WS30-DMF-WEEKLY-01, owner 20.11.2026): weekly Darkmoon Faire rhythm.
// The location alternates every week between Mulgore and Elwynn Forest; the week of
// Mon 30.11.2026 is Mulgore. Days follow server time UTC (owner):
//   Mon setup, Tue-Fri open, Sat teardown, Sun no faire anywhere.
// Pure arithmetic on the epoch second (no localtime/tm_yday), so the parity never repeats
// at the turn of the year and the result does not depend on the process time zone.
// Header-only and dependency-free so t/darkmoon_schedule_727_test.cpp can test it directly.

#include <cstdint>

namespace DarkmoonSchedule
{
    enum class Phase : uint8_t
    {
        None,     // Sunday: no faire anywhere
        Setup,    // Monday: setup event of the week's location
        Open,     // Tuesday-Friday: main event
        Teardown, // Saturday: setup event again (tents and wagons, no vendors or quest givers)
    };

    struct State
    {
        bool mulgore; // true = Mulgore (Horde), false = Elwynn Forest (Alliance)
        Phase phase;
    };

    constexpr int64_t SecondsPerDay = 86400;
    // Days since 1970-01-01 of Mon 30.11.2026 (1795996800 / 86400): the first Mulgore week.
    constexpr int64_t ReferenceMondayDay = 20787;

    constexpr int64_t FloorDiv(int64_t a, int64_t b)
    {
        return (a / b) - ((a % b != 0 && ((a < 0) != (b < 0))) ? 1 : 0);
    }

    constexpr int64_t FloorMod(int64_t a, int64_t b)
    {
        return a - FloorDiv(a, b) * b;
    }

    // 0 = Monday ... 6 = Sunday (1970-01-01 was a Thursday).
    constexpr int Weekday(int64_t epochSeconds)
    {
        return static_cast<int>(FloorMod(FloorDiv(epochSeconds, SecondsPerDay) + 3, 7));
    }

    constexpr State At(int64_t epochSeconds)
    {
        int64_t const day = FloorDiv(epochSeconds, SecondsPerDay);
        int64_t const week = FloorDiv(day - ReferenceMondayDay, 7);
        bool const mulgore = FloorMod(week, 2) == 0;

        Phase phase = Phase::Open;
        switch (Weekday(epochSeconds))
        {
            case 0: phase = Phase::Setup; break;
            case 5: phase = Phase::Teardown; break;
            case 6: phase = Phase::None; break;
            default: phase = Phase::Open; break;
        }
        return State{ mulgore, phase };
    }

    // Seconds until just after the next 00:00 UTC, so the day switch is not up to a day late.
    constexpr uint32_t SecondsUntilNextDay(int64_t epochSeconds)
    {
        return static_cast<uint32_t>(SecondsPerDay - FloorMod(epochSeconds, SecondsPerDay) + 1);
    }
}
