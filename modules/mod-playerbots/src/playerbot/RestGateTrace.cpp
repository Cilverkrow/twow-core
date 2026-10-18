#include "playerbot/playerbot.h"
#include "playerbot/RestGateTrace.h"
#include "playerbot/RotCastPolicy.h"

#include <mutex>

// twow-repo#602 ROT-01 X1: per class and cohort counters of one hour, written on the bot threads under one short std::mutex (an episode
// ends at most once per fight), read and cleared by the World thread once an hour.
namespace
{
using ai::rest_gate::Counters;
using ai::rest_gate::End;

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

namespace ai::rest_gate_rt
{
void RecordEpisode(std::uint8_t cls, bool cohortB, End end, std::uint32_t durMs)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Counters& c = Slot(cls, cohortB);
    ++c.episodes;
    c.restMs += durMs;
    if (end == End::Done)
        ++c.done;
    else if (end == End::Combat)
        ++c.combat;
    else if (end == End::Death)
        ++c.death;
    else if (end == End::Stale)
        ++c.stale;
    else if (end == End::Timeout)
        ++c.timeout;
}

void RecordCastFail(std::uint8_t cls, bool cohortB)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++Slot(cls, cohortB).castFail;
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
                sLog.outBasic("%s", ai::rest_gate::FormatTotalLine(windowMin, b ? 'B' : 'A', cls, snapshot[b][cls], dropped).c_str());
}
}
