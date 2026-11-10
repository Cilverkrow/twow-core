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
#include <unordered_map>
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
    Require(kKinds == std::size_t(Kind::GroupEdge) + 1, "kKinds covers every kind");
    Require(std::size_t(Kind::QueueLeave) == 7 && std::size_t(Kind::DirectiveAck) == 8, "DirectiveAck appended at the end");
    Require(std::size_t(Kind::GroupEdge) == 9 && std::size_t(Kind::GroupWish) == 2, "T30-0: GroupEdge appended, GroupWish kept");
    Require(kKindBuckets == kKinds + 1, "one other bucket");
    Require(KindIndex(Kind::Status) == 3, "Status index");
    Require(KindIndex(Kind::DirectiveAck) == 8, "DirectiveAck has its own bucket");
    Require(KindIndex(Kind::GroupEdge) == 9, "GroupEdge has its own bucket");
    Require(KindIndex(Kind(200)) == kKinds && KindIndex(Kind(10)) == kKinds, "an unknown kind goes to the other bucket");
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
    m.edge = 6;
    m.wish = 2;
    m.keyOverflow = 1;
    m.edges = 3;
    m.wishes = 1;
    m.shadow.stale = 1;
    m.shadow.busy = 0;
    m.shadow.adhoc = 2;
    m.shadow.quest = 1;
    m.shadow.zoneHold = 0;
    m.shadow.noSupervisor = 0;
    char buf[640];
    FormatMinuteLine(buf, sizeof(buf), m);
    Require(std::string(buf) == "[ManagerInbox] posted=5 dropped=1 drained=4 status=4 ack=2 snapshots=3 edge=6 wish=2 key_overflow=1"
        " edges=3 wishes=1 stale_snap=1 cand_busy=0 cand_adhoc=2 cand_quest=1 cand_zone_hold=0 no_supervisor=0", "minute line format");

    // The longest line fits the 640-byte buffer of LogMinute.
    MinuteLine big;
    std::uint64_t const max = ~std::uint64_t(0);
    big.posted = big.dropped = big.drained = big.status = big.ack = big.snapshots = max;
    big.edge = big.wish = big.keyOverflow = big.edges = big.wishes = max;
    big.shadow.stale = big.shadow.busy = big.shadow.adhoc = big.shadow.quest = big.shadow.zoneHold = big.shadow.noSupervisor = max;
    Require(FormatMinuteLine(buf, sizeof(buf), big) < int(sizeof(buf)), "the longest minute line is not cut");
}

// ---- T30-0: the facts --------------------------------------------------------------------------------------------------------
void TestFlagsAndKey()
{
    StatusFlags f;
    Require(PackFlags(f) == 0, "no flag = 0");
    f.alliance = f.alive = f.candidate = f.inBgQueue = f.dungeonMap = f.busy = f.startZoneHold = true;
    Require(PackFlags(f) == 0x7F, "all seven flags, b7 stays 0");
    StatusFlags one;
    one.busy = true;
    Require(PackFlags(one) == kFlagBusy && kFlagBusy == 0x20, "busy = b5");
    one = StatusFlags();
    one.startZoneHold = true;
    Require(PackFlags(one) == 0x40, "startZoneHold = b6");
    StatusFlags const back = UnpackFlags(0x25);
    Require(back.alliance && !back.alive && back.candidate && !back.inBgQueue && !back.dungeonMap && back.busy && !back.startZoneHold,
        "unpack is the inverse");
    Require(PackFlags(UnpackFlags(0x5A)) == 0x5A, "round trip");

    KeyFacts k;
    std::uint32_t bits = 77;
    Require(PackKey(k, bits) && bits == 0, "no key = 0");
    k.questId = 80395;
    k.objective = 2;
    k.questStatus = kQuestIncomplete;
    k.objDone = true;
    k.progress = 13;   // mod 8 = 5
    Require(PackKey(k, bits), "a twow quest id fits");
    Require((bits & kKeyQuestMax) == 80395 && ((bits >> 23) & 7u) == 3 && ((bits >> 26) & 3u) == 1 && ((bits >> 28) & 1u) == 1 &&
        (bits >> 29) == 5, "field layout b0-22 / b23-25 / b26-27 / b28 / b29-31");
    KeyFacts const u = UnpackKey(bits);
    Require(u.questId == 80395 && u.objective == 2 && u.questStatus == kQuestIncomplete && u.objDone && u.progress == 5, "unpack");
    k.objective = -1;
    Require(PackKey(k, bits) && UnpackKey(bits).objective == -1, "no objective = field 0 = -1");
    k.objective = 9;
    Require(PackKey(k, bits) && UnpackKey(bits).objective == -1, "an objective out of range is posted as none");
    k.objective = 0;
    k.questStatus = kQuestRewarded;
    Require(PackKey(k, bits) && UnpackKey(bits).objective == 0 && UnpackKey(bits).questStatus == kQuestRewarded, "objective 0, rewarded");
    k.questId = kKeyQuestMax;
    Require(PackKey(k, bits) && UnpackKey(bits).questId == kKeyQuestMax, "the largest quest id that fits");
    k.questId = kKeyQuestMax + 1;
    bits = 77;
    Require(!PackKey(k, bits) && bits == 0, "overflow: false and an empty key (the caller counts key_overflow)");
}

