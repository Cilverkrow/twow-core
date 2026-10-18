#include "playerbot/playerbot.h"
#include "playerbot/EmergencyLadderTrace.h"
#include "playerbot/RotCastPolicy.h"

#include <mutex>

// twow-repo#602 ROT-01 X3: per class and cohort counters of one hour, written on the bot threads under one short std::mutex (events only
// happen when a solo bot is below critical health or picks a solo CC target), read and cleared by the World thread once an hour.
namespace
{
using ai::emergency_ladder::Choice;
using ai::emergency_ladder::Counters;

constexpr std::uint32_t LinesPerMinute = 300;
constexpr std::uint32_t MaxClass = 12;

std::mutex g_mutex;
Counters g_counters[2][MaxClass];   // [cohort B][class]
std::uint32_t g_window = 0, g_used = 0, g_dropped = 0;

Counters& Slot(std::uint8_t cls, bool cohortB)
{
    return g_counters[cohortB ? 1 : 0][cls < MaxClass ? cls : 0];
}
}

namespace ai::emergency_ladder_rt
{
void RecordEpisode(std::uint8_t cls, bool cohortB, Choice choice)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Counters& c = Slot(cls, cohortB);
    ++c.episodes;
    if (choice == Choice::Fight)
        ++c.fight;
    else if (choice == Choice::Escape)
        ++c.escape;
    else
        ++c.flee;
}

void RecordEscapeFallback(std::uint8_t cls, bool cohortB)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++Slot(cls, cohortB).escapeFallback;
}

void RecordFightAbort(std::uint8_t cls, bool cohortB)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++Slot(cls, cohortB).fightAbort;
}

void RecordSoloCc(std::uint8_t cls, bool cohortB)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++Slot(cls, cohortB).soloCc;
}

void RecordCcBroken(std::uint8_t cls, bool cohortB)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++Slot(cls, cohortB).ccBroken;
}

bool TakeLineBudget(std::uint32_t now)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ai::rot_cast::BudgetTake(g_window, g_used, now, LinesPerMinute))
        return true;
    ++g_dropped;
    return false;
}

void Report(bool firstCall, std::uint32_t now)
{
    static std::uint32_t lastReport = 0;

    if (firstCall)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& cohort : g_counters)
            for (Counters& c : cohort)
                c = Counters();
        g_dropped = 0;
        lastReport = now;
        return;
    }
    if (now < lastReport)
    {
        lastReport = now;   // the wall clock went back: start a new window
        return;
    }
    if (now < lastReport + 3600)
        return;
    std::uint32_t const windowMin = (now - lastReport) / 60;
    lastReport = now;

    Counters snapshot[2][MaxClass];
    std::uint32_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int b = 0; b < 2; ++b)
            for (std::uint32_t cls = 0; cls < MaxClass; ++cls)
            {
                snapshot[b][cls] = g_counters[b][cls];
                g_counters[b][cls] = Counters();
            }
        dropped = g_dropped;
        g_dropped = 0;
    }
    for (int b = 0; b < 2; ++b)
        for (std::uint32_t cls = 0; cls < MaxClass; ++cls)
            if (!snapshot[b][cls].Empty())
                sLog.outBasic("%s", ai::emergency_ladder::FormatTotalLine(windowMin, b ? 'B' : 'A', cls, snapshot[b][cls], dropped).c_str());
}
}
