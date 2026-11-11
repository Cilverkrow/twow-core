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
// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0), R10 (design
// A3-S3-T02-ENTWURF.md v0.4.3 section 7.3): a supervisor registers its bot once (its first GroupTick) and unregisters it in its
// destructor, by token, so a late teardown of an old instance never drops a newer one. Post opens a ring only for a registered
// guid (MailOpenAllowed); a post to an unregistered guid is dropped and counted as unreg. Register and unregister write no line
// and do not roll the minute. The minute line also comes every 10th minute while rings are open (MailGaugeDue), with the age of
// the oldest open ring.
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
std::unordered_map<std::uint32_t, std::uint32_t> g_registered;   // T02-GROUP: guid -> token of its newest supervisor (under g_mailMutex)
std::uint32_t g_nextToken = 0;                                   // T02-GROUP: the last token handed out (skips 0; under g_mailMutex)

struct MailMinuteOut
{
    bool due = false;
    MailCounters delta;
    std::uint32_t rings = 0;
    std::uint32_t regs = 0;        // T02-GROUP: registrations at the minute change (a level)
    std::uint32_t oldestSec = 0;   // T02-GROUP: age of the oldest open ring (a level; only in a due minute with open rings)
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
    out.rings = std::uint32_t(g_mail.size());
    out.due = MailGaugeDue(out.delta, out.rings, minute);
    out.regs = std::uint32_t(g_registered.size());
    if (out.due && out.rings > 0)
    {
        // One pass over at most kMailMaxRings rings, only in a due minute.
        std::uint32_t const nowSec = std::uint32_t(time(nullptr));
        for (auto const& entry : g_mail)
            if (nowSec > entry.second.openedSec && nowSec - entry.second.openedSec > out.oldestSec)
                out.oldestSec = nowSec - entry.second.openedSec;
    }
    return out;
}

void EmitMailMinute(MailMinuteOut const& minute)
{
    if (!minute.due)
        return;
    char buf[256];
    FormatMailMinute(buf, sizeof(buf), minute.delta, minute.rings, minute.regs, minute.oldestSec);
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
        if (it == g_mail.end() && MailOpenAllowed(g_registered.count(guidLow) != 0, guidLow, g_mail.size()))
        {
            it = g_mail.emplace(guidLow, MailRing()).first;
            it->second.openedSec = std::uint32_t(time(nullptr));
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
        // T02-GROUP: the part of dropped that went to a guid no supervisor registered (no ring could open for it).
        if (it == g_mail.end() && g_registered.count(guidLow) == 0)
            ++g_total.unreg;
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

// T02-GROUP R10: the newest instance of a guid wins (an older entry is overwritten). Guid 0 is never registered.
std::uint32_t RegisterMail(std::uint32_t guidLow)
{
    std::lock_guard<std::mutex> lock(g_mailMutex);
    if (guidLow == 0)
        return 0;
    if (++g_nextToken == 0)
        ++g_nextToken;
    g_registered[guidLow] = g_nextToken;
    return g_nextToken;
}

// T02-GROUP R10: only while the guid still holds this token - a later instance of the same guid keeps its registration and its
// ring. The ring's rest counts as lost (as in Forget). A supervisor that never ran its first GroupTick (token 0) holds nothing.
void UnregisterMail(std::uint32_t guidLow, std::uint32_t token)
{
    if (token == 0)
        return;
    std::lock_guard<std::mutex> lock(g_mailMutex);
    auto const reg = g_registered.find(guidLow);
    if (reg == g_registered.end() || reg->second != token)
        return;
    g_registered.erase(reg);
    auto it = g_mail.find(guidLow);
    if (it != g_mail.end())
    {
        g_total.lost += it->second.count;
        g_pending.fetch_sub(it->second.count, std::memory_order_relaxed);
        g_mail.erase(it);
        g_ringsOpen.store(std::uint32_t(g_mail.size()), std::memory_order_relaxed);
    }
}
}
