// A3 step 6 T33-0 "Fakten + Schatten" (design T33 v1.1 + Nachtrag 1; AiPlayerbot.BotGroups.Manager.Gather, default 0;
// measurement only): the pure facts and rules of the meeting point (GroupGatherPolicy.h) - the switch, the GatherPlace pack
// and unpack, the place the manager judges, the group verdict with the leader on the move, the manager clock - and the
// shadow (GroupGatherShadow.h) next to the T30 rule: arrivals, disagreements, the virtual deadline on the manager clock
// (exactly at RendezvousMinutes, a 15 s gap, a resume), the event=phase and minute3 lines. Both headers name no game type,
// so this suite compiles them standalone. The game side (default 0, the post site, the clock before the drain) is pinned by
// t/contracts.d/a3s6_t33_gather_facts_source_contract_tests.cmake.

#include "GroupGatherShadow.h"

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

using namespace ai::group_mgr;
namespace mi = ai::manager_inbox;
namespace qg = ai::quest_groups;

mi::StatusSnapshot StatusAt(std::uint32_t receivedMs, bool alive = true, bool busy = false)
{
    mi::StatusFlags f;
    f.alive = alive;
    f.busy = busy;
    mi::StatusSnapshot s;
    s.flags = mi::PackFlags(f);
    s.receivedMs = receivedMs;
    return s;
}

PlaceSnapshot PlaceAt(std::uint32_t mapId, float x, float y, std::uint32_t receivedMs, float z = 0.0f, std::uint32_t instanceId = 0,
    bool instanceable = false)
{
    PlaceSnapshot p;
    p.place.mapId = mapId;
    p.place.x = x;
    p.place.y = y;
    p.place.z = z;
    p.place.instanceId = instanceId;
    p.place.instanceable = instanceable;
    p.receivedMs = receivedMs;
    return p;
}

GatherRules LiveRules(std::uint32_t maxAgeMs)
{
    GatherRules r;
    r.radius = kGatherYards;
    r.zBand = kZUnknown;
    r.deadCounts = true;
    r.maxAgeMs = maxAgeMs;
    return r;
}

std::vector<MemberVerdict> Members(std::vector<std::uint32_t> const& lows)
{
    std::vector<MemberVerdict> out;
    for (std::uint32_t low : lows)
    {
        MemberVerdict v;
        v.low = low;
        out.push_back(v);
    }
    return out;
}

void TestSwitch()
{
    Require(GatherValid(0) && GatherValid(1) && GatherValid(2) && GatherValid(3), "Gather 0..3 are valid");
    Require(!GatherValid(4) && !GatherValid(0xFFFFFFFFu), "Gather 4 and a negative value are not valid");
    Require(EffectiveGather(2, 0) == 0, "Gather needs Types != 0");
    Require(EffectiveGather(2, kTypeQuest) == 2 && EffectiveGather(1, kTypeAdhoc) == 1, "Gather kept with a type on");
    Require(EffectiveGather(9, kTypesActive) == 0, "an unknown Gather is off");
    Require(GatherArm(1, 10) && GatherArm(1, 11), "Gather 1: every leader is in the arm");
    Require(GatherArm(2, 11) && !GatherArm(2, 10), "Gather 2: odd leaders (cohort B)");
    Require(GatherArm(3, 10) && !GatherArm(3, 11), "Gather 3: even leaders (cohort A)");
    Require(!GatherArm(0, 11) && !GatherArm(9, 11), "Gather 0 or unknown: no arm");
}

void TestPack()
{
    Require(std::size_t(mi::Kind::GatherPlace) == 10, "GatherPlace appended after GroupEdge");
    Require(mi::kKinds == std::size_t(mi::Kind::GatherPlace) + 1, "kKinds covers GatherPlace");
    Require(mi::KindIndex(mi::Kind::GatherPlace) == 10 && mi::KindIndex(mi::Kind(11)) == mi::kKinds, "own bucket, the next is other");

    float const zs[] = { -123.25f, 0.0f, 4711.5f, -0.0f };
    for (float z : zs)
    {
        PlaceFacts f;
        f.mapId = 1;
        f.instanceId = 0xFFFFFFFEu;
        f.instanceable = true;
        f.x = -8913.5f;
        f.y = 554.25f;
        f.z = z;
        f.serial = 0xBEEF;
        mi::Event const e = MakeGatherPlace(4242, f);
        Require(e.kind == mi::Kind::GatherPlace && e.botLow == 4242, "kind and sender");
        Require(e.level == kPlaceInstanceable, "flag b0 instanceable, b1-7 = 0");
        Require(e.role == 0 && e.reason == 0 && e.manaPct10 == 0, "role, reason, manaPct10 reserved = 0");
        Require(e.mapOrDungeon == 1 && e.seq == 0xFFFFFFFEu && e.hpPct10 == 0xBEEF, "map, 32-bit instance id, serial");
        PlaceFacts const back = GatherPlaceOf(e);
        Require(back.mapId == 1 && back.instanceId == 0xFFFFFFFEu && back.instanceable && back.serial == 0xBEEF, "unpack");
        Require(back.x == f.x && back.y == f.y, "x/y round trip");
        Require(std::memcmp(&back.z, &f.z, sizeof(float)) == 0, "z round trip (bits, the sign of -0 included)");
    }
    PlaceFacts continent;
    continent.mapId = 0;
    continent.instanceId = 7;
    mi::Event const e = MakeGatherPlace(1, continent);
    Require(e.level == 0 && !GatherPlaceOf(e).instanceable && GatherPlaceOf(e).instanceId == 7, "a continent: flag 0, raw id kept");
    PlaceSnapshot const s = PlaceSnapshotOf(e, 1234);
    Require(s.receivedMs == 1234 && s.place.instanceId == 7, "snapshot of a drained place");
}