void TestEdgeAndWish()
{
    EdgeFacts f;
    f.groupId = 1234;
    f.leaderLow = 99;
    f.members = 3;
    f.leader = true;
    f.invite = true;
    Event const e = MakeEdge(42, f, 10.0f, -20.0f);
    Require(e.kind == Kind::GroupEdge && e.botLow == 42 && e.mapOrDungeon == 1234 && e.seq == 99 && e.role == 3, "edge fields");
    Require(e.level == (kEdgeLeader | kEdgeInvite) && e.x == 10.0f && e.y == -20.0f && e.reserved == 0, "edge flags and position");
    Require(EdgeOf(e) == f, "edge round trip");
    f.posStuck = true;
    f.raid = true;
    f.bgGroup = true;
    Require(MakeEdge(1, f, 0, 0).level == (kEdgeLeader | kEdgeRaid | kEdgeBgGroup | kEdgeInvite | kEdgePosStuck), "every edge flag");
    Require((MakeEdge(1, f, 0, 0).level & 0x02) == 0, "b1 stays free");
    f.members = 40;
    Require(EdgeOf(MakeEdge(1, f, 0, 0)).members == 40, "a raid's size fits");
    f.members = 300;
    Require(MakeEdge(1, f, 0, 0).role == 255, "the size is clamped to 255");
    EdgeFacts const g = EdgeOf(MakeEdge(1, f, 0, 0));
    Require(g != f, "a clamped size compares unequal to the raw facts");
    Require(EdgeFacts() == EdgeFacts() && !(EdgeFacts() != EdgeFacts()), "ungrouped edges are equal");
    EdgeFacts h;
    h.invite = true;
    Require(h != EdgeFacts(), "a new invite is a change");

    WishFacts w;
    w.questId = 80396;
    w.openObjectives = 3;
    w.dropOnly = true;
    w.level = 21;
    w.mapId = 1;
    w.x = 5.0f;
    w.y = 6.0f;
    w.guildId = 777;
    Event const we = MakeWish(42, 8, w);
    Require(we.kind == Kind::GroupWish && we.mapOrDungeon == 80396 && we.seq == 8 && we.reason == (3 | kWishDropOnly), "wish fields");
    Require(we.level == 21 && we.hpPct10 == 1 && we.reserved == 777 && we.x == 5.0f && we.y == 6.0f, "map in hpPct10, guild in reserved");
    WishFacts const wb = WishOf(we);
    Require(wb.questId == 80396 && wb.openObjectives == 3 && wb.dropOnly && wb.level == 21 && wb.mapId == 1 && wb.guildId == 777,
        "wish round trip");
    w.openObjectives = 12;
    w.dropOnly = false;
    w.mapId = 70000;
    Event const wc = MakeWish(42, 8, w);
    Require(wc.reason == kWishOpenMask && wc.hpPct10 == 0xFFFF, "open objectives clamp to 7, a map id above 16 bits to 0xFFFF");
}

StatusSnapshot Snap(std::uint8_t flags, std::uint32_t receivedMs)
{
    Event e = Make(1, 1);
    e.reason = flags;
    e.reserved = 0x1234;
    StatusSnapshot const s = SnapshotOf(e, receivedMs);
    Require(s.flags == flags && s.key == 0x1234, "the snapshot keeps the flags and the key");
    return s;
}

