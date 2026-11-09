#include "playerbot/playerbot.h"
#include "playerbot/Supervisor.h"
#include "playerbot/SupervisorMailPolicy.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <unordered_map>

// A3 step 3, mailbox pair (design A3-S3-ENTWURF section 1.10; agreed with CLI-GRP 09.11.2026): the module-wide mailbox declared in
// Supervisor.h. Other threads (the group manager on the world thread) post by low guid; only the bot's own supervisor drains, on
// the bot's map thread, once per pass while AiPlayerbot.Supervisor applies to it. One file-local mutex around the map of rings and
// the counters, held only while events are copied; the minute line is written outside it. At the defaults nothing posts, so
// Drain and Forget return after one atomic load and never take the lock. In this pair nothing acts on a drained event: GROUP mode
// and the travel-target write come with T02/T30, each behind its own switch and A/B.
namespace ai::supervisor
{
namespace
{
std::mutex g_mailMutex;
std::unordered_map<std::uint32_t, MailRing> g_mail;   // only guids with undelivered mail; an emptied ring is erased
std::atomic<std::uint32_t> g_pending{0};              // events in all rings (changed under g_mailMutex)
std::atomic<std::uint32_t> g_ringsOpen{0};            // g_mail.size() (changed under g_mailMutex)
MailCounters g_total;
MailCounters g_logged;
std::uint32_t g_minute = 0;

struct MailMinuteOut
{
    bool due = false;
    MailCounters delta;
    std::uint32_t rings = 0;
};

// Under g_mailMutex, before the call counts its own event: at the first call in a new minute, the increments since the last line.
MailMinuteOut RollLocked()
{
    MailMinuteOut out;
    std::uint32_t const minute = std::uint32_t(time(nullptr) / 60);
    if (minute == g_minute)
        return out;
    g_minute = minute;
    out.delta = MailDelta(g_total, g_logged);
    g_logged = g_total;
    out.due = !MailSilent(out.delta);
    out.rings = std::uint32_t(g_mail.size());
    return out;
}

void EmitMailMinute(MailMinuteOut const& minute)
{
    if (!minute.due)
        return;
    char buf[256];
    FormatMailMinute(buf, sizeof(buf), minute.delta, minute.rings);
    sLog.outBasic("%s", buf);
}
}

bool Post(std::uint32_t guidLow, Event const& event)
{
    MailMinuteOut minute;
    bool posted = false;
    {
        std::lock_guard<std::mutex> lock(g_mailMutex);
        minute = RollLocked();
        auto it = g_mail.find(guidLow);
        if (it == g_mail.end() && guidLow != 0 && g_mail.size() < kMailMaxRings)
        {
            it = g_mail.emplace(guidLow, MailRing()).first;
            g_ringsOpen.store(std::uint32_t(g_mail.size()), std::memory_order_relaxed);
        }
        if (it != g_mail.end() && MailPush(it->second, event))
        {
            posted = true;
            ++g_total.posted;
            g_pending.fetch_add(1, std::memory_order_relaxed);
        }
        else
            ++g_total.dropped;
    }
    EmitMailMinute(minute);
    return posted;
}

bool MailWaiting()
{
    return g_pending.load(std::memory_order_relaxed) != 0;
}

std::size_t Drain(std::uint32_t guidLow, Event* out, std::size_t max)
{
    if (!out || max == 0 || !MailWaiting())
        return 0;
    MailMinuteOut minute;
    std::size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(g_mailMutex);
        minute = RollLocked();
        auto it = g_mail.find(guidLow);
        if (it != g_mail.end())
        {
            n = MailTake(it->second, out, max);
            if (it->second.count == 0)
            {
                g_mail.erase(it);
                g_ringsOpen.store(std::uint32_t(g_mail.size()), std::memory_order_relaxed);
            }
            g_pending.fetch_sub(std::uint32_t(n), std::memory_order_relaxed);
            g_total.drained += n;
            for (std::size_t i = 0; i < n; ++i)
                if (out[i].kind == EventKind::GroupDirective)
                    ++g_total.directive;
        }
    }
    EmitMailMinute(minute);
    return n;
}

void Forget(std::uint32_t guidLow)
{
    if (g_ringsOpen.load(std::memory_order_relaxed) == 0)
        return;
    MailMinuteOut minute;
    {
        std::lock_guard<std::mutex> lock(g_mailMutex);
        minute = RollLocked();
        auto it = g_mail.find(guidLow);
        if (it != g_mail.end())
        {
            g_total.lost += it->second.count;
            g_pending.fetch_sub(it->second.count, std::memory_order_relaxed);
            g_mail.erase(it);
            g_ringsOpen.store(std::uint32_t(g_mail.size()), std::memory_order_relaxed);
        }
    }
    EmitMailMinute(minute);
}
}