void TestJump()
{
    PlaceFacts a;
    a.mapId = 0;
    PlaceFacts b = a;
    b.x = 150.0f;
    Require(JumpOf(a, b, kPlaceJumpYards) == PlaceJump::None, "150 yd: no jump");
    b.x = 120.0f;
    b.y = 170.0f;
    Require(JumpOf(a, b, kPlaceJumpYards) == PlaceJump::SameMap, "2D 208 yd: same-map jump");
    b.mapId = 1;
    b.x = 0.0f;
    b.y = 0.0f;
    Require(JumpOf(a, b, kPlaceJumpYards) == PlaceJump::OtherMap, "another map: other-map jump");
}

void TestPlaceOf()
{
    mi::StatusSnapshot const st = StatusAt(1000);
    PlaceSnapshot const pl = PlaceAt(0, 1.0f, 2.0f, 1000, 3.0f, 99, false);
    Place p = PlaceOf(nullptr, &pl, 1000, 11000, true);
    Require(!p.inWorld && !p.alive, "no status: nothing known");
    p = PlaceOf(&st, nullptr, 1000, 11000, true);
    Require(!p.inWorld, "no place: nothing known");
    p = PlaceOf(&st, &pl, 12000, 11000, true);
    Require(p.inWorld && p.alive && !p.teleporting, "both fresh at exactly maxAge");
    Require(p.mapId == 0 && p.instanceId == 0 && p.x == 1.0f && p.y == 2.0f && p.z == 3.0f, "a continent counts as instance 0");
    p = PlaceOf(&st, &pl, 12001, 11000, true);
    Require(!p.inWorld, "one ms older than StatusSeconds + one pass: not in the world");
    mi::StatusSnapshot const fresh = StatusAt(12000);
    p = PlaceOf(&fresh, &pl, 12001, 11000, true);
    Require(!p.inWorld, "a fresh status does not refresh a stale place (both must be fresh)");
    PlaceSnapshot const dungeon = PlaceAt(36, 1.0f, 2.0f, 1000, 0.0f, 99, true);
    Require(PlaceOf(&st, &dungeon, 1000, 11000, true).instanceId == 99, "an instanceable map keeps its instance id");
    mi::StatusSnapshot const dead = StatusAt(1000, false);
    Require(PlaceOf(&dead, &pl, 1000, 11000, true).alive, "F12 (a): dead counts");
    Require(!PlaceOf(&dead, &pl, 1000, 11000, false).alive, "F12 (b): dead is not there");
    mi::StatusSnapshot const busy = StatusAt(1000, true, true);
    Require(PlaceOf(&busy, &pl, 1000, 11000, true).teleporting, "busy (taxi, teleport) = teleporting");
    Require(PlaceMaxAgeMs(10, 1000) == 12000 && PlaceMaxAgeMs(10, 300) == 12000 && PlaceMaxAgeMs(10, 1500) == 13000,
        "StatusSeconds + one bot pass + one manager pass (2 x max(interval, 1000))");
    Require(PlaceMaxAgeMs(0, 0) == 2000, "StatusSeconds 0: the two passes only");
    // A jittered post: the snapshots drained at 0, the bot posts at its first pass after 10 s (up to one interval late, 11000)
    // and the manager drains it up to one pass later (12000); one ms before that drain the old snapshots still count.
    mi::StatusSnapshot const st0 = StatusAt(0);
    PlaceSnapshot const pl0 = PlaceAt(0, 1.0f, 2.0f, 0);
    Require(PlaceOf(&st0, &pl0, 11999, PlaceMaxAgeMs(10, 1000), true).inWorld, "jittered post: fresh until the next drain");
    Require(!PlaceOf(&st0, &pl0, 11999, 11000, true).inWorld, "one interval only would drop the bot before the drain");
    Require(PlaceMaxAgeMs(0xFFFFFFFFu, 1000) == 0xFFFFFFFFu, "max age clamped to 32 bits");
}