void TestShadow()
{
    Require(StaleMs(0) == kStaleMinMs && StaleMs(5) == 30000 && StaleMs(10) == 30000 && StaleMs(11) == 33000, "kStaleMs >= 30 s");
    Require(StaleMs(3600) == 10800000u, "three periods at the max");
    Require(Fresh(1000, 31000, 30000) && !Fresh(1000, 31001, 30000), "fresh up to staleMs");
    Require(Fresh(0xFFFFF000u, 0x00001000u, 30000), "fresh across the clock wrap");

    std::uint8_t const ok = kFlagAlive | kFlagCandidate;
    std::uint32_t const now = 100000;
    std::uint32_t const stale = 30000;
    EdgeSnapshot ungrouped;
    ungrouped.receivedMs = now;
    WishSnapshot wish;
    wish.wish.questId = 5;
    wish.receivedMs = now;

    ShadowVerdict v = ShadowCandidate(Snap(ok, now), &ungrouped, nullptr, true, now, stale);
    Require(v.adhoc && !v.quest && !v.zoneHold && !v.busy && !v.stale && !v.noSupervisor, "an ADHOC candidate");
    v = ShadowCandidate(Snap(ok, now), &ungrouped, &wish, true, now, stale);
    Require(v.adhoc && v.quest && !v.zoneHold, "with a fresh wish: a QUEST candidate too");
    v = ShadowCandidate(Snap(ok | kFlagStartZoneHold, now), &ungrouped, &wish, true, now, stale);
    Require(v.adhoc && !v.quest && v.zoneHold, "start-zone hold: no QUEST candidate (R1)");
    v = ShadowCandidate(Snap(ok, now - stale - 1), &ungrouped, &wish, true, now, stale);
    Require(v.stale && !v.adhoc, "a stale status is no candidate");
    v = ShadowCandidate(Snap(ok | kFlagBusy, now), &ungrouped, &wish, true, now, stale);
    Require(v.busy && !v.adhoc, "busy is no candidate");
    Require(!ShadowCandidate(Snap(kFlagCandidate, now), &ungrouped, nullptr, true, now, stale).adhoc, "dead is no candidate");
    Require(!ShadowCandidate(Snap(kFlagAlive, now), &ungrouped, nullptr, true, now, stale).adhoc, "not a candidate bot");
    Require(!ShadowCandidate(Snap(ok | kFlagInBgQueue, now), &ungrouped, nullptr, true, now, stale).adhoc, "BG queue is no candidate");
    Require(!ShadowCandidate(Snap(ok | kFlagDungeonMap, now), &ungrouped, nullptr, true, now, stale).adhoc, "instance is no candidate");
    Require(!ShadowCandidate(Snap(ok, now), nullptr, nullptr, true, now, stale).adhoc, "no edge yet: no candidate");
    EdgeSnapshot grouped = ungrouped;
    grouped.edge.groupId = 7;
    Require(!ShadowCandidate(Snap(ok, now), &grouped, nullptr, true, now, stale).adhoc, "grouped: no candidate in T30-0");
    EdgeSnapshot oldEdge = ungrouped;
    oldEdge.receivedMs = now - stale - 1;
    Require(!ShadowCandidate(Snap(ok, now), &oldEdge, nullptr, true, now, stale).adhoc, "a stale edge: no candidate");
    v = ShadowCandidate(Snap(ok | kFlagBusy, now), &grouped, &wish, true, now, stale);
    Require(!v.busy && !v.adhoc, "busy and grouped: not counted as cand_busy");
    v = ShadowCandidate(Snap(kFlagCandidate | kFlagBusy, now), &ungrouped, &wish, true, now, stale);
    Require(!v.busy && !v.adhoc, "busy and dead: not counted as cand_busy");
    v = ShadowCandidate(Snap(ok | kFlagBusy | kFlagDungeonMap, now), &ungrouped, &wish, true, now, stale);
    Require(!v.busy && !v.adhoc, "busy in an instance: not counted as cand_busy");
    v = ShadowCandidate(Snap(ok | kFlagBusy, now), &ungrouped, &wish, false, now, stale);
    Require(v.busy && !v.noSupervisor, "busy is counted before the supervisor test");
    v = ShadowCandidate(Snap(ok, now), &ungrouped, &wish, false, now, stale);
    Require(v.noSupervisor && !v.adhoc && !v.quest, "no supervisor: counted, no candidate");
    WishSnapshot oldWish = wish;
    oldWish.receivedMs = now - stale - 1;
    Require(!ShadowCandidate(Snap(ok, now), &ungrouped, &oldWish, true, now, stale).quest, "a stale wish: no QUEST candidate");

    std::unordered_map<std::uint32_t, StatusSnapshot> status;
    std::unordered_map<std::uint32_t, EdgeSnapshot> edges;
    std::unordered_map<std::uint32_t, WishSnapshot> wishes;
    status[1] = Snap(ok, now);
    status[2] = Snap(ok, now);
    status[3] = Snap(ok | kFlagBusy, now);
    status[4] = Snap(ok, 1);
    status[5] = Snap(ok, now);
    status[6] = Snap(ok | kFlagBusy, now);
    edges[1] = ungrouped;
    edges[2] = ungrouped;
    edges[3] = ungrouped;
    edges[5] = ungrouped;
    edges[6] = grouped;
    wishes[2] = wish;
    ShadowCounts const c = CountShadow(status, edges, wishes, [](std::uint32_t low) { return low != 5; }, now, stale);
    Require(c.adhoc == 2 && c.quest == 1 && c.busy == 1 && c.stale == 1 && c.noSupervisor == 1 && c.zoneHold == 0, "tally over the tables");
}

void TestT30MinuteDue()
{
    MinuteLine m;
    m.edge = 1;
    Require(MinuteLineDue(m), "an edge alone makes the line due");
    m = MinuteLine();
    m.wishes = 1;
    Require(MinuteLineDue(m), "a kept wish table makes the line due");
    m = MinuteLine();
    m.keyOverflow = 1;
    Require(MinuteLineDue(m), "a key overflow makes the line due");
    Require(!MinuteLineDue(MinuteLine()), "still no line for a silent minute");
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
    TestFlagsAndKey();
    TestEdgeAndWish();
    TestShadow();
    TestT30MinuteDue();
    std::cout << "manager_inbox_policy_tests: OK\n";
    return 0;
}
