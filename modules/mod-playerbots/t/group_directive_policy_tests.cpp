// A3 step 6 T30-1 (design T30 v1.1; AiPlayerbot.BotGroups.Manager.Types, default 0): the pure state machine of the group
// manager (GroupDirectivePolicy.h) - the directive bits, the end reasons and their F13 attempt rule, the ack reasons, the
// candidate facts and the engine pass (design 2.2 order, the gap and resume rules, the op budget, the fail-closed timeouts).
// The header names no game type, so this suite compiles it standalone with a fake Io. The game side (default 0, the gates
// G1-G12, the one supervisor::Post) is pinned by t/contracts.d/a3s6_t30_directives_source_contract_tests.cmake.

#include "GroupDirectivePolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
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
namespace go = ai::group_ops;
namespace sv = ai::supervisor;

struct JoinCall
{
    std::uint32_t invitee = 0;
    std::uint32_t inviter = 0;
    std::uint32_t groupId = 0;
    GroupType type = GroupType::Adhoc;
    std::uint32_t questId = 0;
};

struct LeaveCall
{
    std::uint32_t bot = 0;
    std::uint32_t groupId = 0;
};

class FakeIo : public Io
{
public:
    bool supervisor = true;
    bool postOk = true;
    bool joinOk = true;
    std::vector<std::uint32_t> s7;
    std::vector<JoinCall> joins;
    std::vector<LeaveCall> leaves;
    std::vector<std::pair<std::uint32_t, sv::Event>> posts;
    std::vector<std::string> lines;

    bool SupervisorApplies(std::uint32_t) override { return supervisor; }
    bool S7Owns(std::uint32_t low) override
    {
        for (std::uint32_t s : s7)
            if (s == low)
                return true;
        return false;
    }
    bool QueueJoin(std::uint32_t invitee, std::uint32_t inviter, std::uint32_t groupId, GroupType type, std::uint32_t questId,
        std::uint8_t) override
    {
        if (!joinOk)
            return false;
        JoinCall j;
        j.invitee = invitee;
        j.inviter = inviter;
        j.groupId = groupId;
        j.type = type;
        j.questId = questId;
        joins.push_back(j);
        return true;
    }
    bool QueueLeave(std::uint32_t bot, std::uint32_t groupId, std::uint32_t) override
    {
        LeaveCall l;
        l.bot = bot;
        l.groupId = groupId;
        leaves.push_back(l);
        return true;
    }
    bool PostDirective(std::uint32_t low, sv::Event const& event) override
    {
        if (!postOk)
            return false;
        posts.emplace_back(low, event);
        return true;
    }
    void Line(char const* line) override { lines.emplace_back(line); }

    std::size_t CountLines(char const* needle) const
    {
        std::size_t n = 0;
        for (std::string const& l : lines)
            n += l.find(needle) != std::string::npos ? 1 : 0;
        return n;
    }
};

// The inbox tables, the acks and the op outcomes of one pass.
struct World
{
    StatusTable status;
    EdgeTable edges;
    WishTable wishes;
    std::vector<Ack> acks;
    std::vector<go::ManagerOutcome> outcomes;
    bool started = false;
    std::uint32_t originMs = 0;   // the seconds clock runs on from here (time(nullptr) in the game; it never wraps with the ms)
};

std::uint32_t SecOf(World const& w, std::uint32_t nowMs)
{
    return 100000 + std::uint32_t(nowMs - w.originMs) / 1000;
}

void SetStatus(World& w, std::uint32_t low, std::uint32_t nowMs, float x, float y, std::uint32_t quest = 100,
    std::int8_t objective = 0, std::uint8_t questStatus = mi::kQuestIncomplete, std::uint32_t progress = 0, bool zoneHold = false)
{
    mi::StatusSnapshot s;
    s.level = 10;
    s.mapId = 1;
    s.x = x;
    s.y = y;
    s.receivedMs = nowMs;
    mi::StatusFlags f;
    f.alliance = true;
    f.alive = true;
    f.candidate = true;
    f.startZoneHold = zoneHold;
    s.flags = mi::PackFlags(f);
    mi::KeyFacts k;
    k.questId = quest;
    k.objective = objective;
    k.questStatus = questStatus;
    k.progress = progress;
    std::uint32_t key = 0;
    mi::PackKey(k, key);
    s.key = key;
    w.status[low] = s;
}

void SetEdge(World& w, std::uint32_t low, std::uint32_t nowMs, std::uint32_t groupId = 0, std::uint32_t leader = 0,
    std::uint32_t members = 0)
{
    mi::EdgeSnapshot e;
    e.edge.groupId = groupId;
    e.edge.leaderLow = leader;
    e.edge.members = members;
    e.edge.leader = groupId && leader == low;
    e.receivedMs = nowMs;
    w.edges[low] = e;
}

void SetWish(World& w, std::uint32_t low, std::uint32_t nowMs, std::uint32_t quest, float x, float y)
{
    mi::WishSnapshot s;
    s.wish.questId = quest;
    s.wish.openObjectives = 2;
    s.wish.level = 10;
    s.wish.mapId = 1;
    s.wish.x = x;
    s.wish.y = y;
    s.receivedMs = nowMs;
    w.wishes[low] = s;
}

// Every snapshot fresh again (the bots keep posting).
void Refresh(World& w, std::uint32_t nowMs)
{
    for (auto& s : w.status)
        s.second.receivedMs = nowMs;
    for (auto& e : w.edges)
        e.second.receivedMs = nowMs;
    for (auto& s : w.wishes)
        s.second.receivedMs = nowMs;
}

Rules AdhocRules()
{
    Rules r;
    r.mode = 1;
    r.types = kTypeAdhoc;
    r.statusSeconds = 10;
    r.intervalMs = 1000;
    return r;
}

void Run(Engine& e, World& w, Rules const& r, FakeIo& io, std::uint32_t nowMs, bool resumed = false)
{
    if (!w.started)
    {
        w.started = true;
        w.originMs = nowMs;
    }
    PassInput in;
    in.nowMs = nowMs;
    in.nowSec = SecOf(w, nowMs);
    in.resumed = resumed;
    in.status = &w.status;
    in.edges = &w.edges;
    in.wishes = &w.wishes;
    in.acks = &w.acks;
    in.outcomes = &w.outcomes;
    e.Pass(in, r, io);
    w.acks.clear();
    w.outcomes.clear();
}

// One pass per second (the manager's interval), the snapshots kept fresh, up to and including until. Returns the last time.
std::uint32_t Tick(Engine& e, World& w, Rules const& r, FakeIo& io, std::uint32_t t, std::uint32_t until)
{
    while (std::uint32_t(until - t) >= 1000 && std::uint32_t(until - t) < 0x80000000u)
    {
        t += 1000;
        Refresh(w, t);
        Run(e, w, r, io, t);
    }
    return t;
}

go::ManagerOutcome Outcome(std::uint32_t bot, std::uint32_t other, go::OpKind kind, go::ManagerOpResult result,
    go::Verdict verdict = go::Verdict::Execute)
{
    go::ManagerOutcome o;
    o.bot = bot;
    o.other = other;
    o.kind = kind;
    o.result = result;
    o.verdict = verdict;
    return o;
}

Ack AckFor(std::uint32_t low, std::uint32_t seq, std::uint8_t state, std::uint8_t reason = 0)
{
    Ack a;
    a.low = low;
    a.seq = seq;
    a.state = state;
    a.reason = reason;
    return a;
}

std::uint32_t SeqOf(FakeIo const& io, std::uint32_t low, Verb verb)
{
    std::uint32_t seq = 0;
    for (auto const& p : io.posts)
        if (p.first == low && UnpackDirective(p.second.b).verb == verb)
            seq = p.second.a;
    return seq;
}

std::size_t Posts(FakeIo const& io, Verb verb)
{
    std::size_t n = 0;
    for (auto const& p : io.posts)
        n += UnpackDirective(p.second.b).verb == verb ? 1 : 0;
    return n;
}