void TestGatherPoint()
{
    GatherPoint g = GatherPointFor(PlaceAt(0, 10.0f, 20.0f, 0, 30.0f, 5, false), kGatherYards, kZUnknown);
    Require(g.mapId == 0 && g.instanceId == 0 && g.x == 10.0f && g.y == 20.0f && g.z == 30.0f, "point at the leader's place");
    Require(g.radius == kGatherYards && g.zBand == kZUnknown && g.deadlineMs == 0 && g.leg == Leg::SameMap, "F7 (a), SameMap");
    g = GatherPointFor(PlaceAt(36, 0.0f, 0.0f, 0, 0.0f, 5, true), kGatherYards, kZUnknown);
    Require(g.instanceId == 5, "an instanceable map keeps the instance");
    Place p;
    p.alive = true;
    p.inWorld = true;
    p.mapId = 36;
    p.instanceId = 5;
    p.z = 500.0f;
    Require(Arrived(p, g), "kZUnknown: no z test");
}

void TestArrivedForGroup()
{
    std::uint32_t const maxAge = PlaceMaxAgeMs(10, 1000);
    GatherRules const r = LiveRules(maxAge);
    std::unordered_map<std::uint32_t, mi::StatusSnapshot> status;
    PlaceTable places;
    status[1] = StatusAt(0);
    status[2] = StatusAt(0);
    status[3] = StatusAt(0);

    // Leader in motion (K0-1): the leader moves on, its place reaches the manager every status; the follower trails it by one
    // status. The verdict is against the leader's LAST place; the leader is never judged and never "not there".
    places[1] = PlaceAt(0, 400.0f, 0.0f, 0);
    places[2] = PlaceAt(0, 390.0f, 0.0f, 0);
    std::vector<MemberVerdict> m = Members({ 1, 2 });
    GroupVerdict v = ArrivedForGroup(1, m, status, places, 0, r);
    Require(v.pointKnown && v.gathered && v.judged == 1 && v.arrived == 1, "follower within 30 yd of the leader's last place");
    Require(!m[0].judged && !m[0].arrived && m[1].judged && m[1].arrived, "the leader is not judged");
    places[1] = PlaceAt(0, 800.0f, 0.0f, 10000);   // the leader's next place, the follower's not yet
    status[1] = StatusAt(10000);
    v = ArrivedForGroup(1, m, status, places, 10000, r);
    Require(v.pointKnown && !v.gathered && !m[1].arrived && !m[0].judged, "the leader moved on: the follower is not there yet");
    places[2] = PlaceAt(0, 790.0f, 0.0f, 10500);
    status[2] = StatusAt(10500);
    v = ArrivedForGroup(1, m, status, places, 11000, r);
    Require(v.gathered && m[1].arrived, "the follower caught up with the leader's last place");

    // No fresh leader place: nobody is there.
    v = ArrivedForGroup(1, m, status, places, 10000 + maxAge + 1, r);
    Require(!v.pointKnown && !v.gathered && v.judged == 1 && v.arrived == 0, "a stale leader place: no point, nobody there");
    places.erase(1);
    v = ArrivedForGroup(1, m, status, places, 11000, r);
    Require(!v.pointKnown && !v.gathered, "no leader place yet: no verdict");

    // Map, busy, dead, instance and z rules (leader 1 at the origin of map 0).
    places[1] = PlaceAt(0, 0.0f, 0.0f, 20000, 0.0f, 3, false);
    status[1] = StatusAt(20000);
    std::vector<MemberVerdict> two = Members({ 1, 2, 3 });
    places[2] = PlaceAt(1, 0.0f, 0.0f, 20000);
    places[3] = PlaceAt(0, 29.0f, 0.0f, 20000, 20.0f, 4, false);
    status[2] = StatusAt(20000);
    status[3] = StatusAt(20000);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!two[1].arrived && two[2].arrived && v.judged == 2 && v.arrived == 1 && !v.gathered, "another map is not there");
    Require(two[2].zOut && two[2].regionMismatch, "continent region and dz 20: arrived, z_out and region_mismatch counted");
    places[2] = PlaceAt(0, 0.0f, 31.0f, 20000);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!two[1].arrived, "31 yd 2D: not there");
    places[2] = PlaceAt(0, 0.0f, 30.0f, 20000, 0.0f, 3, false);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(two[1].arrived && !two[1].zOut && !two[1].regionMismatch && !two[1].dead && v.gathered, "30 yd 2D: there (S7 <=)");
    status[2] = StatusAt(20000, true, true);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!two[1].arrived && !v.gathered, "busy (taxi, teleport): not there");
    status[2] = StatusAt(20000, false, false);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(two[1].arrived && two[1].dead, "F12 (a): a dead member at the point is there (dead_at_point)");
    status[2] = StatusAt(20000 - maxAge - 1);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!two[1].arrived, "a stale status: not there (K1-23)");
    status[2] = StatusAt(20000);

    // K1-23 for the leader too: a fresh place with a stale or missing Status is no point.
    status[1] = StatusAt(20000 - maxAge - 1);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!v.pointKnown && !v.gathered && v.arrived == 0, "a stale leader status with a fresh place: no point");
    status.erase(1);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!v.pointKnown, "no leader status: no point");
    status[1] = StatusAt(20000);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(v.pointKnown && v.gathered, "both leader snapshots fresh: the point is back");

    // An instanceable map compares the instance strictly.
    places[1] = PlaceAt(36, 0.0f, 0.0f, 20000, 0.0f, 5, true);
    places[2] = PlaceAt(36, 1.0f, 0.0f, 20000, 0.0f, 6, true);
    places[3] = PlaceAt(36, 1.0f, 0.0f, 20000, 0.0f, 5, true);
    v = ArrivedForGroup(1, two, status, places, 20000, r);
    Require(!two[1].arrived && two[2].arrived && !two[2].regionMismatch, "a dungeon: another instance is not there");
}

