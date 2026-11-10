// A3 step 3, mailbox pair (design A3-S3-ENTWURF section 1.10; agreed with CLI-GRP 09.11.2026): the pure rules of the supervisor's
// mailbox (SupervisorMailPolicy.h). The header names no game type, so this suite compiles it standalone. The game side (the one
// drain in the tick, Forget in ~PlayerbotAI, no Post caller yet, the [SupervisorMail] line) is pinned by
// t/contracts.d/a3s3_supervisor_mailbox_source_contract_tests.cmake.

#include "SupervisorMailPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
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
}

void TestLine()
{
    MailCounters d;
    d.posted = 5;
    d.dropped = 1;
    d.drained = 4;
    d.directive = 4;
    d.lost = 0;
    char buf[256];
    int n = FormatMailMinute(buf, sizeof(buf), d, 2);
    std::string const want = "[SupervisorMail] posted=5 dropped=1 drained=4 directive=4 lost=0 rings=2";
    Require(n == int(want.size()) && want == buf, "the minute line format");
    char small[16];
    int cut = FormatMailMinute(small, sizeof(small), d, 2);
    Require(cut >= int(sizeof(small)) && std::strlen(small) == sizeof(small) - 1, "a short buffer cuts, terminated");
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
    std::cout << "supervisor_mail_policy_tests: OK\n";
    return 0;
}
