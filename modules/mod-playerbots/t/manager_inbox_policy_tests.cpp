// A3-GERUEST-L60 7.2.11 (AiPlayerbot.BotGroups.Manager.StatusSeconds, default 0): the pure rules of the manager inbox
// (ManagerInboxPolicy.h). The header names no game type, so this suite compiles it standalone. The game side (default 0, the
// one Post site, the drain only in the manager, no group writes) is pinned by
// t/contracts.d/a3s6_manager_inbox_source_contract_tests.cmake.

#include "ManagerInboxPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

using namespace ai::manager_inbox;

Event Make(std::uint32_t botLow, std::uint32_t seq, Kind kind = Kind::Status)
{
    Event e{};
    e.botLow = botLow;
    e.kind = kind;
    e.seq = seq;
    return e;
}

void TestLayout()
{
    Require(sizeof(Event) == 32, "Event is 32 bytes");
    Require(Capacity == 4096, "module ring capacity 4096");
    Require(kDrainMaxPerPass == 1024, "drain max per pass 1024");
    Require(kKinds == std::size_t(Kind::DirectiveAck) + 1, "kKinds covers every kind");
    Require(std::size_t(Kind::QueueLeave) == 7 && std::size_t(Kind::DirectiveAck) == 8, "DirectiveAck appended at the end");
    Require(kKindBuckets == kKinds + 1, "one other bucket");
    Require(KindIndex(Kind::Status) == 3, "Status index");
    Require(KindIndex(Kind::DirectiveAck) == 8, "DirectiveAck has its own bucket");
    Require(KindIndex(Kind(200)) == kKinds && KindIndex(Kind(9)) == kKinds, "an unknown kind goes to the other bucket");
    Require(KindIndex(Kind(200)) < kKindBuckets, "the other bucket is in range");
}

void TestFifo()
{
    Ring<4> ring;
    Require(ring.Push(Make(1, 1)) && ring.Push(Make(2, 2)) && ring.Push(Make(3, 3)), "pushes below capacity succeed");
    std::vector<Event> out;
    Require(ring.Drain(out, 10) == 3, "drain returns what is there");
    Require(out.size() == 3 && out[0].seq == 1 && out[1].seq == 2 && out[2].seq == 3, "FIFO order");
    Require(ring.Size() == 0, "empty after the drain");

    // wrap around the end of the buffer
    for (std::uint32_t i = 10; i < 14; ++i)
        Require(ring.Push(Make(i, i)), "fill to capacity after a drain");
    out.clear();
    Require(ring.Drain(out, 2) == 2 && out[0].seq == 10 && out[1].seq == 11, "partial drain, oldest first");
    Require(ring.Push(Make(14, 14)) && ring.Push(Make(15, 15)), "room again after the partial drain");
    out.clear();
    Require(ring.Drain(out, 100) == 4, "the rest");
    Require(out[0].seq == 12 && out[1].seq == 13 && out[2].seq == 14 && out[3].seq == 15, "FIFO across the wrap");
}

void TestFull()
{
    Ring<2> ring;
    Require(ring.Push(Make(1, 1)) && ring.Push(Make(1, 2)), "two fit");
    Require(!ring.Push(Make(1, 3)), "full -> false");
    Require(!ring.Push(Make(1, 4)), "still full -> false");
    Require(ring.Read().posted == 2 && ring.Read().dropped == 2, "posted counts accepted, dropped counts refused");
    std::vector<Event> out;
    ring.Drain(out, 5);
    Require(out.size() == 2 && out[1].seq == 2, "the dropped events never enter");
}

void TestDrainMax()
{
    Ring<8> ring;
    for (std::uint32_t i = 0; i < 6; ++i)
        ring.Push(Make(i, i));
    std::vector<Event> out;
    Require(ring.Drain(out, 0) == 0 && out.empty(), "max 0 takes nothing");
    Require(ring.Drain(out, 4) == 4 && out.size() == 4, "max 4 takes 4");
    Require(ring.Drain(out, 4) == 2 && out.size() == 6, "appends, takes the remaining 2");
    Require(ring.Drain(out, 4) == 0, "empty ring drains 0");
}

void TestCounters()
{
    Ring<3> ring;
    for (std::uint32_t i = 0; i < 5; ++i)
        ring.Push(Make(i, i));
    std::vector<Event> out;
    ring.Drain(out, 2);
    ring.Push(Make(9, 9));
    Counters const c = ring.Read();
    Require(c.posted == 4 && c.dropped == 2 && c.drained == 2, "counters posted/dropped/drained");
    ring.Drain(out, 10);
    Require(ring.Read().drained == 4, "drained is monotonic");
}