void TestAdvanceClock()
{
    std::uint32_t last = 0;
    std::uint64_t ms = AdvanceClock(0, last, 5000, 1000);
    Require(ms == 1 && last == 5000, "the first pass starts the clock at 1 on its own anchor");
    ms = AdvanceClock(ms, last, 6000, 1000);
    Require(ms == 1001 && last == 6000, "one pass: +1 s");
    ms = AdvanceClock(ms, last, 8000, 1000);
    Require(ms == 3001, "2 x interval is no gap");
    ms = AdvanceClock(ms, last, 23000, 1000);
    Require(ms == 4001 && last == 23000, "a 15 s gap at 1 s: +1 s only");
    ms = AdvanceClock(ms, last, 25500, 300);
    Require(ms == 4301, "interval 300: the gap limit is 2 x 1000 ms, a gap adds the interval");
    last = 0xFFFFFC18u;   // 1000 ms before the 32-bit wrap
    ms = AdvanceClock(ms, last, 1000, 1000);
    Require(ms == 6301 && last == 1000, "the 32-bit wrap: +2 s");
    last = 0;
    Require(AdvanceClock(777, last, 0, 1000) == 777 && last == 1, "a restart keeps the count; the anchor at 0 is stored as 1");
}

// ---- the shadow ----------------------------------------------------------------------------------------------------------
struct Bench
{
    std::unordered_map<std::uint32_t, ManagedGroup> groups;
    std::unordered_map<std::uint32_t, mi::StatusSnapshot> status;
    PlaceTable places;
    GatherShadow shadow;
    std::vector<std::string> lines;
    ShadowInput in;

    Bench()
    {
        in.gather = 2;
        in.rendezvousMinutes = 1;
        in.rules = LiveRules(PlaceMaxAgeMs(10, 1000));
        in.status = &status;
        in.places = &places;
    }

    void Pass(std::uint32_t nowMs, std::uint64_t mgrMs, bool resumed = false)
    {
        in.nowMs = nowMs;
        in.mgrMs = mgrMs;
        in.resumed = resumed;
        shadow.Pass(groups, in, [this](char const* line) { lines.push_back(line); });
    }

    void Fresh(std::uint32_t low, float x, std::uint32_t nowMs, std::uint32_t map = 0)
    {
        status[low] = StatusAt(nowMs);
        places[low] = PlaceAt(map, x, 0.0f, nowMs);
    }

    std::size_t Count(char const* needle) const
    {
        std::size_t n = 0;
        for (std::string const& l : lines)
            n += l.find(needle) != std::string::npos ? 1 : 0;
        return n;
    }
};

ManagedGroup QuestGroup(std::uint32_t serial, std::uint32_t leader, std::uint32_t follower)
{
    ManagedGroup g;
    g.serial = serial;
    g.type = GroupType::Quest;
    g.leaderLow = leader;
    g.state = GroupState::Forming;
    Member a;
    a.low = leader;
    a.state = MemberState::Acked;
    Member b;
    b.low = follower;
    b.state = MemberState::Acked;
    g.m.push_back(a);
    g.m.push_back(b);
    return g;
}

void Form(ManagedGroup& g)
{
    g.state = GroupState::Active;
    g.formedSec = 100;
    g.phase = qg::Phase::Rendezvous;
}