// Bots 1 and 2 on the same objective 10 yd apart; pass 1 forms and queues the one join, pass 2 executes and confirms it
// (core group 77) and sends both Enters. Returns the time of pass 2.
std::uint32_t FormAdhocPair(Engine& e, World& w, Rules const& r, FakeIo& io, std::uint32_t t0)
{
    SetStatus(w, 1, t0, 0.0f, 0.0f);
    SetStatus(w, 2, t0, 10.0f, 0.0f);
    SetEdge(w, 1, t0);
    SetEdge(w, 2, t0);
    Run(e, w, r, io, t0);
    Require(io.joins.size() == 1, "forming: one join op in the first pass (budget 1)");
    Require(io.joins[0].groupId == 0 && io.joins[0].type == GroupType::Adhoc && io.joins[0].questId == 100,
        "forming: a new ADHOC group (groupId 0) on the pair's quest");
    std::uint32_t const invitee = io.joins[0].invitee;
    std::uint32_t const inviter = io.joins[0].inviter;
    Require((invitee == 1 && inviter == 2) || (invitee == 2 && inviter == 1), "forming: the pair joins");
    std::uint32_t const t1 = t0 + 1000;
    Refresh(w, t1);
    w.outcomes.push_back(Outcome(invitee, inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    SetEdge(w, invitee, t1, 77, inviter, 2);
    SetEdge(w, inviter, t1, 77, inviter, 2);
    Run(e, w, r, io, t1);
    Require(e.Total(Cnt::Joined) == 2, "both members confirmed by their edges");
    Require(Posts(io, Verb::Enter) == 2, "one Enter per member after the confirmation");
    return t1;
}

void TestDirectiveBits()
{
    DirectiveBits d;
    d.verb = Verb::Enter;
    d.type = GroupType::Quest;
    d.objective = 3;
    d.reason = EndReason::NoProgress;   // not carried by an Enter
    d.serial = 0xBEEF;
    std::uint32_t const b = PackDirective(d);
    Require((b & 0xFu) == 1 && ((b >> 4) & 0x3u) == 1 && ((b >> 6) & 0x7u) == 4, "Enter: verb, type, objective + 1");
    Require(((b >> 9) & 0x7Fu) == 0, "Enter: b9-15 stay 0 (T33 reads them)");
    Require((b >> 16) == 0xBEEF, "the serial in b16-31 (9b R3)");
    DirectiveBits const u = UnpackDirective(b);
    Require(u.verb == Verb::Enter && u.type == GroupType::Quest && u.objective == 3 && u.reason == EndReason::None && u.serial == 0xBEEF,
        "Enter round trip");
    d.verb = Verb::Exit;
    d.objective = -1;
    DirectiveBits const x = UnpackDirective(PackDirective(d));
    Require(x.verb == Verb::Exit && x.objective == -1 && x.reason == EndReason::NoProgress, "Exit carries the end reason");
    Require(std::uint32_t(EndReason::Count) < 128, "every end reason fits b9-15");
    sv::Event const ev = MakeDirective(42, d, 1234, 1, 5.0f, 6.0f, 999);
    Require(ev.kind == sv::EventKind::GroupDirective && ev.a == 42 && ev.c == 1234 && ev.mapId == 1 && ev.z == 0.0f && ev.atMs == 999,
        "the event: kind, seq, quest id, map, no gather point");
    Require(ev.source == std::uint8_t(sv::Requester::GroupMgr) && ev.source == 2, "source = Requester::GroupMgr (2)");
}

void TestReasons()
{
    Require(std::string(EndReasonName(EndReason::ForeignGroup)) == "foreign_group" &&
        std::string(EndReasonName(EndReason::ExtLeft)) == "ext_left" &&
        std::string(EndReasonName(EndReason::DirectiveDropped)) == "directive_dropped", "end reason names for the log");
    // F13 (a): only the S7 dissolve reasons except turned_in are a QUEST attempt.
    Require(CountsAsQuestAttempt(EndReason::NoProgress) && CountsAsQuestAttempt(EndReason::RendezvousTimeout) &&
        CountsAsQuestAttempt(EndReason::LeaderStuck), "S7 reasons count as an attempt");
    Require(!CountsAsQuestAttempt(EndReason::TurnedIn), "turned_in is no attempt");
    Require(!CountsAsQuestAttempt(EndReason::AckTimeout) && !CountsAsQuestAttempt(EndReason::JoinFailed) &&
        !CountsAsQuestAttempt(EndReason::SwitchOff) && !CountsAsQuestAttempt(EndReason::ObjectiveDone),
        "manager and ADHOC reasons are no attempt");
    Require(FromLeave(ai::adhoc_group::Leave::LeaderOtherMap) == EndReason::LeaderOtherMap &&
        FromDissolve(ai::quest_groups::Dissolve::TooSmall) == EndReason::TooSmall, "legacy reasons map one to one");
    // F12 (a): the ack reason is uint8(Deny) 1-7, own reasons from 16.
    Require(std::string(AckReasonName(3)) == std::string(sv::DenyName(sv::Deny::Requester)), "1-7 = the supervisor's deny");
    Require(std::string(AckReasonName(kAckReasonStartZoneHold)) == "start_zone_hold" && kAckReasonStartZoneHold == 16,
        "16 = start_zone_hold (R8)");
    Require(std::string(AckReasonName(19)) == "gather_other_map" && std::string(AckReasonName(20)) == "no_open_group" &&
        std::string(AckReasonName(17)) == "reserved_17", "17/18 reserved, 19/20 named for T33");
    Require(ExitDone(mi::kAckApplied, 0) && ExitDone(mi::kAckRefused, std::uint8_t(sv::Deny::SameMode)) &&
        ExitDone(mi::kAckRefused, std::uint8_t(sv::Deny::PassiveHold)), "Exit done: 2, or 3 with SameMode / PassiveHold");
    Require(!ExitDone(mi::kAckRefused, std::uint8_t(sv::Deny::Requester)) && !ExitDone(mi::kAckReceived, 0), "other Exit answers fail");
}

void TestCandidate()
{
    World w;
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetEdge(w, 1, 1000);
    CandidateFacts c = Candidate(w.status[1], &w.edges[1], nullptr, true, false, 2000, 30000);
    Require(c.adhoc && !c.quest, "a fresh ungrouped bot with an open objective: an ADHOC candidate");
    c = Candidate(w.status[1], &w.edges[1], nullptr, true, false, 40000, 30000);
    Require(c.stale && !c.adhoc, "a stale status: no candidate");
    c = Candidate(w.status[1], &w.edges[1], nullptr, false, false, 2000, 30000);
    Require(c.noSupervisor && !c.adhoc, "F2 (a): no candidate without the supervisor");
    SetEdge(w, 1, 1000, 9, 5, 2);
    c = Candidate(w.status[1], &w.edges[1], nullptr, true, false, 2000, 30000);
    Require(!c.adhoc && !c.busy, "grouped: no candidate");
    SetEdge(w, 1, 1000);
    SetWish(w, 1, 1000, 300, 0.0f, 0.0f);
    c = Candidate(w.status[1], &w.edges[1], &w.wishes[1], true, false, 2000, 30000);
    Require(c.quest, "a fresh wish: a QUEST candidate");
    c = Candidate(w.status[1], &w.edges[1], &w.wishes[1], true, true, 2000, 30000);
    Require(!c.quest, "a blocked quest (attempts, cooldown): no QUEST candidate");
    SetStatus(w, 1, 1000, 0.0f, 0.0f, 100, 0, mi::kQuestIncomplete, 0, true);
    c = Candidate(w.status[1], &w.edges[1], &w.wishes[1], true, false, 2000, 30000);
    Require(c.zoneHold && !c.quest, "F14 (a): a start-zone hold is never a QUEST candidate");
}

// Types 0 (the default): the engine forms nothing, queues nothing, posts nothing, writes no line.
void TestTypesZero()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    r.types = 0;
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetStatus(w, 2, 1000, 10.0f, 0.0f);
    SetEdge(w, 1, 1000);
    SetEdge(w, 2, 1000);
    for (std::uint32_t t = 1000; t <= 200000; t += 1000)
    {
        Refresh(w, t);
        Run(e, w, r, io, t);
    }
    Require(io.joins.empty() && io.leaves.empty() && io.posts.empty(), "Types 0: no op, no directive");
    Require(io.lines.empty(), "Types 0: no line (no [GroupMgr] minute either)");
    Require(!e.HasGroups() && !e.View().Any(), "Types 0: the registry and the view stay empty");
    // An owned bot in a bot-only group is not touched either (F4 needs Types).
    SetEdge(w, 1, 201000, 50, 1, 2);
    SetEdge(w, 2, 201000, 50, 1, 2);
    Run(e, w, r, io, 201000);
    Require(io.leaves.empty(), "Types 0: no foreign_group dissolve");
}

void TestAdhocAcked()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t const t1 = FormAdhocPair(e, w, r, io, 1000);
    Require(e.View().Any(), "the view holds the members");
    ViewEntry v;
    Require(e.View().Find(1, v) && v.coreGroupId == 77 && v.questRole == 0, "an ADHOC member: managed, no QUEST role");
    Require(e.View().KeyOf(77, v) && v.questId == 100 && v.objective == 0, "the group's key by its core id (ReadKey)");
    // Pass order (M15): the ack drained in the same pass as the deadline check counts, no timeout.
    std::uint32_t const t2 = Tick(e, w, r, io, t1, t1 + kAckTimeoutMs) + 1000;
    Require(e.Total(Cnt::AckTimeout) == 0, "no timeout at exactly kAckTimeoutMs");
    Refresh(w, t2);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t2);
    Require(e.Total(Cnt::AckApp) == 2 && e.Total(Cnt::AckTimeout) == 0, "acks before deadlines: acked, no timeout");
    Require(e.Total(Cnt::Formed) == 1, "two acked members: the group is formed");
    Require(io.CountLines("[GroupMgr] event=formed") == 1, "one formed line");
    // A stale ack (an old seq) is counted, nothing else.
    Refresh(w, t2 + 1000);
    w.acks.push_back(AckFor(1, 999999, mi::kAckRefused, 3));
    Run(e, w, r, io, t2 + 1000);
    Require(e.Total(Cnt::AckStale) == 1 && e.Groups().size() == 1, "an ack of an unknown seq: ack_stale only");
}

// No supervisor answers (no PostDirectiveAck caller in this pair): every Enter times out, the group ends cleanly.
void TestAckTimeoutFailClosed()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs + 1000);
    Require(e.Total(Cnt::AckTimeout) == 2, "both Enters time out (fail closed)");
    Require(e.TotalLeave(EndReason::AckTimeout) == 2, "ack_timeout booked per member");
    Require(Posts(io, Verb::Exit) == 2, "an Exit for each delivered Enter");
    Require(io.leaves.size() == 1, "one leave op per pass (budget)");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[0].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    Require(io.leaves.size() == 2, "the second leave in the next pass");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[1].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Stale, go::Verdict::DropAlreadyLeft));
    SetEdge(w, 1, t);
    SetEdge(w, 2, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Exit), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Exit), mi::kAckRefused, std::uint8_t(sv::Deny::SameMode)));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Left) == 2 && e.Groups().empty(), "both left (executed, already left): the group is gone");
    Require(io.CountLines("[GroupMgr] event=dissolved") == 1, "one dissolved line");
    Require(!e.HasGroups() && !e.View().Any(), "exits answered: nothing held, the call site may stop");
}