void TestStatusDue()
{
    Require(!StatusDue(0, 5000, 0), "period 0 is never due");
    Require(!StatusDue(1000, 900000, 0), "period 0 is never due, whatever the clock");
    Require(StatusDue(0, 5, 30000), "never posted = due");
    Require(!StatusDue(1000, 30999, 30000), "before the period");
    Require(StatusDue(1000, 31000, 30000), "at the period");
    Require(StatusDue(0xFFFFF000u, 0x00006000u, 0x6000u), "wrap: 0x7000 ms elapsed >= 0x6000");
    Require(!StatusDue(0xFFFFF000u, 0x00004000u, 0x6000u), "wrap: 0x5000 ms elapsed < 0x6000");
    Require(PostStamp(0) == 1 && PostStamp(77) == 77, "a post at clock 0 is stored as 1 (0 = never)");
}

void TestPerMille()
{
    Require(PerMille(0, 0) == 0 && PerMille(50, 0) == 0, "max 0 -> 0");
    Require(PerMille(0, 100) == 0, "empty -> 0");
    Require(PerMille(100, 100) == 1000, "full -> 1000");
    Require(PerMille(150, 100) == 1000, "over max clamps to 1000");
    Require(PerMille(1, 3) == 333, "rounds down");
    Require(PerMille(4000000000u, 4000000001u) == 999, "no overflow on large values");
}

void TestClampAndDelta()
{
    Require(ClampStatusSeconds(0) == 0 && ClampStatusSeconds(-5) == 0, "0 and negative = off");
    Require(ClampStatusSeconds(30) == 30, "in range kept");
    Require(ClampStatusSeconds(3600) == 3600 && ClampStatusSeconds(100000) == kStatusSecondsMax, "clamped to the max");

    std::uint64_t seen = 0;
    Require(TakeDelta(10, seen) == 10 && seen == 10, "first delta from 0");
    Require(TakeDelta(25, seen) == 15 && seen == 25, "increment, not total");
    Require(TakeDelta(25, seen) == 0, "no change = 0");
    seen = ~std::uint64_t(0);
    Require(TakeDelta(4, seen) == 5, "wrap-safe increment");
}

void TestMinuteDue()
{
    MinuteLine m;
    Require(!MinuteLineDue(m), "an all-zero minute writes no line (StatusSeconds 0)");
    m.ack = 1;
    Require(MinuteLineDue(m), "an ack alone makes the line due");
    m = MinuteLine();
    m.dropped = 1;
    Require(MinuteLineDue(m), "a drop alone makes the line due");
    m = MinuteLine();
    m.snapshots = 4;
    Require(MinuteLineDue(m), "a kept table makes the line due");

    Require(!ResumedAfterGap(1000, 1000 + kResumeGapMs), "a gap of exactly kResumeGapMs is no resume");
    Require(ResumedAfterGap(1000, 1001 + kResumeGapMs), "a longer gap = the manager was off");
    Require(!ResumedAfterGap(0xFFFFF000u, 0x00001000u), "wrap: 0x2000 ms is a normal pass");
    Require(ResumedAfterGap(0xFFFFF000u, kResumeGapMs), "wrap: a long gap across the wrap");
}

void TestSnapshotAndLine()
{
    Event e = Make(42, 7);
    e.role = 2;
    e.level = 33;
    e.mapOrDungeon = 1;
    e.hpPct10 = 500;
    e.manaPct10 = 250;
    e.x = 1.5f;
    e.y = -2.5f;
    StatusSnapshot const s = SnapshotOf(e, 9000);
    Require(s.role == 2 && s.level == 33 && s.mapId == 1 && s.hpPct10 == 500 && s.manaPct10 == 250, "snapshot fields");
    Require(s.x == 1.5f && s.y == -2.5f && s.seq == 7 && s.receivedMs == 9000, "snapshot position, seq, received");

    MinuteLine m;
    m.posted = 5;
    m.dropped = 1;
    m.drained = 4;
    m.status = 4;
    m.ack = 2;
    m.snapshots = 3;
    char buf[256];
    FormatMinuteLine(buf, sizeof(buf), m);
    Require(std::string(buf) == "[ManagerInbox] posted=5 dropped=1 drained=4 status=4 ack=2 snapshots=3", "minute line format");
}
}

int main()
{
    TestLayout();
    TestFifo();
    TestFull();
    TestDrainMax();
    TestCounters();
    TestStatusDue();
    TestPerMille();
    TestClampAndDelta();
    TestMinuteDue();
    TestSnapshotAndLine();
    std::cout << "manager_inbox_policy_tests: OK\n";
    return 0;
}