void TestShadowVerdicts()
{
    Bench b;
    b.groups[7] = QuestGroup(7, 11, 12);
    b.Pass(1000, 1);
    Require(b.lines.empty() && b.shadow.Meetings().at(7).tracked && !b.shadow.Meetings().at(7).formed, "forming: tracked, no line");

    Form(b.groups[7]);
    b.Fresh(11, 0.0f, 2000);
    b.Fresh(12, 100.0f, 2000);
    b.Pass(2000, 1001);
    Require(b.Count("[GroupMgr] event=phase group=7 phase=rendezvous ms=0 gather=1") == 1, "formed: rendezvous line, odd leader arm 1");
    Require(b.shadow.Totals().disagree == 0 && b.shadow.Totals().arrived == 0, "both rules: not gathered");

    b.Fresh(12, 10.0f, 3000);
    b.Pass(3000, 2001);
    Require(b.shadow.Totals().arrived == 1 && b.shadow.Totals().gathered == 1, "T33: the follower arrived, gathered");
    Require(b.shadow.Totals().disagree == 1, "T30 stayed in rendezvous in the same pass: one disagreement");
    Require(!b.shadow.Meetings().at(7).open, "gathered closes the shadow meeting");
    Require(b.Count("[GroupMgr] event=shadow_end group=7 gather=1 result=gathered ms=1000 arrived=1/1") == 1, "one end line, arm 1");

    b.groups[7].phase = qg::Phase::Goal;
    b.Pass(4000, 3001);
    Require(b.shadow.Totals().disagree == 1, "T30 gathered too: no disagreement");
    Require(b.Count("[GroupMgr] event=phase group=7 phase=goal ms=2000 gather=1") == 1, "goal line, ms on the manager clock");

    b.Pass(5000, 4001);
    Require(b.shadow.Totals().disagree == 1 && b.shadow.Totals().arrived == 1, "after rendezvous: no comparison, no recount");
    Require(b.shadow.Totals().compared == 3 && b.shadow.Totals().t33First == 1 && b.shadow.Totals().t30First == 0,
        "compared passes: formed, the T33 gathered pass, the T30 goal pass; T33 first once");
    Require(b.Count("event=shadow_end") == 1, "a closed meeting ends once");

    b.groups[7].phase = qg::Phase::TurnIn;
    b.Pass(6000, 5001);
    Require(b.Count("phase=turnin") == 1, "turnin line");

    b.groups.erase(7);
    b.Pass(7000, 6001);
    Require(b.shadow.Meetings().empty(), "a group gone from the registry: its meeting is over");

    // T30 gathered while the T33 rule does not (the follower is busy on a taxi): disagreement, the meeting stays open.
    Bench c;
    c.groups[9] = QuestGroup(9, 21, 22);
    c.Pass(1000, 1);
    Form(c.groups[9]);
    c.Fresh(21, 0.0f, 2000);
    c.Fresh(22, 5.0f, 2000);
    c.status[22] = StatusAt(2000, true, true);
    c.groups[9].phase = qg::Phase::Goal;
    c.Pass(2000, 1001);
    Require(c.shadow.Totals().disagree == 1 && c.shadow.Totals().gathered == 0 && c.shadow.Meetings().at(9).open,
        "T30 gathered, T33 not: disagreement, the shadow meeting stays open");
    Require(c.Count("phase=rendezvous ms=0") == 1 && c.Count("phase=goal ms=0") == 1, "formed and gathered in one pass: both lines");
    c.status[22] = StatusAt(3000);
    c.Pass(3000, 2001);
    Require(c.shadow.Totals().gathered == 1 && c.shadow.Totals().disagree == 1, "T33 gathers later; T30 is past rendezvous");
    Require(c.shadow.Totals().t30First == 1 && c.shadow.Totals().t33First == 0 && c.shadow.Totals().compared == 1, "T30 first once");

    // straight to TurnIn (all complete): T30 did not judge the gathered rule, no comparison.
    Bench d;
    d.groups[3] = QuestGroup(3, 31, 32);
    d.Pass(1000, 1);
    Form(d.groups[3]);
    d.Fresh(31, 0.0f, 2000);
    d.Fresh(32, 500.0f, 2000);
    d.groups[3].phase = qg::Phase::TurnIn;
    d.Pass(2000, 1001);
    Require(d.shadow.Totals().disagree == 0, "rendezvous -> turnin: no comparison");
}