// M7: the join clock starts at Execute; a queued join without an outcome never times out.
void TestJoinClock()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetStatus(w, 2, 1000, 10.0f, 0.0f);
    SetEdge(w, 1, 1000);
    SetEdge(w, 2, 1000);
    Run(e, w, r, io, 1000);
    for (std::uint32_t t = 2000; t <= 1000 + 3 * kJoinConfirmMs; t += 1000)
    {
        Refresh(w, t);
        Run(e, w, r, io, t);
    }
    Require(e.Total(Cnt::JoinFailed) == 0 && io.joins.size() == 1, "pending join: no clock, no second join (M8)");
    std::uint32_t t = 1000 + 3 * kJoinConfirmMs + 1000;
    w.outcomes.push_back(Outcome(io.joins[0].invitee, io.joins[0].inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    for (t += 1000; t <= 1000 + 4 * kJoinConfirmMs + 2000; t += 1000)
    {
        Refresh(w, t);
        Run(e, w, r, io, t);
    }
    Require(e.Total(Cnt::JoinFailed) == 1, "executed, never confirmed: join_failed after kJoinConfirmMs");
    // A failed join op: the member ends without a leave op.
    Engine e2;
    World w2;
    FakeIo io2;
    SetStatus(w2, 1, 1000, 0.0f, 0.0f);
    SetStatus(w2, 2, 1000, 10.0f, 0.0f);
    SetEdge(w2, 1, 1000);
    SetEdge(w2, 2, 1000);
    Run(e2, w2, r, io2, 1000);
    Refresh(w2, 2000);
    w2.outcomes.push_back(Outcome(io2.joins[0].invitee, io2.joins[0].inviter, go::OpKind::Join, go::ManagerOpResult::Failed));
    Run(e2, w2, r, io2, 2000);
    Refresh(w2, 3000);
    Run(e2, w2, r, io2, 3000);
    Require(e2.Total(Cnt::JoinFailed) == 1 && io2.leaves.empty() && io2.posts.empty(), "a failed join: no leave, no directive");
}

// M15: a world hitch (15 s between passes at a 1 s interval) ends no deadline.
void TestGapRule()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t += 15000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::GapSuspend) == 1 && e.Total(Cnt::AckTimeout) == 0, "a 15 s gap: gap_suspend, no ack timeout");
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs + 1000);
    Require(e.Total(Cnt::AckTimeout) == 2, "the deadline runs on after the gap");
}

// M16: a resume (> 120 s off) keeps the registry, restarts the anchors and ends nothing in that pass.
void TestResume()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t += 200000;
    // The caller cleared its tables on the resume (DrainInbox): no snapshot in this pass and the next ones.
    w.status.clear();
    w.edges.clear();
    w.wishes.clear();
    Run(e, w, r, io, t, true);
    Require(e.Total(Cnt::AckTimeout) == 0 && e.Groups().size() == 1, "resume pass: no timeout, no member forgotten");
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs / 2);
    Require(e.Total(Cnt::AckTimeout) == 0 && e.Groups().size() == 1, "the anchors restarted at the resume, silent not yet");
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs);
    Require(e.Total(Cnt::AckTimeout) == 2, "then the deadline ends them");
}

// R10: an Enter the supervisor's postbox does not take ends the member at once (directive_dropped), no ack wait, no Exit.
void TestDirectiveDropped()
{
    Engine e;
    World w;
    FakeIo io;
    io.postOk = false;
    Rules const r = AdhocRules();
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetStatus(w, 2, 1000, 10.0f, 0.0f);
    SetEdge(w, 1, 1000);
    SetEdge(w, 2, 1000);
    Run(e, w, r, io, 1000);
    Refresh(w, 2000);
    w.outcomes.push_back(Outcome(io.joins[0].invitee, io.joins[0].inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    SetEdge(w, 1, 2000, 77, io.joins[0].inviter, 2);
    SetEdge(w, 2, 2000, 77, io.joins[0].inviter, 2);
    Run(e, w, r, io, 2000);
    Require(e.Total(Cnt::DirDropped) == 2 && e.TotalLeave(EndReason::DirectiveDropped) == 2, "both Enters dropped: directive_dropped");
    Require(io.posts.empty() && e.Total(Cnt::DirSent) == 0, "nothing delivered, no Exit owed");
    Require(io.leaves.size() == 1, "the leave op follows at once (fail closed)");
}

// F8 (a): a refused Exit goes once more in a later pass, then the bot is blocked (exit_failed).
void TestExitRetry()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs + 1000);
    std::uint32_t const first = SeqOf(io, 1, Verb::Exit);
    Require(first != 0, "the first Exit went out");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, first, mi::kAckRefused, std::uint8_t(sv::Deny::Requester)));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::ExitPending) == 1 && SeqOf(io, 1, Verb::Exit) == first, "a refused Exit: exit_pending, not resent in that pass");
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    std::uint32_t const second = SeqOf(io, 1, Verb::Exit);
    Require(second != first, "the second Exit goes out in the next pass");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, second, mi::kAckRefused, std::uint8_t(sv::Deny::Requester)));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::ExitFailed) == 1 && io.CountLines("[GroupMgr] event=exit_failed bot=1") == 1, "twice refused: exit_failed");
    Require(e.Bots().count(1) && e.Bots().at(1).blockedUntilSec > 0, "the bot stays blocked (no candidate) for kExitBlockSec");
}

// 2.8: Types back to 0 ends every group (switch_off), one op per pass; HasGroups keeps the pass until then.
void TestSwitchOff()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t);
    r.types = 0;
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    Require(e.TotalLeave(EndReason::SwitchOff) == 2 && Posts(io, Verb::Exit) == 2, "switch off: every member ends with an Exit");
    Require(io.leaves.size() == 1 && e.HasGroups(), "one leave per pass, the registry still holds the group");
}

// F4 (a): an owned bot in an existing bot-only group is taken out (foreign_group); a group with an unseen member is not.
void TestForeignGroup()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    SetStatus(w, 1, 1000, 0.0f, 0.0f, 0);
    SetStatus(w, 2, 1000, 10.0f, 0.0f, 0);
    SetEdge(w, 1, 1000, 50, 1, 2);
    SetEdge(w, 2, 1000, 50, 1, 2);
    SetStatus(w, 3, 1000, 500.0f, 0.0f, 0);
    SetEdge(w, 3, 1000, 60, 3, 2);   // a member that posts no edge (a real player or the other cohort)
    Run(e, w, r, io, 1000);
    Require(e.Total(Cnt::Foreign) == 2 && e.TotalLeave(EndReason::ForeignGroup) == 2, "a bot-only group: each owned member to leave");
    Require(io.leaves.size() == 1 && io.leaves[0].groupId == 50, "one leave op per pass (budget)");
    Require(io.CountLines("[GroupMgr] event=foreign core=50") == 2, "a foreign line per member");
    Require(e.Total(Cnt::SkipUnknown) == 1, "a group with an unseen member: skip_unknown, no leave");
    Refresh(w, 2000);
    Run(e, w, r, io, 2000);
    Require(io.leaves.size() == 2 && io.leaves[1].groupId == 50 && io.leaves[1].bot != io.leaves[0].bot, "the second member next pass");
    Require(e.Total(Cnt::Foreign) == 2, "no repeat while the leave is out (kLeaveConfirmMs)");
    Engine e2;
    FakeIo io2;
    io2.s7.push_back(1);
    io2.s7.push_back(2);
    Run(e2, w, r, io2, 2000);
    Require(io2.leaves.empty() && e2.Total(Cnt::SkipS7) == 2, "S7 owns the bots: skip_s7, no leave");
}

