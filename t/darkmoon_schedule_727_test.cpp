// twow-repo#727 (WS30-DMF-WEEKLY-01): the weekly Darkmoon Faire table.
// Mon setup, Tue-Fri open, Sat teardown, Sun none; Mon 30.11.2026 starts a Mulgore week,
// then the location alternates every week, also across the turn of the year (UTC days).
#include "../src/game/DarkmoonSchedule.h"

#include <cstdint>
#include <iostream>

namespace
{
    using DarkmoonSchedule::Phase;

    int failures = 0;

    char const* Name(Phase p)
    {
        switch (p)
        {
            case Phase::None: return "none";
            case Phase::Setup: return "setup";
            case Phase::Open: return "open";
            case Phase::Teardown: return "teardown";
        }
        return "?";
    }

    void Expect(int64_t t, bool mulgore, Phase phase, char const* label)
    {
        auto const s = DarkmoonSchedule::At(t);
        if (s.mulgore != mulgore || s.phase != phase)
        {
            std::cerr << label << " (" << t << "): got " << (s.mulgore ? "Mulgore" : "Elwynn") << "/" << Name(s.phase)
                      << ", expected " << (mulgore ? "Mulgore" : "Elwynn") << "/" << Name(phase) << "\n";
            ++failures;
        }
    }

    void ExpectU(uint32_t actual, uint32_t expected, char const* label)
    {
        if (actual != expected)
        {
            std::cerr << label << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        }
    }

    constexpr int64_t D = DarkmoonSchedule::SecondsPerDay;
    constexpr int64_t Mon30Nov2026 = 1795996800; // 2026-11-30 00:00:00 UTC

    // One full week starting at a Monday: the phase table, the location fixed for the week.
    void ExpectWeek(int64_t monday, bool mulgore, char const* label)
    {
        Phase const table[7] = { Phase::Setup, Phase::Open, Phase::Open, Phase::Open, Phase::Open,
                                 Phase::Teardown, Phase::None };
        for (int i = 0; i < 7; ++i)
        {
            Expect(monday + i * D, mulgore, table[i], label);             // 00:00:00
            Expect(monday + i * D + D / 2, mulgore, table[i], label);     // 12:00:00
            Expect(monday + i * D + D - 1, mulgore, table[i], label);     // 23:59:59
        }
    }
}

int main()
{
    static_assert(Mon30Nov2026 / D == DarkmoonSchedule::ReferenceMondayDay, "reference Monday");
    static_assert(DarkmoonSchedule::Weekday(Mon30Nov2026) == 0, "30.11.2026 is a Monday");
    static_assert(DarkmoonSchedule::Weekday(0) == 3, "1970-01-01 is a Thursday");

    ExpectWeek(Mon30Nov2026, true, "week 0 (30.11.2026) Mulgore");
    ExpectWeek(Mon30Nov2026 + 7 * D, false, "week 1 (07.12.2026) Elwynn");
    ExpectWeek(Mon30Nov2026 + 14 * D, true, "week 2 (14.12.2026) Mulgore");
    ExpectWeek(Mon30Nov2026 + 21 * D, false, "week 3 (21.12.2026) Elwynn");
    // Turn of the year: 28.12.2026 Mulgore -> 04.01.2027 Elwynn -> 11.01.2027 Mulgore (no repeat).
    ExpectWeek(1798416000, true, "week 4 (28.12.2026) Mulgore");
    ExpectWeek(1799020800, false, "week 5 (04.01.2027) Elwynn");
    ExpectWeek(1799625600, true, "week 6 (11.01.2027) Mulgore");
    // Before the reference the rhythm continues backwards (floor division).
    ExpectWeek(1795392000, false, "week -1 (23.11.2026) Elwynn");
    ExpectWeek(Mon30Nov2026 - 14 * D, true, "week -2 (16.11.2026) Mulgore");

    // Strict alternation over ten years, Monday by Monday.
    for (int w = -260; w < 260; ++w)
    {
        auto const a = DarkmoonSchedule::At(Mon30Nov2026 + w * 7 * D + D / 2);
        auto const b = DarkmoonSchedule::At(Mon30Nov2026 + (w + 1) * 7 * D + D / 2);
        if (a.mulgore == b.mulgore || a.phase != Phase::Setup)
        {
            std::cerr << "alternation broken at week " << w << "\n";
            ++failures;
            break;
        }
    }

    ExpectU(DarkmoonSchedule::SecondsUntilNextDay(Mon30Nov2026), D + 1, "delay at 00:00:00");
    ExpectU(DarkmoonSchedule::SecondsUntilNextDay(Mon30Nov2026 + D - 1), 2, "delay at 23:59:59");
    ExpectU(DarkmoonSchedule::SecondsUntilNextDay(Mon30Nov2026 + D / 2), D / 2 + 1, "delay at 12:00:00");

    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "darkmoon_schedule_727_test: OK\n";
    return 0;
}