void TestShadowDeadline()
{
    // Expiry exactly at RendezvousMinutes on the manager clock (formed at mgr 1001, 1 minute).
    Bench b;
    b.groups[5] = QuestGroup(5, 41, 42);
    b.Pass(1000, 1);
    Form(b.groups[5]);
    b.Fresh(41, 0.0f, 2000);
    b.Fresh(42, 100.0f, 2000);
    b.Pass(2000, 1001);
    Require(b.shadow.Meetings().at(5).deadlineMs == 61001, "deadline = formed + RendezvousMinutes on the manager clock");
    b.Pass(2500, 61000);
    Require(b.shadow.Totals().legExpired == 0 && b.shadow.Meetings().at(5).open, "1 ms before the deadline: open");
    b.Pass(3000, 61001);
    Require(b.shadow.Totals().legExpired == 1 && !b.shadow.Meetings().at(5).open, "exactly at the deadline: expired");
    b.Pass(3500, 70000);
    Require(b.shadow.Totals().legExpired == 1, "expired once");

    // A 15 s world hitch at 1 s passes moves the manager clock by 1 s only: no expiry.
    Bench g;
    g.groups[6] = QuestGroup(6, 51, 52);
    std::uint32_t last = 0;
    std::uint64_t mgr = AdvanceClock(0, last, 1000, 1000);
    g.Pass(1000, mgr);
    Form(g.groups[6]);
    mgr = AdvanceClock(mgr, last, 2000, 1000);
    g.Pass(2000, mgr);
    std::uint64_t const formed = mgr;
    std::uint32_t now = 2000;
    for (int i = 0; i < 50; ++i)
    {
        now += 1000;
        mgr = AdvanceClock(mgr, last, now, 1000);
        g.Pass(now, mgr);
    }
    now += 15000;   // the hitch
    mgr = AdvanceClock(mgr, last, now, 1000);
    g.Pass(now, mgr);
    Require(mgr - formed == 51000 && g.shadow.Totals().legExpired == 0, "a 15 s gap: +1 s, no rendezvous_timeout");
    for (int i = 0; i < 9; ++i)
    {
        now += 1000;
        mgr = AdvanceClock(mgr, last, now, 1000);
        g.Pass(now, mgr);
    }
    Require(mgr - formed == 60000 && g.shadow.Totals().legExpired == 1, "the deadline on the manager clock, not the wall clock");

    // Resume (> 120 s off): a fresh deadline, no expiry in that pass.
    Bench r;
    r.groups[8] = QuestGroup(8, 61, 62);
    r.Pass(1000, 1);
    Form(r.groups[8]);
    r.Pass(2000, 1001);
    r.Pass(200000, 70000, true);
    Require(r.shadow.Totals().resume == 1 && r.shadow.Totals().legExpired == 0, "resume: past the deadline, no expiry in that pass");
    Require(r.shadow.Meetings().at(8).deadlineMs == 130000 && r.shadow.Meetings().at(8).formedMs == 1001, "fresh deadline, formed kept");
    r.Pass(201000, 129999);
    Require(r.shadow.Totals().legExpired == 0, "before the fresh deadline");
    r.Pass(202000, 130000);
    Require(r.shadow.Totals().legExpired == 1, "at the fresh deadline");

    // RendezvousMinutes 0: not gathered in the formed pass = expired at once (like S7).
    Bench z;
    z.in.rendezvousMinutes = 0;
    z.groups[4] = QuestGroup(4, 71, 72);
    z.Pass(1000, 1);
    Form(z.groups[4]);
    z.Pass(2000, 1001);
    Require(z.shadow.Totals().legExpired == 1, "RendezvousMinutes 0: expired in the formed pass");
    Bench zg;
    zg.in.rendezvousMinutes = 0;
    zg.groups[4] = QuestGroup(4, 71, 72);
    zg.Pass(1000, 1);
    Form(zg.groups[4]);
    zg.Fresh(71, 0.0f, 2000);
    zg.Fresh(72, 1.0f, 2000);
    zg.Pass(2000, 1001);
    Require(zg.shadow.Totals().legExpired == 0 && zg.shadow.Totals().gathered == 1, "RendezvousMinutes 0: gathered wins");

    // T30 ends the group. Its rendezvous timeout (wall-clock seconds, no gap rule) before the virtual deadline is a T30 cut,
    // at or after it the shadow's expiry; another end closes the meeting without a verdict.
    Bench t;
    t.groups[2] = QuestGroup(2, 81, 82);
    t.groups[3] = QuestGroup(3, 83, 84);
    t.groups[4] = QuestGroup(4, 85, 86);
    t.Pass(1000, 1);
    Form(t.groups[2]);
    Form(t.groups[3]);
    Form(t.groups[4]);
    t.Pass(2000, 1001);
    t.groups[2].state = GroupState::Ending;
    t.groups[2].endReason = EndReason::RendezvousTimeout;
    t.groups[3].state = GroupState::Ending;
    t.groups[3].endReason = EndReason::TooSmall;
    t.Pass(3000, 60500);
    Require(t.shadow.Totals().legExpired == 0 && t.shadow.Totals().t30Cut == 1, "T30 timeout before the virtual deadline: t30_cut");
    Require(!t.shadow.Meetings().at(2).open && !t.shadow.Meetings().at(3).open, "both meetings closed");
    Require(t.Count("[GroupMgr] event=shadow_end group=2 gather=1 result=t30_cut ms=59499 arrived=0/1") == 1 &&
        t.Count("[GroupMgr] event=shadow_end group=3 gather=1 result=closed ms=59499 arrived=0/1") == 1, "t30_cut and closed lines");
    Require(t.shadow.Totals().disagree == 0, "an ending pass is not compared");
    t.groups[4].state = GroupState::Ending;
    t.groups[4].endReason = EndReason::RendezvousTimeout;
    t.Pass(4000, 61001);
    Require(t.shadow.Totals().legExpired == 1 && t.shadow.Totals().t30Cut == 1, "T30 timeout at the virtual deadline: expired");
    Require(t.Count("[GroupMgr] event=shadow_end group=4 gather=1 result=expired ms=60000 arrived=0/1") == 1, "expired line");
}