// F13 (a): a QUEST group that ends on an S7 reason counts an attempt; a manager reason only sets the cooldown.
void TestQuestAttempts()
{
    Rules r;
    r.mode = 1;
    r.types = kTypeQuest;
    r.statusSeconds = 10;
    r.intervalMs = 1000;
    // (a) the Enters time out: no attempt, the retry cooldown.
    {
        Engine e;
        World w;
        FakeIo io;
        SetStatus(w, 1, 1000, 0.0f, 0.0f, 300, -1);
        SetStatus(w, 2, 1000, 100.0f, 0.0f, 300, -1);
        SetEdge(w, 1, 1000);
        SetEdge(w, 2, 1000);
        SetWish(w, 1, 1000, 300, 0.0f, 0.0f);
        SetWish(w, 2, 1000, 300, 100.0f, 0.0f);
        Run(e, w, r, io, 1000);
        Require(io.joins.size() == 1 && io.joins[0].type == GroupType::Quest && io.joins[0].questId == 300, "QUEST: a join for the wish");
        std::uint32_t const inviter = io.joins[0].inviter;
        std::uint32_t const invitee = io.joins[0].invitee;
        Refresh(w, 2000);
        w.outcomes.push_back(Outcome(invitee, inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
        SetEdge(w, invitee, 2000, 88, inviter, 2);
        SetEdge(w, inviter, 2000, 88, inviter, 2);
        Run(e, w, r, io, 2000);
        ViewEntry v;
        Require(e.View().Find(inviter, v) && v.questRole == 2 && e.View().Find(invitee, v) && v.questRole == 1,
            "the S7 role mirror: leader 2, follower 1 (3.6)");
        std::uint32_t const t = Tick(e, w, r, io, 2000, 2000 + kAckTimeoutMs + 1000);
        Require(e.TotalLeave(EndReason::AckTimeout) == 2, "QUEST: the Enters time out");
        std::uint32_t const nowSec = SecOf(w, t);
        Require(e.QuestAttempts(1, 300) == 0 && e.QuestAttempts(2, 300) == 0, "ack_timeout: no QUEST attempt");
        Require(e.QuestCooldown(1, 300, nowSec), "ack_timeout: the retry cooldown only");
    }
    // (b) acked, never gathered (100 yd apart): rendezvous_timeout after RendezvousMinutes counts an attempt.
    {
        Engine e;
        World w;
        FakeIo io;
        SetStatus(w, 1, 1000, 0.0f, 0.0f, 300, -1);
        SetStatus(w, 2, 1000, 100.0f, 0.0f, 300, -1);
        SetEdge(w, 1, 1000);
        SetEdge(w, 2, 1000);
        SetWish(w, 1, 1000, 300, 0.0f, 0.0f);
        SetWish(w, 2, 1000, 300, 100.0f, 0.0f);
        Run(e, w, r, io, 1000);
        std::uint32_t const inviter = io.joins[0].inviter;
        std::uint32_t const invitee = io.joins[0].invitee;
        Refresh(w, 2000);
        w.outcomes.push_back(Outcome(invitee, inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
        SetEdge(w, invitee, 2000, 88, inviter, 2);
        SetEdge(w, inviter, 2000, 88, inviter, 2);
        Run(e, w, r, io, 2000);
        Refresh(w, 3000);
        w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
        w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
        Run(e, w, r, io, 3000);
        Require(e.Total(Cnt::Formed) == 1, "QUEST group formed");
        std::uint32_t t = 3000;
        for (std::uint32_t i = 0; i < r.timers.rendezvousMinutes * 60 + 5 && e.TotalLeave(EndReason::RendezvousTimeout) == 0; ++i)
        {
            t += 1000;
            Refresh(w, t);
            Run(e, w, r, io, t);
        }
        Require(e.TotalLeave(EndReason::RendezvousTimeout) == 2, "never gathered: rendezvous_timeout");
        Require(e.QuestAttempts(1, 300) == 1 && e.QuestAttempts(2, 300) == 1, "an S7 reason: one attempt per member");
    }
}

// OB-10 note on #984: a Status lost to a full ring leaves the old wish behind. A wish older than WishFreshMs (1.5 periods)
// is no wish, even while it is younger than kStaleMs; a wish of the latest period forms as before.
void TestWishLeftover()
{
    Require(WishFreshMs(10) == 15000 && WishFreshMs(0) == 0, "WishFreshMs: 1.5 status periods");
    Rules r;
    r.mode = 1;
    r.types = kTypeQuest;
    r.statusSeconds = 10;
    r.intervalMs = 1000;
    Engine e;
    World w;
    FakeIo io;
    SetWish(w, 1, 1000, 300, 0.0f, 0.0f);
    SetWish(w, 2, 1000, 300, 100.0f, 0.0f);
    SetStatus(w, 1, 20000, 0.0f, 0.0f, 300, -1);
    SetStatus(w, 2, 20000, 100.0f, 0.0f, 300, -1);
    SetEdge(w, 1, 20000);
    SetEdge(w, 2, 20000);
    Run(e, w, r, io, 20000);
    Require(io.joins.empty(), "a 19 s old wish (status period 10 s) forms no QUEST group");
    SetWish(w, 1, 21000, 300, 0.0f, 0.0f);
    SetWish(w, 2, 21000, 300, 100.0f, 0.0f);
    Refresh(w, 21000);
    Run(e, w, r, io, 21000);
    Require(io.joins.size() == 1 && io.joins[0].type == GroupType::Quest, "a wish of the latest period forms as before");
}

// The minute lines: fixed fields, only while Types != 0 or the registry holds anything.
void TestMinuteLine()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    for (std::uint32_t t = 1000; t <= 62000; t += 1000)
        Run(e, w, r, io, t);
    Require(io.CountLines("[GroupMgr] minute types=1 passes=") == 1, "one minute line with the type stand first");
    Require(io.CountLines("[GroupMgr] minute2 leave_quest_turned_in=0") == 1, "one minute2 line, every reason, 0 included");
    Require(io.CountLines(" owned=0") == 1 && io.CountLines(" lines_dropped=0") == 1, "stands and the line budget field");
    char buf[2048];
    MinuteCounts m;
    Stands s;
    Require(FormatMinute(buf, sizeof(buf), m, s) > 0 && std::strlen(buf) < 1024, "the minute line stays far below 4096 characters");
    Require(FormatMinute2(buf, sizeof(buf), m) > 0 && std::strlen(buf) < 1024, "minute2 too");
}

// 32-bit ms wrap: the deadlines measure by unsigned difference.
void TestWrap()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t const t0 = 0xFFFFFFFFu - 1500u;
    std::uint32_t t = FormAdhocPair(e, w, r, io, t0);
    t = Tick(e, w, r, io, t, t + 2000);   // wraps
    Require(t < 1000000, "the ms clock wrapped");
    Require(e.Total(Cnt::AckTimeout) == 0 && e.Total(Cnt::GapSuspend) == 0, "over the wrap: no false timeout, no false gap");
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs);
    Require(e.Total(Cnt::AckTimeout) == 2, "the timeout still fires after the wrap");
}

Rules QuestRulesOnly()
{
    Rules r;
    r.mode = 1;
    r.types = kTypeQuest;
    r.statusSeconds = 10;
    r.intervalMs = 1000;
    return r;
}

// Bots 1 and 2 with a wish for quest 300 (objective as given), 100 yd apart; pass 1 forms the QUEST group and queues the join.
void SetQuestPair(World& w, std::uint32_t t, std::int8_t objective)
{
    SetStatus(w, 1, t, 0.0f, 0.0f, 300, objective);
    SetStatus(w, 2, t, 100.0f, 0.0f, 300, objective);
    SetEdge(w, 1, t);
    SetEdge(w, 2, t);
    SetWish(w, 1, t, 300, 0.0f, 0.0f);
    SetWish(w, 2, t, 300, 100.0f, 0.0f);
}

// F13 (a): a QUEST group that never got going (the follower's join refused at push, or its Enter refused) ends as join_stale:
// no attempt for anybody, only the retry cooldown (too_small would charge the leader; MaxAttempts would lock it out).
void TestFormingNoAttempt()
{
    Rules const r = QuestRulesOnly();
    {
        Engine e;
        World w;
        FakeIo io;
        io.joinOk = false;
        SetQuestPair(w, 1000, -1);
        Run(e, w, r, io, 1000);
        Require(e.TotalLeave(EndReason::OpRefused) == 1, "forming: the follower's join refused at push (op_refused)");
        Tick(e, w, r, io, 1000, 3000);
        Require(e.TotalLeave(EndReason::JoinStale) == 1 && e.TotalLeave(EndReason::TooSmall) == 0,
            "forming: the leader left alone ends as join_stale, not too_small");
        Require(e.QuestAttempts(1, 300) == 0 && e.QuestAttempts(2, 300) == 0, "forming: no QUEST attempt (F13 (a))");
        Require(e.Groups().empty(), "forming: the group is gone");
    }
    {
        Engine e;
        World w;
        FakeIo io;
        SetQuestPair(w, 1000, -1);
        Run(e, w, r, io, 1000);
        std::uint32_t const leader = io.joins[0].inviter;
        std::uint32_t const follower = io.joins[0].invitee;
        Refresh(w, 2000);
        w.outcomes.push_back(Outcome(follower, leader, go::OpKind::Join, go::ManagerOpResult::Executed));
        SetEdge(w, follower, 2000, 88, leader, 2);
        SetEdge(w, leader, 2000, 88, leader, 2);
        Run(e, w, r, io, 2000);
        Refresh(w, 3000);
        w.acks.push_back(AckFor(leader, SeqOf(io, leader, Verb::Enter), mi::kAckApplied));
        w.acks.push_back(AckFor(follower, SeqOf(io, follower, Verb::Enter), mi::kAckRefused, std::uint8_t(sv::Deny::PassiveHold)));
        Run(e, w, r, io, 3000);
        Require(e.TotalLeave(EndReason::AckRefused) == 1, "forming: the follower's Enter refused (PASSIVE passive_hold)");
        Require(e.TotalLeave(EndReason::JoinStale) == 1 && e.TotalLeave(EndReason::TooSmall) == 0,
            "forming: the acked leader alone ends as join_stale");
        Require(e.QuestAttempts(1, 300) == 0 && e.QuestAttempts(2, 300) == 0, "forming: a PASSIVE refusal charges no attempt");
    }
}

// 3.1: a QUEST group publishes objective -1 for its key (ReadKey then sums every objective); the Enter keeps the objective.
void TestQuestViewKey()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = QuestRulesOnly();
    SetQuestPair(w, 1000, 2);
    Run(e, w, r, io, 1000);
    std::uint32_t const leader = io.joins[0].inviter;
    std::uint32_t const follower = io.joins[0].invitee;
    Refresh(w, 2000);
    w.outcomes.push_back(Outcome(follower, leader, go::OpKind::Join, go::ManagerOpResult::Executed));
    SetEdge(w, follower, 2000, 88, leader, 2);
    SetEdge(w, leader, 2000, 88, leader, 2);
    Run(e, w, r, io, 2000);
    ViewEntry v;
    Require(e.View().KeyOf(88, v) && v.questId == 300 && v.objective == -1, "QUEST key: the quest, objective -1 (all objectives)");
    Require(!io.posts.empty() && UnpackDirective(io.posts[0].second.b).objective == 2, "the Enter still carries the leader's objective");
}

