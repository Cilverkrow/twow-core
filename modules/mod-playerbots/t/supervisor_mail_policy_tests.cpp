// A3 step 3, mailbox pair (design A3-S3-ENTWURF section 1.10; agreed with CLI-GRP 09.11.2026): the pure rules of the supervisor's
// mailbox (SupervisorMailPolicy.h). The header names no game type, so this suite compiles it standalone. The game side (the one
// drain in the tick, Forget in ~PlayerbotAI, no Post caller yet, the [SupervisorMail] line) is pinned by
// t/contracts.d/a3s3_supervisor_mailbox_source_contract_tests.cmake.
// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): the R10 registration with an
// instance token (MailOpenAllowed and a pure model of RegisterMail / Post / UnregisterMail), the gauge rule MailGaugeDue and the
// appended fields unreg, regs, oldest_s (design A3-S3-T02-ENTWURF.md v0.4.3 7.3, 7.11).

#include "SupervisorMailPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>

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

using namespace ai::supervisor;

Event Mail(std::uint32_t seq, EventKind kind = EventKind::GroupDirective)
{
    Event e;
    e.kind = kind;
    e.a = seq;
    e.b = 2;
    e.c = 1000 + seq;
    e.mapId = 1;
    e.x = 1.5f;
    e.atMs = 77;
    return e;
}

void TestEventFields()
{
    Event e;
    Require(e.c == 0, "the target id defaults to 0");
    Event m = Mail(5);
    Require(m.a == 5 && m.b == 2 && m.c == 1005 && m.mapId == 1, "seq, type, target and map travel together");
}

void TestPushTakeOrder()
{
    MailRing ring;
    for (std::uint32_t i = 1; i <= 3; ++i)
        Require(MailPush(ring, Mail(i)), "push into a ring with room");
    Event out[kMailSlots];
    std::size_t n = MailTake(ring, out, kMailSlots);
    Require(n == 3, "all three taken");
    Require(out[0].a == 1 && out[1].a == 2 && out[2].a == 3, "oldest first");
    Require(out[2].c == 1003, "the event is copied whole");
    Require(ring.count == 0 && ring.head == 0, "an emptied ring starts over at slot 0");
    Require(MailTake(ring, out, kMailSlots) == 0, "nothing more to take");
}

void TestFullDropsNew()
{
    MailRing ring;
    for (std::uint32_t i = 1; i <= kMailSlots; ++i)
        Require(MailPush(ring, Mail(i)), "push up to the slot count");
    Require(!MailPush(ring, Mail(99)), "a full ring refuses");
    Require(ring.count == kMailSlots, "the refusal changes nothing");
    Event out[kMailSlots];
    std::size_t n = MailTake(ring, out, kMailSlots);
    Require(n == kMailSlots, "all slots taken");
    Require(out[0].a == 1 && out[kMailSlots - 1].a == kMailSlots, "the old events stay; the new one was dropped");
}

void TestMaxAndWrap()
{
    MailRing ring;
    for (std::uint32_t i = 1; i <= 10; ++i)
        MailPush(ring, Mail(i));
    Event out[kMailSlots];
    Require(MailTake(ring, out, 4) == 4, "max bounds the take");
    Require(out[3].a == 4 && ring.count == 6 && ring.head == 4, "the rest stays in order");
    for (std::uint32_t i = 11; i <= 20; ++i)
        Require(MailPush(ring, Mail(i)), "the ring wraps around");
    Require(!MailPush(ring, Mail(21)), "full again after the wrap");
    std::size_t n = MailTake(ring, out, kMailSlots);
    Require(n == kMailSlots, "a full wrapped ring gives all slots");
    for (std::size_t i = 0; i < n; ++i)
        Require(out[i].a == 5 + i, "oldest first across the wrap");
    Require(MailTake(ring, out, 0) == 0, "max 0 takes nothing");
}