void TestShadowScope()
{
    Bench b;
    // First seen active (Gather switched on mid-run): never measured.
    ManagedGroup late = QuestGroup(1, 91, 92);
    Form(late);
    b.groups[1] = late;
    // ADHOC: no shadow in T33-0.
    ManagedGroup adhoc = QuestGroup(2, 93, 94);
    adhoc.type = GroupType::Adhoc;
    b.groups[2] = adhoc;
    b.Pass(1000, 1);
    Form(b.groups[2]);
    b.Pass(2000, 1001);
    Require(b.lines.empty() && b.shadow.Totals().arrived == 0, "a late group and ADHOC: no line, no count");
    Require(b.shadow.Meetings().count(2) == 0 && !b.shadow.Meetings().at(1).tracked, "ADHOC not kept, the late group untracked");

    // A group that ends while forming never formed: no line.
    Bench f;
    f.groups[3] = QuestGroup(3, 95, 96);
    f.Pass(1000, 1);
    f.groups[3].state = GroupState::Ending;
    f.Pass(2000, 1001);
    Require(f.lines.empty(), "ended while forming: no phase line");

    // The event line cap: kLinesPerMinute per minute, the rest counted.
    Bench c;
    for (std::uint32_t s = 1; s <= kLinesPerMinute + 5; ++s)
        c.groups[s] = QuestGroup(s, 1000 + 2 * s, 1001 + 2 * s);
    c.Pass(1000, 1);
    for (auto& entry : c.groups)
        Form(entry.second);
    c.Pass(2000, 1001);
    Require(c.lines.size() == kLinesPerMinute && c.shadow.Totals().linesDropped == 5, "line cap, dropped lines counted");
    c.Pass(61000, 60001);
    Require(c.Count("[GroupMgr] minute3 ") == 1 && c.Count(" shadow_lines_dropped=5") == 1, "minute line with the dropped lines");
}

void TestShadowMinute()
{
    Bench b;
    b.groups[7] = QuestGroup(7, 11, 12);
    b.Pass(1000, 1);
    Require(b.lines.empty(), "the first pass opens the window, no line");
    Form(b.groups[7]);
    b.Fresh(11, 0.0f, 2000);
    b.Fresh(12, 10.0f, 2000);
    // Two places of bot 13 for the jump counters (StorePlace from the drain).
    mi::Event e = MakeGatherPlace(13, PlaceAt(0, 0.0f, 0.0f, 0).place);
    b.shadow.StorePlace(b.places, b.status, e, 2000);
    e = MakeGatherPlace(13, PlaceAt(0, 300.0f, 0.0f, 0).place);
    b.shadow.StorePlace(b.places, b.status, e, 2500);
    e = MakeGatherPlace(13, PlaceAt(1, 300.0f, 0.0f, 0).place);
    b.shadow.StorePlace(b.places, b.status, e, 2600);
    Require(b.places.at(13).receivedMs == 2600 && b.places.at(13).place.mapId == 1, "the latest place per bot");
    b.Pass(2000, 1001);
    b.Pass(60999, 60000);
    Require(b.Count("minute3") == 0, "no line before 60 s");
    b.Fresh(11, 0.0f, 60500);   // the only fresh place at the minute line (12 and 13 stopped posting)
    b.Pass(61000, 60001);
    Require(b.Count("[GroupMgr] minute3 gather=2 region_mismatch=0 z_out=0 place_jump_same=1 place_jump_other=1 gather_resume=0"
        " shadow_arrived=1 shadow_disagree=1 shadow_leg_expired=0 places=1 open=0 shadow_gathered=1 shadow_compared=3"
        " shadow_t33_first=1 shadow_t30_first=0 shadow_t30_cut=0 dead_at_point=0 shadow_lines_dropped=0") == 1,
        "minute3: the 5.3 fields in order, then the T33-0 additions; increments, fresh places, open meetings");
    b.Pass(121000, 120001);
    Require(b.Count("[GroupMgr] minute3 gather=2 region_mismatch=0 z_out=0 place_jump_same=0 place_jump_other=0 gather_resume=0"
        " shadow_arrived=0 shadow_disagree=0 shadow_leg_expired=0 places=0 open=0 shadow_gathered=0 shadow_compared=1") == 1,
        "the next minute: increments only");

    b.shadow.Off();
    Require(b.shadow.Meetings().empty() && b.shadow.Totals().arrived == 0, "Off forgets meetings and counts");
    std::size_t const before = b.lines.size();
    b.Pass(122000, 121001);
    Require(b.lines.size() == before, "after Off the first pass opens a fresh window; the active group is untracked");
}