// 2.3 Ende: a leave's outcome comes before the bot's next edge. Its old edge names the group it left: no orphan leave while the
// group is open, no foreign_group after it is gone; the next edge (another group) releases the hold.
void TestLeaveEdgeLag()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t = Tick(e, w, r, io, t, t + kAckTimeoutMs + 1000);
    Require(io.leaves.size() == 1, "fail closed: the first leave");
    std::uint32_t const first = io.leaves[0].bot;
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(first, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Orphan) == 0 && e.TotalLeave(EndReason::Orphan) == 0, "the left bot's old edge starts no orphan leave");
    Require(io.leaves.size() == 2 && io.leaves[1].bot != first, "the budget goes to the second member's leave");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[1].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    t = Tick(e, w, r, io, t, t + 3000);   // the group is swept, both old edges still name core 77 (2 of 2 members)
    Require(e.Groups().empty() && e.Total(Cnt::Foreign) == 0 && e.TotalLeave(EndReason::ForeignGroup) == 0,
        "old edges of a dissolved manager group: no foreign_group");
    Require(io.leaves.size() == 2 && io.CountLines("action=dissolve") == 0, "no stray leave, no foreign line");
    t += 1000;
    SetEdge(w, 1, t);
    SetEdge(w, 2, t);
    Run(e, w, r, io, t);
    Require(e.Bots().at(1).outGroupId == 0 && e.Bots().at(2).outGroupId == 0, "the next edge (no group) confirms the leave");
}

// M15: steps (2) and (3) run before the gap rule - a clock they set in the hitch pass stays where it is.
void TestGapSameMs()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetStatus(w, 2, 1000, 10.0f, 0.0f);
    SetEdge(w, 1, 1000);
    SetEdge(w, 2, 1000);
    Run(e, w, r, io, 1000);
    std::uint32_t t = 16000;   // a 15 s hitch that also delivers the join's Execute
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.joins[0].invitee, io.joins[0].inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::GapSuspend) == 1 && e.Total(Cnt::JoinFailed) == 0, "the hitch pass: gap_suspend, no join_failed at once");
    t = Tick(e, w, r, io, t, t + kJoinConfirmMs);
    Require(e.Total(Cnt::JoinFailed) == 0, "the join clock runs from the hitch pass");
    t = Tick(e, w, r, io, t, t + 2000);
    Require(e.Total(Cnt::JoinFailed) == 1, "then join_failed after kJoinConfirmMs");
}

// M15 stale anchor: a 45 s hitch (no posts in between) makes no member silent and ends no group.
void TestGapStaleAnchor()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Formed) == 1, "formed");
    t += 45000;
    Run(e, w, r, io, t);   // every snapshot is 45 s old
    Require(e.Total(Cnt::GapSuspend) == 1 && io.CountLines("[GroupMgr] event=gap ms=45000 action=suspend") == 1, "gap line");
    Require(e.Groups().size() == 1 && e.TotalLeave(EndReason::TooSmall) == 0 && e.TotalLeave(EndReason::LeaderLost) == 0,
        "no member silent after the hitch: the group goes on");
    t = Tick(e, w, r, io, t, t + 5000);
    Require(e.Groups().size() == 1 && e.Groups().begin()->second.state == GroupState::Active, "still active after the hitch");
}

// R10: an Exit to a bot the supervisor no longer applies to is done (it cannot be in GROUP): no dir_dropped, no exit_failed.
void TestExitNoSupervisor()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    io.supervisor = false;
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    Require(e.TotalLeave(EndReason::NoSupervisor) == 2, "no supervisor: the group ends (m7)");
    Require(e.Total(Cnt::ExitSkipped) == 2 && e.Total(Cnt::DirDropped) == 0, "the Exits are skipped, not dropped");
    t = Tick(e, w, r, io, t, t + 3 * kAckTimeoutMs);
    Require(e.Total(Cnt::ExitPending) == 0 && e.Total(Cnt::ExitFailed) == 0, "no exit_pending, no exit_failed alarm");
}

// A member that ends while its join is queued: the join may still run. No mark-left on a pre-join edge; the leave goes out once
// an edge shows the core group (no bot left in a group nobody owns).
void TestEndWhileJoinQueued()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    SetStatus(w, 1, 1000, 0.0f, 0.0f);
    SetStatus(w, 2, 1000, 10.0f, 0.0f);
    SetEdge(w, 1, 1000);
    SetEdge(w, 2, 1000);
    Run(e, w, r, io, 1000);
    std::uint32_t const invitee = io.joins[0].invitee;
    std::uint32_t const inviter = io.joins[0].inviter;
    r.types = 0;   // switch off while the join is queued
    Refresh(w, 2000);
    Run(e, w, r, io, 2000);
    Require(e.TotalLeave(EndReason::SwitchOff) == 2 && io.leaves.empty() && e.Total(Cnt::Left) == 0,
        "queued join: nobody marked left, no leave yet");
    Refresh(w, 3000);
    w.outcomes.push_back(Outcome(invitee, inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    Run(e, w, r, io, 3000);   // the edges still predate the join
    Require(io.leaves.empty() && e.Total(Cnt::Left) == 0, "executed, no edge yet: wait, no mark-left");
    SetEdge(w, invitee, 4000, 77, inviter, 2);
    SetEdge(w, inviter, 4000, 77, inviter, 2);
    Refresh(w, 4000);
    Run(e, w, r, io, 4000);
    Require(io.leaves.size() == 1 && io.leaves[0].bot == invitee && io.leaves[0].groupId == 77,
        "the edge shows the core group: the invitee's leave first (non-leader first)");
    Refresh(w, 5000);
    w.outcomes.push_back(Outcome(invitee, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    Run(e, w, r, io, 5000);
    Require(io.leaves.size() == 2 && io.leaves[1].bot == inviter && io.leaves[1].groupId == 77, "then the leader's leave");
}

// 2.1 cohorts: with Manager 2 (cohort B, odd guids) only bot 3 is owned - no pair with bot 4, no foreign leave for bot 4.
void TestCohort()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    r.mode = 2;
    SetStatus(w, 3, 1000, 0.0f, 0.0f);
    SetStatus(w, 4, 1000, 10.0f, 0.0f);
    SetEdge(w, 3, 1000);
    SetEdge(w, 4, 1000);
    std::uint32_t t = 1000;
    Run(e, w, r, io, t);
    t = Tick(e, w, r, io, t, 61000);
    Require(io.joins.empty(), "cohort B: the even bot is no candidate, no pair");
    Require(io.CountLines(" owned=1") == 1, "cohort B: one owned bot in the minute line");
    t += 1000;
    SetEdge(w, 3, t, 60, 3, 2);
    SetEdge(w, 4, t, 60, 3, 2);
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Foreign) == 1 && io.leaves.size() == 1 && io.leaves[0].bot == 3, "a foreign leave only for the owned bot");
}

// M2: a silent member gets no leave op at the end (no leave_unconfirmed); it stays held and gets the orphan leave when it posts
// again from the group.
void TestSilentMember()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    std::uint32_t const leader = io.joins[0].inviter;
    std::uint32_t const quiet = io.joins[0].invitee;
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Formed) == 1, "formed");
    // Only the leader keeps posting.
    std::uint32_t const until = t + mi::kStaleMinMs + 1000;
    while (t < until)
    {
        t += 1000;
        w.status[leader].receivedMs = t;
        w.edges[leader].receivedMs = t;
        Run(e, w, r, io, t);
    }
    Require(e.TotalLeave(EndReason::TooSmall) == 2 && e.Total(Cnt::LeaveSilent) == 1, "the silent member ends without an op");
    Require(io.leaves.size() == 1 && io.leaves[0].bot == leader, "only the leader's leave goes out");
    Require(e.Bots().count(quiet) && e.Bots().at(quiet).staleGroupId == 77, "the silent bot stays held on its group");
    t += 1000;
    w.status[leader].receivedMs = t;
    w.edges[leader].receivedMs = t;
    w.status[quiet].receivedMs = t;
    w.edges[quiet].receivedMs = t;   // it posts again, still in core group 77
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Orphan) == 1 && io.leaves.size() == 2 && io.leaves[1].bot == quiet, "back from silence in the group: orphan");
    Require(e.Total(Cnt::LeaveUnconfirmed) == 0, "no leave_unconfirmed alarm");
}

// m6: a member whose leave waits in the op queue still reports the core group - no false real_player. Also join n+1 carries
// the confirmed core id (M8).
void TestRealPlayerLeaving()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = QuestRulesOnly();
    for (std::uint32_t low = 1; low <= 3; ++low)
    {
        SetStatus(w, low, 1000, float(low - 1) * 10.0f, 0.0f, 300, -1);
        SetEdge(w, low, 1000);
        SetWish(w, low, 1000, 300, float(low - 1) * 10.0f, 0.0f);
    }
    Run(e, w, r, io, 1000);
    Require(io.joins.size() == 1 && io.joins[0].invitee == 2 && io.joins[0].inviter == 1 && io.joins[0].groupId == 0,
        "QUEST of 3: the first join makes the group");
    Refresh(w, 2000);
    w.outcomes.push_back(Outcome(2, 1, go::OpKind::Join, go::ManagerOpResult::Executed));
    SetEdge(w, 1, 2000, 88, 1, 2);
    SetEdge(w, 2, 2000, 88, 1, 2);
    Run(e, w, r, io, 2000);
    Require(io.joins.size() == 2 && io.joins[1].invitee == 3 && io.joins[1].groupId == 88, "join n+1 with the confirmed core id (M8)");
    Refresh(w, 3000);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    w.outcomes.push_back(Outcome(3, 1, go::OpKind::Join, go::ManagerOpResult::Executed));
    for (std::uint32_t low = 1; low <= 3; ++low)
        SetEdge(w, low, 3000, 88, 1, 3);
    Run(e, w, r, io, 3000);
    Require(e.Total(Cnt::Formed) == 1 && e.Total(Cnt::Joined) == 3, "formed with two acked, the third joined");
    std::uint32_t t = Tick(e, w, r, io, 3000, 3000 + kAckTimeoutMs + 1000);
    Require(e.TotalLeave(EndReason::AckTimeout) == 1 && io.leaves.size() == 1 && io.leaves[0].bot == 3,
        "the third member's Enter times out: its leave is queued (no outcome yet)");
    t = Tick(e, w, r, io, t, t + kRealPlayerGraceMs + 5000);
    Require(e.TotalLeave(EndReason::RealPlayer) == 0 && e.Groups().size() == 1, "a leaving member still reports: no real_player");
}