void TestCounters()
{
    MailCounters before;
    before.posted = 10;
    before.dropped = 1;
    before.drained = 8;
    before.directive = 3;
    before.lost = 0;
    MailCounters now = before;
    Require(MailSilent(MailDelta(now, before)), "no change = silent");
    now.dropped = 4;
    MailCounters d = MailDelta(now, before);
    Require(!MailSilent(d) && d.dropped == 3 && d.posted == 0, "increments only");
    now = before;
    now.lost = 2;
    Require(!MailSilent(MailDelta(now, before)), "a lost event is not silent");
    now = before;
    now.dropped = 2;
    now.unreg = 1;
    d = MailDelta(now, before);
    Require(!MailSilent(d) && d.unreg == 1 && d.dropped == 1, "T02-GROUP: unreg is an increment and part of dropped");
    MailCounters only;
    only.unreg = 3;
    Require(!MailSilent(MailDelta(only, MailCounters())), "an unregistered drop alone is not silent");
}

void TestLine()
{
    MailCounters d;
    d.posted = 5;
    d.dropped = 1;
    d.drained = 4;
    d.directive = 4;
    d.lost = 0;
    d.unreg = 1;
    char buf[256];
    int n = FormatMailMinute(buf, sizeof(buf), d, 2, 7, 3);
    std::string const want = "[SupervisorMail] posted=5 dropped=1 drained=4 directive=4 lost=0 rings=2 unreg=1 regs=7 oldest_s=3";
    Require(n == int(want.size()) && want == buf, "the minute line format");
    char small[16];
    int cut = FormatMailMinute(small, sizeof(small), d, 2, 7, 3);
    Require(cut >= int(sizeof(small)) && std::strlen(small) == sizeof(small) - 1, "a short buffer cuts, terminated");
}

// T02-GROUP: every field at its maximum still fits the 256 byte buffer of EmitMailMinute (worst case 239 characters).
void TestLineMax()
{
    std::uint64_t const big = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t const big32 = std::numeric_limits<std::uint32_t>::max();
    MailCounters d;
    d.posted = big;
    d.dropped = big;
    d.drained = big;
    d.directive = big;
    d.lost = big;
    d.unreg = big;
    char buf[256];
    int const n = FormatMailMinute(buf, sizeof(buf), d, big32, big32, big32);
    Require(n == 239, "the longest line has 239 characters");
    Require(n < int(sizeof(buf)) && std::strlen(buf) == std::size_t(n), "the longest line is not cut");
    std::string const line(buf);
    Require(line.find(" rings=4294967295 unreg=18446744073709551615 regs=4294967295 oldest_s=4294967295") != std::string::npos &&
        line.size() == std::size_t(n), "the appended fields come last, in this order");
}

// T02-GROUP: R10, the open rule of Post.
void TestOpenAllowed()
{
    Require(MailOpenAllowed(true, 7, 0), "a registered guid opens a ring");
    Require(!MailOpenAllowed(false, 7, 0), "an unregistered guid opens none");
    Require(!MailOpenAllowed(true, 0, 0), "guid 0 opens none");
    Require(MailOpenAllowed(true, 7, kMailMaxRings - 1), "below the ring limit");
    Require(!MailOpenAllowed(true, 7, kMailMaxRings), "at the ring limit");
    MailRing ring;
    Require(ring.openedSec == 0, "a new ring has no open time yet");
}

// T02-GROUP (MINOR 12): the gauge line.
void TestGaugeDue()
{
    MailCounters zero;
    Require(MailGaugeDue(zero, 3, 10), "no increment, rings open, minute 10: due (gauge)");
    Require(!MailGaugeDue(zero, 3, 11), "no increment, rings open, minute 11: silent");
    Require(!MailGaugeDue(zero, 0, 10), "no increment, no ring: silent");
    Require(MailGaugeDue(zero, 1, 0), "minute 0 is a tenth minute");
    MailCounters one;
    one.posted = 1;
    Require(MailGaugeDue(one, 0, 11) && MailGaugeDue(one, 2, 13), "an increment is always due");
}