void TestShadowEnd()
{
    // One event=shadow_end per measured meeting, with its arm (Gather 2 = cohort B: odd leaders arm 1, even leaders arm 0).
    Bench b;
    b.groups[1] = QuestGroup(1, 11, 12);   // gathered in the formed pass, the follower dead at the point
    b.groups[2] = QuestGroup(2, 20, 22);   // its virtual deadline passes
    b.groups[3] = QuestGroup(3, 31, 32);   // T30's rendezvous_timeout before the virtual deadline
    b.groups[4] = QuestGroup(4, 40, 42);   // gone from the registry while open
    b.groups[5] = QuestGroup(5, 51, 52);   // T30's rendezvous_timeout at the virtual deadline
    b.Pass(1000, 1);
    for (auto& entry : b.groups)
        Form(entry.second);
    b.Fresh(11, 0.0f, 2000);
    b.Fresh(12, 5.0f, 2000);
    b.status[12] = StatusAt(2000, false);
    b.Pass(2000, 1001);
    Require(b.Count("[GroupMgr] event=shadow_end group=1 gather=1 result=gathered ms=0 arrived=1/1") == 1, "gathered, arm 1");
    Require(b.shadow.Totals().arrived == 1 && b.shadow.Totals().deadAtPoint == 1, "F12 (a): the dead follower counts once");

    b.groups[3].state = GroupState::Ending;
    b.groups[3].endReason = EndReason::RendezvousTimeout;
    b.groups.erase(4);
    b.Pass(3000, 30001);
    Require(b.Count("[GroupMgr] event=shadow_end group=3 gather=1 result=t30_cut ms=29000 arrived=0/1") == 1, "t30_cut");
    Require(b.Count("[GroupMgr] event=shadow_end group=4 gather=0 result=closed ms=29000 arrived=0/1") == 1, "gone: closed, arm 0");

    b.groups[5].state = GroupState::Ending;
    b.groups[5].endReason = EndReason::RendezvousTimeout;
    b.Pass(4000, 61001);
    Require(b.Count("[GroupMgr] event=shadow_end group=2 gather=0 result=expired ms=60000 arrived=0/1") == 1, "expired, arm 0");
    Require(b.Count("[GroupMgr] event=shadow_end group=5 gather=1 result=expired ms=60000 arrived=0/1") == 1, "T30 at the deadline");
    Require(b.shadow.Totals().legExpired == 2 && b.shadow.Totals().t30Cut == 1 && b.shadow.Totals().gathered == 1, "end totals");
    b.Pass(5000, 62001);
    Require(b.Count("event=shadow_end") == 5, "every meeting ends exactly once");

    // place_jump_same skips taxi flights: no jump while the bot is busy at either place.
    Bench t;
    t.status[14] = StatusAt(1000);
    t.shadow.StorePlace(t.places, t.status, MakeGatherPlace(14, PlaceAt(0, 0.0f, 0.0f, 0).place), 1000);
    t.status[14] = StatusAt(11000, true, true);   // on a taxi
    t.shadow.StorePlace(t.places, t.status, MakeGatherPlace(14, PlaceAt(0, 300.0f, 0.0f, 0).place), 11000);
    t.shadow.StorePlace(t.places, t.status, MakeGatherPlace(14, PlaceAt(0, 600.0f, 0.0f, 0).place), 21000);
    t.status[14] = StatusAt(31000);               // landed
    t.shadow.StorePlace(t.places, t.status, MakeGatherPlace(14, PlaceAt(0, 900.0f, 0.0f, 0).place), 31000);
    Require(t.shadow.Totals().jumpSame == 0 && t.places.at(14).place.x == 900.0f, "taxi: no jump, the place still kept");
    t.shadow.StorePlace(t.places, t.status, MakeGatherPlace(14, PlaceAt(0, 1200.0f, 0.0f, 0).place), 41000);
    Require(t.shadow.Totals().jumpSame == 1, "on foot at both places: a far place is a jump");
}
}

int main()
{
    TestSwitch();
    TestPack();
    TestJump();
    TestPlaceOf();
    TestGatherPoint();
    TestArrivedForGroup();
    TestAdvanceClock();
    TestShadowVerdicts();
    TestShadowDeadline();
    TestShadowScope();
    TestShadowMinute();
    TestShadowEnd();
    std::cout << "group_gather_policy_tests: OK\n";
    return 0;
}