// Ends the pair of FormAdhocPair after r.types went to 0: pass 1 ends both (Exit + the first leave), pass 2 executes it and
// queues the second, pass 3 executes that, the edges show no group and both Exits are applied. Returns the time of pass 3.
std::uint32_t SwitchOffPair(Engine& e, World& w, Rules const& r, FakeIo& io, std::uint32_t t)
{
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    Require(io.leaves.size() == 1, "switch off: the first leave");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[0].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    Run(e, w, r, io, t);
    Require(io.leaves.size() == 2, "switch off: the second leave");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[1].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    SetEdge(w, 1, t);
    SetEdge(w, 2, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Exit), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Exit), mi::kAckApplied));
    Run(e, w, r, io, t);
    return t;
}

std::uint32_t MaxSeq(FakeIo const& io)
{
    std::uint32_t seq = 0;
    for (auto const& p : io.posts)
        seq = std::max<std::uint32_t>(seq, p.second.a);
    return seq;
}

// 9b R3: the group serial and the directive seq are never reset - an emptied registry after a switch off and on hands out the
// next serial and the next seq, so an old directive or ack can never match a new group.
void TestSerialAcrossOffOn()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    std::uint16_t const firstSerial = UnpackDirective(io.posts.back().second.b).serial;
    Require(firstSerial != 0, "the first group has a serial on the wire");
    r.types = 0;
    t = SwitchOffPair(e, w, r, io, t);
    Require(!e.HasGroups() && !e.View().Any(), "off: the registry is empty");
    std::uint32_t const seqBefore = MaxSeq(io);
    // On again with a new pair (bots 3 and 4).
    r.types = kTypeAdhoc;
    w.status.clear();
    w.edges.clear();
    t += 1000;
    SetStatus(w, 3, t, 0.0f, 0.0f);
    SetStatus(w, 4, t, 10.0f, 0.0f);
    SetEdge(w, 3, t);
    SetEdge(w, 4, t);
    std::size_t const joins = io.joins.size();
    Run(e, w, r, io, t);
    Require(io.joins.size() == joins + 1, "on: the new pair's join");
    JoinCall const j = io.joins.back();
    std::size_t const posts = io.posts.size();
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(j.invitee, j.inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
    SetEdge(w, j.invitee, t, 78, j.inviter, 2);
    SetEdge(w, j.inviter, t, 78, j.inviter, 2);
    Run(e, w, r, io, t);
    Require(io.posts.size() == posts + 2, "on: two Enters for the new group");
    for (std::size_t i = posts; i < io.posts.size(); ++i)
    {
        DirectiveBits const d = UnpackDirective(io.posts[i].second.b);
        Require(d.verb == Verb::Enter && d.serial > firstSerial, "the new group's serial follows the old one (no reset)");
        Require(io.posts[i].second.a > seqBefore, "the new seqs follow every old one (no reset, never 0)");
    }
}

// The wire carries the serial's low 16 bits only: NextGroupSerial skips 0 there and the low 16 bits of a live group.
void TestSerialLow16()
{
    auto const none = [](std::uint16_t) { return false; };
    auto const live1 = [](std::uint16_t low16) { return low16 == 1; };
    Require(NextGroupSerial(0, none) == 1u && NextGroupSerial(41, none) == 42u, "monotonic");
    Require(NextGroupSerial(0xFFFFu, none) == 0x10001u, "a serial whose low 16 bits are 0 is skipped");
    Require(NextGroupSerial(0xFFFFFFFFu, none) == 1u, "the 32-bit wrap skips 0");
    Require(NextGroupSerial(0xFFFFu, live1) == 0x10002u, "low 16 bits 0, then a collision with live group 1: both skipped");
    Require(NextGroupSerial(0xFFFFFFFFu, live1) == 2u, "after the wrap, serial 1 is still live: skipped");
    Require(NextGroupSerial(5, live1) == 6u, "no collision: the next value");
}

// 3.4 / R4: an ack counts only for its own directive (this bot's open Exit or its group's Enter, by seq); any other ack - an
// unknown seq, another bot's seq, a bot nobody holds, any state - is ack_stale and changes nothing.
void TestAckStale()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    std::uint32_t const seq1 = SeqOf(io, 1, Verb::Enter);
    std::uint32_t const seq2 = SeqOf(io, 2, Verb::Enter);
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, seq2, mi::kAckRefused, std::uint8_t(sv::Deny::Requester)));
    w.acks.push_back(AckFor(2, seq1, mi::kAckApplied));
    w.acks.push_back(AckFor(1, 0x7FFFFFF0u, mi::kAckReceived));
    w.acks.push_back(AckFor(3, seq1, mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::AckStale) == 4 && e.Total(Cnt::AckRcv) == 0 && e.Total(Cnt::AckApp) == 0 && e.Total(Cnt::AckRef) == 0,
        "four non-matching acks: ack_stale each, no other count");
    Require(e.Groups().size() == 1 && Posts(io, Verb::Exit) == 0 && io.leaves.empty() && io.CountLines("event=ack") == 0,
        "a stale ack: no end, no Exit, no leave, no ack line");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, seq1, mi::kAckReceived));
    w.acks.push_back(AckFor(1, seq1, mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::AckRcv) == 1 && e.Total(Cnt::AckApp) == 1 && e.Total(Cnt::AckStale) == 4, "its own seq: received, applied");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, seq1, mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::AckStale) == 5 && e.Total(Cnt::AckApp) == 1, "the same ack again (already acked): stale");
}

// 2.8 + F8 (a): on switch_off every member gets an Exit (reason switch_off, its group's serial) with its leave; a refused Exit
// goes once more in a later pass; the registry keeps the bot until its Exit is answered. Without the supervisor: exit_skipped.
void TestSwitchOffExits()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t);
    r.types = 0;
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    Require(Posts(io, Verb::Exit) == 2 && io.leaves.size() == 1, "switch off: both Exits go out with the first leave");
    for (auto const& p : io.posts)
    {
        DirectiveBits const d = UnpackDirective(p.second.b);
        if (d.verb == Verb::Exit)
            Require(d.reason == EndReason::SwitchOff && d.serial == serial, "the Exit: reason switch_off, the group's serial");
    }
    std::uint32_t const firstExit = SeqOf(io, 1, Verb::Exit);
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[0].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    w.acks.push_back(AckFor(1, firstExit, mi::kAckRefused, std::uint8_t(sv::Deny::Requester)));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::ExitPending) == 1 && Posts(io, Verb::Exit) == 2 && io.leaves.size() == 2,
        "bot 1's Exit refused: exit_pending, not resent in that pass; the second leave");
    t += 1000;
    Refresh(w, t);
    w.outcomes.push_back(Outcome(io.leaves[1].bot, 0, go::OpKind::Leave, go::ManagerOpResult::Executed));
    SetEdge(w, 1, t);
    SetEdge(w, 2, t);
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Exit), mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(Posts(io, Verb::Exit) == 3 && SeqOf(io, 1, Verb::Exit) != firstExit, "the one Exit retry for bot 1");
    ViewEntry v;
    Require(e.Groups().empty() && e.Bots().count(1) && !e.Bots().count(2) && e.View().Find(1, v) && !e.View().Find(2, v),
        "both left: bot 2 (Exit applied) is free, bot 1 stays held until its Exit is answered");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Exit), mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(!e.HasGroups() && !e.View().Any() && e.Total(Cnt::ExitFailed) == 0, "the retry applied: nothing held");

    // The supervisor no longer applies (R10): the Exits are skipped, nothing waits for an ack.
    Engine e2;
    World w2;
    FakeIo io2;
    Rules r2 = AdhocRules();
    std::uint32_t t2 = FormAdhocPair(e2, w2, r2, io2, 1000);
    io2.supervisor = false;
    r2.types = 0;
    t2 += 1000;
    Refresh(w2, t2);
    Run(e2, w2, r2, io2, t2);
    Require(e2.TotalLeave(EndReason::SwitchOff) == 2 && e2.Total(Cnt::ExitSkipped) == 2 && Posts(io2, Verb::Exit) == 0,
        "switch off without the supervisor: exit_skipped for both");
}

// The T02-GROUP read: IsManaged(guid, serial) is true only for a live member of the group with these low 16 bits; another
// serial, serial 0, a bot not held, an ending member and an empty registry answer false (IsManaged(guid) stays true while held).
void TestManagedSerialView()
{
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    Require(!e.View().IsMember(1, 1), "an empty registry: false (the atomic fast path)");
    std::uint32_t t = FormAdhocPair(e, w, r, io, 1000);
    std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
    Require(e.View().IsMember(1, serial) && e.View().IsMember(2, serial), "both members are live under the Enter's serial");
    Require(!e.View().IsMember(1, std::uint16_t(serial + 1)) && !e.View().IsMember(1, 0) && !e.View().IsMember(3, serial),
        "another serial, serial 0, a bot not held: false");
    r.types = 0;
    t += 1000;
    Refresh(w, t);
    Run(e, w, r, io, t);
    ViewEntry v;
    Require(!e.View().IsMember(1, serial) && !e.View().IsMember(2, serial) && e.View().Find(1, v) && e.View().Find(2, v),
        "ending members (Exit sent): not live, still held");
}

