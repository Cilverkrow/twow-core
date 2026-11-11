#pragma once

#include "SupervisorPolicy.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3, mailbox pair (design A3-S3-ENTWURF section 1.10; agreed with CLI-GRP 09.11.2026 for the group manager's directives,
// T30): the pure rules of the supervisor's mailbox. One fixed ring of kMailSlots events per bot (low guid). A full ring drops the
// NEW event and counts it; an old event is never overwritten and nobody waits. No game header, no config, no lock: the map of
// rings, its one lock and the minute line live in SupervisorMail.cpp. Unit test: t/supervisor_mail_policy_tests.cpp.
namespace ai::supervisor
{
constexpr std::size_t kMailSlots = 16;
// More guids than a realm has bots: a post that would open a ring beyond this is dropped (counted) instead.
constexpr std::size_t kMailMaxRings = 4096;

struct MailRing
{
    Event slots[kMailSlots];
    std::uint8_t head = 0;    // the oldest event
    std::uint8_t count = 0;
    std::uint32_t openedSec = 0;   // T02-GROUP: when Post opened the ring (the gauge's oldest_s)
};

// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0), R10: Post opens a ring only
// for a guid whose supervisor registered (RegisterMail in its first GroupTick), never for guid 0 and never beyond kMailMaxRings.
inline bool MailOpenAllowed(bool registered, std::uint32_t guidLow, std::size_t rings)
{
    return registered && guidLow != 0 && rings < kMailMaxRings;
}

// false = the ring is full: nothing changes, the caller counts the drop.
inline bool MailPush(MailRing& ring, Event const& event)
{
    if (ring.count >= kMailSlots)
        return false;
    ring.slots[(ring.head + ring.count) % kMailSlots] = event;
    ++ring.count;
    return true;
}

// Oldest first, at most max events into out; returns how many were taken.
inline std::size_t MailTake(MailRing& ring, Event* out, std::size_t max)
{
    std::size_t n = 0;
    while (n < max && ring.count > 0)
    {
        out[n++] = ring.slots[ring.head];
        ring.head = std::uint8_t((ring.head + 1) % kMailSlots);
        --ring.count;
    }
    if (ring.count == 0)
        ring.head = 0;
    return n;
}

// Monotonic totals since start; the minute line prints their increments.
struct MailCounters
{
    std::uint64_t posted = 0;      // accepted into a ring
    std::uint64_t dropped = 0;     // refused: ring full, ring limit reached or guid 0
    std::uint64_t drained = 0;     // taken by the bot's own supervisor
    std::uint64_t directive = 0;   // of the drained: GroupDirective (counted only; acted on in T02/T30)
    std::uint64_t lost = 0;        // still in the ring when Forget dropped it (logout, AI teardown)
    std::uint64_t unreg = 0;       // T02-GROUP: part of dropped, posted to a guid without a registration (R10)
};

inline MailCounters MailDelta(MailCounters const& now, MailCounters const& before)
{
    MailCounters d;
    d.posted = now.posted - before.posted;
    d.dropped = now.dropped - before.dropped;
    d.drained = now.drained - before.drained;
    d.directive = now.directive - before.directive;
    d.lost = now.lost - before.lost;
    d.unreg = now.unreg - before.unreg;
    return d;
}

// A silent stretch (nothing posted, refused, taken or lost) writes no line.
inline bool MailSilent(MailCounters const& d)
{
    return d.posted == 0 && d.dropped == 0 && d.drained == 0 && d.directive == 0 && d.lost == 0 && d.unreg == 0;
}

// T02-GROUP (design 7.3, MINOR 12): a ring nobody drains adds no increment, so the line also comes as a gauge every 10th minute
// while rings are open.
inline bool MailGaugeDue(MailCounters const& delta, std::uint32_t rings, std::uint32_t minute)
{
    return !MailSilent(delta) || (rings > 0 && minute % 10 == 0);
}

// The minute line (own tag SupervisorMail; fields posted, dropped, drained, directive, lost, rings, unreg, regs, oldest_s):
// increments since the last line; rings is the number of rings open at the time of the line (a level, not an increment).
// T02-GROUP appends unreg (an increment), regs (registrations, a level) and oldest_s (age of the oldest open ring in seconds, a
// level) as their own literal. Returns what snprintf returns (cut when >= size); the longest line fits 256.
inline int FormatMailMinute(char* buf, std::size_t size, MailCounters const& d, std::uint32_t rings, std::uint32_t regs,
    std::uint32_t oldestSec)
{
    return std::snprintf(buf, size, "[SupervisorMail] posted=%llu dropped=%llu drained=%llu directive=%llu lost=%llu rings=%u"
        " unreg=%llu regs=%u oldest_s=%u",
        (unsigned long long)d.posted, (unsigned long long)d.dropped, (unsigned long long)d.drained,
        (unsigned long long)d.directive, (unsigned long long)d.lost, rings, (unsigned long long)d.unreg, regs, oldestSec);
}
}