// T02-GROUP: a pure model of SupervisorMail.cpp's registration (same rules, no lock): RegisterMail overwrites an older entry of
// the guid with a new token (never 0), UnregisterMail drops the registration and the ring only for the stored token, Post opens
// a ring only through MailOpenAllowed and counts an unregistered drop as dropped and unreg.
struct MailModel
{
    std::unordered_map<std::uint32_t, std::uint32_t> registered;
    std::unordered_map<std::uint32_t, MailRing> rings;
    std::uint32_t nextToken = 0;
    MailCounters total;

    std::uint32_t Register(std::uint32_t guid)
    {
        if (guid == 0)
            return 0;
        if (++nextToken == 0)
            ++nextToken;
        registered[guid] = nextToken;
        return nextToken;
    }

    void Unregister(std::uint32_t guid, std::uint32_t token)
    {
        auto const it = registered.find(guid);
        if (it == registered.end() || it->second != token)
            return;
        registered.erase(it);
        auto const r = rings.find(guid);
        if (r != rings.end())
        {
            total.lost += r->second.count;
            rings.erase(r);
        }
    }

    bool Post(std::uint32_t guid, Event const& e, std::uint32_t nowSec)
    {
        auto it = rings.find(guid);
        if (it == rings.end())
        {
            if (!MailOpenAllowed(registered.count(guid) != 0, guid, rings.size()))
            {
                ++total.dropped;
                if (guid != 0 && registered.count(guid) == 0)
                    ++total.unreg;
                return false;
            }
            it = rings.emplace(guid, MailRing()).first;
            it->second.openedSec = nowSec;
        }
        if (!MailPush(it->second, e))
        {
            ++total.dropped;
            return false;
        }
        ++total.posted;
        return true;
    }
};

void TestRegistrationModel()
{
    MailModel m;
    Require(m.Register(0) == 0, "guid 0 is refused");
    Require(!m.Post(5, Mail(1), 100) && m.total.dropped == 1 && m.total.unreg == 1, "no registration: dropped and unreg");
    Require(m.rings.empty(), "no ring for an unregistered guid");
    std::uint32_t const a = m.Register(5);
    Require(a != 0, "a token is never 0");
    Require(m.Post(5, Mail(2), 100) && m.rings.count(5) == 1 && m.rings[5].openedSec == 100, "registered: the ring opens");
    Require(m.Post(5, Mail(3), 150) && m.rings[5].openedSec == 100, "the open time stays the first post's");
    m.Unregister(5, a);
    Require(m.registered.count(5) == 0 && m.rings.count(5) == 0 && m.total.lost == 2, "unregister drops the ring, its rest is lost");
    Require(!m.Post(5, Mail(4), 200), "a post after the unregister fails");
    Require(m.rings.count(5) == 0, "no ring open after the unregister");
    Require(m.total.unreg == 2 && m.total.dropped == 2, "counted as unregistered drop");
}

void TestOldInstanceAfterNew()
{
    MailModel m;
    std::uint32_t const a = m.Register(9);   // the old AI
    std::uint32_t const b = m.Register(9);   // the new AI of the same guid (relog)
    Require(a != b && m.registered[9] == b, "the newest instance holds the registration");
    Require(m.Post(9, Mail(1), 10), "a post reaches the new instance's ring");
    m.Unregister(9, a);                      // the late teardown of the old instance
    Require(m.registered.count(9) == 1 && m.registered[9] == b, "the old token does not drop the new registration");
    Require(m.rings.count(9) == 1 && m.rings[9].count == 1, "nor the new instance's ring");
    m.Unregister(9, b);
    Require(m.registered.count(9) == 0 && m.rings.count(9) == 0, "the new token does");
    m.nextToken = 0xFFFFFFFFu;
    Require(m.Register(3) == 1, "the token counter skips 0 at the wrap");
}
}

int main()
{
    TestEventFields();
    TestPushTakeOrder();
    TestFullDropsNew();
    TestMaxAndWrap();
    TestCounters();
    TestLine();
    TestLineMax();
    TestOpenAllowed();
    TestGaugeDue();
    TestRegistrationModel();
    TestOldInstanceAfterNew();
    std::cout << "supervisor_mail_policy_tests: OK\n";
    return 0;
}