// ---- T30-1b ----------------------------------------------------------------------------------------------------------------
// A left ack (state 4, seq 0) with the serial the bot holds (b16-31 of the state word).
Ack LeftAck(std::uint32_t low, std::uint16_t serial, std::uint8_t reason, std::uint32_t seq = 0)
{
    Ack a = AckFor(low, seq, mi::kAckLeft, reason);
    a.serial = serial;
    return a;
}

// The pair of FormAdhocPair, both Enters applied in the next pass: the group is formed. Returns the time of that pass.
std::uint32_t FormAckedPair(Engine& e, World& w, Rules const& r, FakeIo& io, std::uint32_t t0)
{
    std::uint32_t t = FormAdhocPair(e, w, r, io, t0) + 1000;
    Refresh(w, t);
    w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
    w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::Formed) == 1 && e.Total(Cnt::AckApp) == 2, "the pair is formed (both Enters applied)");
    return t;
}

// The names and the classification of the T30-1b reasons (G2, G5, G6).
void TestLeftReasons()
{
    Require(std::string(EndReasonName(EndReason::BotLeftDone)) == "bot_left_done" &&
        std::string(EndReasonName(EndReason::BotLeftLost)) == "bot_left_lost" &&
        std::string(EndReasonName(EndReason::BotLeftOff)) == "bot_left_off", "the three bot_left reason names");
    Require(std::size_t(EndReason::BotLeftOff) + 1 == kEndReasons && kEndReasons <= 128, "appended right before Count, fits b9-15");
    DirectiveBits d;
    d.verb = Verb::Exit;
    d.reason = EndReason::BotLeftOff;
    Require(UnpackDirective(PackDirective(d)).reason == EndReason::BotLeftOff, "an Exit carries bot_left_off");
    Require(std::string(AckReasonName(21)) == "legacy_group_role" && std::string(AckReasonName(22)) == "arbiter_off" &&
        std::string(AckReasonName(23)) == "lane_busy" && std::string(AckReasonName(24)) == "goal_not_in_log" &&
        std::string(AckReasonName(25)) == "focus_conflict" && std::string(AckReasonName(26)) == "goal_done" &&
        std::string(AckReasonName(27)) == "goal_lost" && std::string(AckReasonName(28)) == "unknown", "ack reasons 21-27 (T02-GROUP)");
    Require(std::string(AckStateName(mi::kAckLeft)) == "left" && mi::kAckLeft == 4 && std::string(AckStateName(5)) == "unknown",
        "ack state 4 = left");
    Require(FromLeftReason(kAckReasonGoalDone) == EndReason::BotLeftDone && FromLeftReason(kAckReasonGoalLost) == EndReason::BotLeftLost &&
        FromLeftReason(kAckReasonArbiterOff) == EndReason::BotLeftOff && FromLeftReason(0) == EndReason::BotLeftOff,
        "left reason 26 done, 27 lost, else off");
    Require(!CountsAsQuestAttempt(EndReason::BotLeftDone) && !CountsAsQuestAttempt(EndReason::BotLeftLost) &&
        !CountsAsQuestAttempt(EndReason::BotLeftOff), "no bot_left reason is an S7 attempt");
    Require(IsNaturalEnd(EndReason::BotLeftDone) && !IsNaturalEnd(EndReason::BotLeftLost) && !IsNaturalEnd(EndReason::BotLeftOff) &&
        IsNaturalEnd(EndReason::TurnedIn) && IsNaturalEnd(EndReason::QuestTurnedIn) && IsNaturalEnd(EndReason::ObjectiveDone) &&
        IsNaturalEnd(EndReason::GroupDone) && !IsNaturalEnd(EndReason::NoProgress), "the natural ends (G5)");
    Require(IsManagerReason(EndReason::BotLeftLost) && IsManagerReason(EndReason::BotLeftOff) && !IsManagerReason(EndReason::BotLeftDone),
        "bot_left_lost / bot_left_off are the manager's own, bot_left_done is not");
}

// The state word: b0-7 the state, b16-31 the serial; an old-style ack (state 2, upper bits 0) reads and matches as before.
void TestAckWord()
{
    mi::Event ev{};
    ev.botLow = 5;
    ev.kind = mi::Kind::DirectiveAck;
    ev.seq = 42;
    ev.mapOrDungeon = mi::kAckApplied;
    Ack a = AckOf(ev);
    Require(a.low == 5 && a.seq == 42 && a.state == mi::kAckApplied && a.serial == 0 && a.reason == 0, "state 2, serial bits 0");
    ev.seq = 0;
    ev.mapOrDungeon = (std::uint32_t(0xBEEF) << 16) | mi::kAckLeft;
    ev.reason = kAckReasonGoalLost;
    a = AckOf(ev);
    Require(a.state == mi::kAckLeft && a.serial == 0xBEEF && a.seq == 0 && a.reason == 27, "state 4 with the serial in b16-31");

    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t const t = FormAdhocPair(e, w, r, io, 1000) + 1000;
    Refresh(w, t);
    for (std::uint32_t low = 1; low <= 2; ++low)
    {
        mi::Event old{};
        old.botLow = low;
        old.kind = mi::Kind::DirectiveAck;
        old.seq = SeqOf(io, low, Verb::Enter);
        old.mapOrDungeon = mi::kAckApplied;
        w.acks.push_back(AckOf(old));
    }
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::AckApp) == 2 && e.Total(Cnt::AckStale) == 0 && e.Total(Cnt::Formed) == 1 && e.Total(Cnt::SelfLeft) == 0,
        "old-style acks (state 2, upper bits 0) match their Enters as before");
}

// G2: a left ack with the serial of the bot's current group, its Enter applied, no Exit open: self_left, the member ends with
// bot_left_done, the tracked Exit goes out and the leave is queued.
void TestSelfLeft()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAckedPair(e, w, r, io, 1000);
    std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
    std::uint32_t const bot = io.joins[0].invitee;   // not the leader: its leave goes first (2.3 Ende)
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(LeftAck(bot, serial, kAckReasonGoalDone));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::SelfLeft) == 1 && e.Total(Cnt::AckStale) == 0, "a matching left ack: self_left, not stale");
    Require(e.TotalLeave(EndReason::BotLeftDone) == 1, "the member ends with bot_left_done");
    Require(io.CountLines("[GroupMgr] event=self_left group=") == 1 && io.CountLines(" reason=goal_done") == 1 &&
        io.CountLines(" reason=bot_left_done ") == 1, "one self_left line (ack reason name) and the leave line");
    bool exitPosted = false;
    for (auto const& p : io.posts)
    {
        DirectiveBits const d = UnpackDirective(p.second.b);
        if (p.first == bot && d.verb == Verb::Exit)
            exitPosted = d.reason == EndReason::BotLeftDone && d.serial == serial;
    }
    Require(exitPosted, "the tracked Exit: reason bot_left_done, the group's serial");
    Require(e.Bots().count(bot) && e.Bots().at(bot).exitOpen, "the Exit waits for its ack");
    Require(!io.leaves.empty() && io.leaves[0].bot == bot && io.leaves[0].groupId == 77, "the leave op for the bot");
    // bot_left_done is a natural end and the partner's too_small a follow-on end: no osc at all.
    Require(e.TotalLeave(EndReason::TooSmall) == 1 && e.Total(Cnt::Osc) == 0, "osc: none for bot_left_done and its too_small");
}

// G2: every other left ack is ack_stale and nothing else.
void TestLeftAckStale()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    std::uint32_t t = FormAckedPair(e, w, r, io, 1000);
    std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
    std::size_t const posts = io.posts.size();
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(LeftAck(1, std::uint16_t(serial + 1), kAckReasonGoalDone));   // another serial
    w.acks.push_back(LeftAck(1, serial, kAckReasonGoalDone, SeqOf(io, 1, Verb::Enter)));   // seq != 0
    w.acks.push_back(LeftAck(3, serial, kAckReasonGoalDone));   // a bot nobody holds
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::AckStale) == 3 && e.Total(Cnt::SelfLeft) == 0, "wrong serial, seq != 0, no group: ack_stale each");
    Require(e.Groups().size() == 1 && io.posts.size() == posts && io.leaves.empty() && io.CountLines("event=self_left") == 0 &&
        io.CountLines("event=leave ") == 0, "a stale left ack: no end, no Exit, no leave, no line");
    // The member is no longer Acked and its Exit is open (after a matching left ack): a second one is stale.
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(LeftAck(1, serial, kAckReasonGoalLost));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::SelfLeft) == 1 && e.TotalLeave(EndReason::BotLeftLost) == 1, "the matching one ends bot 1 (bot_left_lost)");
    Require(e.Bots().count(1) && e.Bots().at(1).exitOpen, "bot 1's Exit is open");
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(LeftAck(1, serial, kAckReasonGoalLost));
    Run(e, w, r, io, t);
    Require(e.Total(Cnt::SelfLeft) == 1 && e.Total(Cnt::AckStale) == 4 && e.TotalLeave(EndReason::BotLeftLost) == 1,
        "a left ack while its Exit is open: ack_stale only");
    // A member whose Enter is not applied yet (Directed): stale.
    Engine e2;
    World w2;
    FakeIo io2;
    std::uint32_t t2 = FormAdhocPair(e2, w2, r, io2, 1000);
    std::uint16_t const serial2 = UnpackDirective(io2.posts.back().second.b).serial;
    t2 += 1000;
    Refresh(w2, t2);
    w2.acks.push_back(LeftAck(1, serial2, kAckReasonArbiterOff));
    Run(e2, w2, r, io2, t2);
    Require(e2.Total(Cnt::AckStale) == 1 && e2.Total(Cnt::SelfLeft) == 0 && e2.TotalLeave(EndReason::BotLeftOff) == 0,
        "a left ack before ack 2: ack_stale only");
}

// G5: osc counts an end without a natural reason less than kOscSec after the Enter's ack 2.
void TestOsc()
{
    Require(kOscSec == 900, "kOscSec = 15 minutes");
    Require(CountsAsOsc(true, 1000, 1000 + 899, EndReason::NoProgress) && !CountsAsOsc(true, 1000, 1000 + 900, EndReason::NoProgress),
        "no_progress within 15 minutes of the ack: osc; at 15 minutes: none");
    Require(!CountsAsOsc(true, 1000, 1001, EndReason::TurnedIn) && !CountsAsOsc(true, 1000, 1001, EndReason::BotLeftDone) &&
        !CountsAsOsc(true, 1000, 1001, EndReason::ObjectiveDone), "a natural end is no osc");
    Require(!CountsAsOsc(false, 0, 1, EndReason::NoProgress), "no ack 2: no osc");
    Require(CountsAsOsc(true, 1000, 1001, EndReason::BotLeftLost) && CountsAsOsc(true, 1000, 1001, EndReason::BotLeftOff),
        "bot_left_lost / bot_left_off are osc");
    Require(!CountsAsOsc(true, 1000, 1001, EndReason::TooSmall) && !CountsAsOsc(true, 1000, 1001, EndReason::SwitchOff),
        "too_small (follow-on) and switch_off (operator) are no osc");
    // In the engine: a goal_lost self-exit 1 s after the acks is one osc; the partner's too_small is none.
    Engine e;
    World w;
    FakeIo io;
    Rules r = AdhocRules();
    std::uint32_t t = FormAckedPair(e, w, r, io, 1000);
    std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
    std::uint32_t const bot = io.joins[0].invitee;
    t += 1000;
    Refresh(w, t);
    w.acks.push_back(LeftAck(bot, serial, kAckReasonGoalLost));
    Run(e, w, r, io, t);
    Require(e.TotalLeave(EndReason::BotLeftLost) == 1 && e.TotalLeave(EndReason::TooSmall) == 1 && e.Total(Cnt::Osc) == 1,
        "goal_lost within 15 minutes: one osc, none for the partner's too_small");
    // switch_off right after the acks: no osc.
    Engine e3;
    World w3;
    FakeIo io3;
    Rules r3 = AdhocRules();
    std::uint32_t t3 = FormAckedPair(e3, w3, r3, io3, 1000);
    r3.types = 0;
    t3 += 1000;
    Refresh(w3, t3);
    Run(e3, w3, r3, io3, t3);
    Require(e3.TotalLeave(EndReason::SwitchOff) == 2 && e3.Total(Cnt::Osc) == 0, "switch_off: no osc");
    // Ended before ack 2 (ack_timeout): no osc.
    Engine e2;
    World w2;
    FakeIo io2;
    Rules const r2 = AdhocRules();
    std::uint32_t t2 = FormAdhocPair(e2, w2, r2, io2, 1000);
    t2 = Tick(e2, w2, r2, io2, t2, t2 + kAckTimeoutMs + 1000);
    Require(e2.TotalLeave(EndReason::AckTimeout) == 2 && e2.Total(Cnt::Osc) == 0, "no ack 2, no osc");
}

// F13 (a) for the left reasons: bot_left_done forgets the S7 entry (no attempt, no cooldown); bot_left_lost sets the retry
// cooldown only.
void TestLeftQuest()
{
    Rules r;
    r.mode = 1;
    r.types = kTypeQuest;
    r.statusSeconds = 10;
    r.intervalMs = 1000;
    for (std::uint8_t const reason : { kAckReasonGoalDone, kAckReasonGoalLost })
    {
        Engine e;
        World w;
        FakeIo io;
        SetStatus(w, 1, 1000, 0.0f, 0.0f, 300, -1);
        SetStatus(w, 2, 1000, 100.0f, 0.0f, 300, -1);
        SetEdge(w, 1, 1000);
        SetEdge(w, 2, 1000);
        SetWish(w, 1, 1000, 300, 0.0f, 0.0f);
        SetWish(w, 2, 1000, 300, 100.0f, 0.0f);
        Run(e, w, r, io, 1000);
        Require(io.joins.size() == 1, "QUEST: a join for the wish");
        std::uint32_t const inviter = io.joins[0].inviter;
        std::uint32_t const invitee = io.joins[0].invitee;
        Refresh(w, 2000);
        w.outcomes.push_back(Outcome(invitee, inviter, go::OpKind::Join, go::ManagerOpResult::Executed));
        SetEdge(w, invitee, 2000, 88, inviter, 2);
        SetEdge(w, inviter, 2000, 88, inviter, 2);
        Run(e, w, r, io, 2000);
        std::uint16_t const serial = UnpackDirective(io.posts.back().second.b).serial;
        Refresh(w, 3000);
        w.acks.push_back(AckFor(1, SeqOf(io, 1, Verb::Enter), mi::kAckApplied));
        w.acks.push_back(AckFor(2, SeqOf(io, 2, Verb::Enter), mi::kAckApplied));
        Run(e, w, r, io, 3000);
        Require(e.Total(Cnt::Formed) == 1, "QUEST group formed");
        Refresh(w, 4000);
        w.acks.push_back(LeftAck(invitee, serial, reason));
        Run(e, w, r, io, 4000);
        Require(e.Total(Cnt::SelfLeft) == 1, "QUEST: the left ack ends the member");
        Require(e.QuestAttempts(invitee, 300) == 0, "a left reason is no QUEST attempt");
        if (reason == kAckReasonGoalDone)
            Require(e.TotalLeave(EndReason::BotLeftDone) == 1 && !e.QuestCooldown(invitee, 300, SecOf(w, 4000)),
                "bot_left_done: the entry is forgotten like turned_in (no cooldown)");
        else
            Require(e.TotalLeave(EndReason::BotLeftLost) == 1 && e.QuestCooldown(invitee, 300, SecOf(w, 4000)),
                "bot_left_lost: the retry cooldown only");
    }
}

// OB-10 smoke note: skip_unknown_30m at the end of minute2 is a level - the bots skip_unknown without a break for >= 30 min.
bool EndsWith(std::string const& s, char const* suffix)
{
    std::size_t const n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string LastLine(FakeIo const& io, char const* needle)
{
    std::string last;
    for (std::string const& l : io.lines)
        if (l.find(needle) != std::string::npos)
            last = l;
    return last;
}

void TestSkipUnknownLong()
{
    Engine e;
    World w;
    FakeIo io;
    Rules const r = AdhocRules();
    SetStatus(w, 3, 1000, 500.0f, 0.0f, 0);
    SetEdge(w, 3, 1000, 60, 3, 2);   // a member that posts no edge: skip_unknown every pass
    Run(e, w, r, io, 1000);
    std::uint32_t t = Tick(e, w, r, io, 1000, 1000 + 1700000);
    Require(io.CountLines(" skip_unknown_30m=1") == 0 && io.CountLines(" skip_unknown_30m=0") > 0, "under 30 minutes: 0");
    Require(EndsWith(LastLine(io, "[GroupMgr] minute2 "), " osc=0 self_left=0 skip_unknown_30m=0"),
        "minute2 ends with osc, self_left, skip_unknown_30m");
    Require(LastLine(io, "[GroupMgr] minute types=").find(" osc=") == std::string::npos, "the minute line keeps its fields");
    t = Tick(e, w, r, io, t, 1000 + 1900000);
    Require(EndsWith(LastLine(io, "[GroupMgr] minute2 "), " skip_unknown_30m=1"), "skip_unknown for over 30 minutes: 1");
    SetEdge(w, 3, t);   // out of the group: the run ends
    t = Tick(e, w, r, io, t, t + 61000);
    Require(EndsWith(LastLine(io, "[GroupMgr] minute2 "), " skip_unknown_30m=0"), "no longer skip_unknown: 0 again (a level)");
    SetEdge(w, 3, t, 60, 3, 2);   // back in: a new run starts from 0
    t = Tick(e, w, r, io, t, t + 61000);
    Require(EndsWith(LastLine(io, "[GroupMgr] minute2 "), " skip_unknown_30m=0"), "a new run counts from its own start");
}
}

int main()
{
    TestDirectiveBits();
    TestReasons();
    TestCandidate();
    TestTypesZero();
    TestAdhocAcked();
    TestAckTimeoutFailClosed();
    TestJoinClock();
    TestGapRule();
    TestResume();
    TestDirectiveDropped();
    TestExitRetry();
    TestSwitchOff();
    TestForeignGroup();
    TestQuestAttempts();
    TestWishLeftover();
    TestMinuteLine();
    TestWrap();
    TestFormingNoAttempt();
    TestQuestViewKey();
    TestLeaveEdgeLag();
    TestGapSameMs();
    TestGapStaleAnchor();
    TestExitNoSupervisor();
    TestEndWhileJoinQueued();
    TestCohort();
    TestSilentMember();
    TestRealPlayerLeaving();
    TestSerialAcrossOffOn();
    TestSerialLow16();
    TestAckStale();
    TestSwitchOffExits();
    TestManagedSerialView();
    TestLeftReasons();
    TestAckWord();
    TestSelfLeft();
    TestLeftAckStale();
    TestOsc();
    TestLeftQuest();
    TestSkipUnknownLong();
    std::cout << "group_directive_policy_tests: OK\n";
    return 0;
}
