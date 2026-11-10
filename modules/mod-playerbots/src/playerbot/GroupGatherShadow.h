#pragma once

#include "GroupGatherPolicy.h"
#include "GroupDirectivePolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <vector>

// A3 step 6 T33-0 "Fakten + Schatten" (design T33 v1.1 7, 5.2, 5.3; AiPlayerbot.BotGroups.Manager.Gather, default 0).
// MEASUREMENT ONLY: for every manager QUEST group the shadow judges the meeting with the T33 rule (ArrivedForGroup against
// the leader's last GatherPlace, the leader not judged, F7 (a)) next to the T30 rule (Engine QuestRules, read from the phase
// T30 leaves the pass with) and runs a virtual deadline on the manager clock (anchor = formed, F16 (a); RendezvousMinutes).
// It reads the engine's registry after the pass and never writes it: no directive, no op, no end, no phase change. The world
// thread runs it after the directive pass (GroupManager.cpp RunGather), only while Gather != 0; the lines go through the
// manager's one log line (Io::Line). ADHOC groups are not measured (design 7 T33-0 row; 2.1 also names an ADHOC counter).
namespace ai::group_mgr
{
using PlaceTable = std::unordered_map<std::uint32_t, PlaceSnapshot>;

// Totals since the shadow was switched on; the minute line prints their increments (5.3).
struct GatherCounts
{
    std::uint64_t regionMismatch = 0;   // arrivals on a continent with another raw instance id (F7 (a): counted only)
    std::uint64_t zOut = 0;             // arrivals with |dz| > 15 yd (F7 (a): counted only)
    std::uint64_t jumpSame = 0;         // a GatherPlace > 200 yd from the bot's previous one, same map (2.8, descriptive)
    std::uint64_t jumpOther = 0;        // a GatherPlace on another map than the bot's previous one
    std::uint64_t resume = 0;           // passes after an off gap (open meetings got a fresh deadline)
    std::uint64_t arrived = 0;          // members judged arrived, once per member and meeting
    std::uint64_t deadAtPoint = 0;      // of those, dead at the point (F12 (a): they count as there)
    std::uint64_t gathered = 0;         // meetings the T33 rule saw gathered
    std::uint64_t compared = 0;         // passes in which T30 judged the rendezvous and the T33 verdict was compared
    std::uint64_t disagree = 0;         // of those, passes in which the two verdicts differ
    std::uint64_t t33First = 0;         // meetings with a pass where T33 saw gathered and T30 did not (once per meeting)
    std::uint64_t t30First = 0;         // meetings with a pass where T30 saw gathered and T33 did not (once per meeting)
    std::uint64_t legExpired = 0;       // meetings whose virtual deadline on the manager clock passed before T33 saw them gathered
    std::uint64_t t30Cut = 0;           // meetings T30 ended with rendezvous_timeout before the virtual deadline (wall clock)
    std::uint64_t linesDropped = 0;     // shadow event lines over the minute cap
};

inline GatherCounts GatherDelta(GatherCounts const& now, GatherCounts const& seen)
{
    GatherCounts d;
    d.regionMismatch = now.regionMismatch - seen.regionMismatch;
    d.zOut = now.zOut - seen.zOut;
    d.jumpSame = now.jumpSame - seen.jumpSame;
    d.jumpOther = now.jumpOther - seen.jumpOther;
    d.resume = now.resume - seen.resume;
    d.arrived = now.arrived - seen.arrived;
    d.deadAtPoint = now.deadAtPoint - seen.deadAtPoint;
    d.gathered = now.gathered - seen.gathered;
    d.compared = now.compared - seen.compared;
    d.disagree = now.disagree - seen.disagree;
    d.t33First = now.t33First - seen.t33First;
    d.t30First = now.t30First - seen.t30First;
    d.legExpired = now.legExpired - seen.legExpired;
    d.t30Cut = now.t30Cut - seen.t30Cut;
    d.linesDropped = now.linesDropped - seen.linesDropped;
    return d;
}

// "[GroupMgr] minute3 ...": the T33-0 fields of design 5.3 in its order (T33-1 inserts its lever fields after gather=, as in
// 5.3), then the T33-0 additions; fixed. Increments except gather (the switch), places (fresh entries of the table) and open
// (open shadow meetings). shadow_disagree and shadow_compared count passes, shadow_t33_first / shadow_t30_first meetings.
inline int FormatGatherMinute(char* buf, std::size_t size, std::uint32_t gather, GatherCounts const& d, std::uint64_t places,
    std::uint64_t open)
{
    return std::snprintf(buf, size, "[GroupMgr] minute3 gather=%u region_mismatch=%llu z_out=%llu place_jump_same=%llu"
        " place_jump_other=%llu gather_resume=%llu shadow_arrived=%llu shadow_disagree=%llu shadow_leg_expired=%llu places=%llu"
        " open=%llu shadow_gathered=%llu shadow_compared=%llu shadow_t33_first=%llu shadow_t30_first=%llu shadow_t30_cut=%llu"
        " dead_at_point=%llu shadow_lines_dropped=%llu",
        gather, (unsigned long long)d.regionMismatch, (unsigned long long)d.zOut, (unsigned long long)d.jumpSame,
        (unsigned long long)d.jumpOther, (unsigned long long)d.resume, (unsigned long long)d.arrived,
        (unsigned long long)d.disagree, (unsigned long long)d.legExpired, (unsigned long long)places, (unsigned long long)open,
        (unsigned long long)d.gathered, (unsigned long long)d.compared, (unsigned long long)d.t33First,
        (unsigned long long)d.t30First, (unsigned long long)d.t30Cut, (unsigned long long)d.deadAtPoint,
        (unsigned long long)d.linesDropped);
}

// "[GroupMgr] event=phase ...": one line per phase a manager QUEST group enters (rendezvous at formed), in both arms (K1-5);
// ms = formed -> now on the manager clock; gather = the group's arm by its leader (F8 (a)).
inline int FormatPhaseLine(char* buf, std::size_t size, std::uint32_t serial, quest_groups::Phase phase, std::uint64_t ms, bool arm)
{
    return std::snprintf(buf, size, "[GroupMgr] event=phase group=%u phase=%s ms=%llu gather=%u", serial,
        quest_groups::PhaseName(phase), (unsigned long long)ms, arm ? 1u : 0u);
}

// How a shadow meeting closed (event=shadow_end).
enum class ShadowEnd : std::uint8_t
{
    Gathered = 0,   // the T33 rule saw every judged member arrived
    Expired,        // the virtual deadline on the manager clock passed first (8.3 deadline expiries)
    T30Cut,         // T30 ended the group with rendezvous_timeout before the virtual deadline
    Closed,         // any other T30 end, or the registry dropped the group: no verdict
};

inline char const* ShadowEndName(ShadowEnd r)
{
    switch (r)
    {
        case ShadowEnd::Gathered: return "gathered";
        case ShadowEnd::Expired: return "expired";
        case ShadowEnd::T30Cut: return "t30_cut";
        case ShadowEnd::Closed: return "closed";
    }
    return "closed";
}

// T30's rendezvous timeout runs on whole seconds of time() from formedSec (QuestGroupPolicy), the virtual deadline on the
// manager's ms clock from the same pass: without a gap T30 fires up to 1 s (the second's fraction at formed) before the
// virtual deadline. Within this slack (that second plus 100 ms for clock-read skew) the timeout is the shadow's expiry; only
// an earlier one - the wall clock ran ahead of the manager clock, a world gap (AdvanceClock loses > 1 interval) - is a T30
// cut. Kept narrow so a single clamped gap still shows as t30_cut.
constexpr std::uint64_t kT30SecondSlackMs = 1100;

// The verdict of a meeting T30 ended (in the pass, or ended and swept in one pass): its rendezvous timeout is an expiry or a
// T30 cut; any other end closes the meeting without a verdict.
inline ShadowEnd T30EndVerdict(EndReason why, bool resumed, std::uint64_t mgrMs, std::uint64_t deadlineMs)
{
    if (why != EndReason::RendezvousTimeout)
        return ShadowEnd::Closed;
    GatherPoint leg;
    leg.deadlineMs = deadlineMs;
    return !resumed && LegExpired(mgrMs + kT30SecondSlackMs, leg) ? ShadowEnd::Expired : ShadowEnd::T30Cut;
}

// "[GroupMgr] event=shadow_end ...": one line per measured meeting when it closes, with the group's arm, so the A/B splits
// the shadow measures per arm (8.3); ms = formed -> close on the manager clock; arrived = members counted arrived in the
// meeting / members judged in its last verdict.
inline int FormatShadowEnd(char* buf, std::size_t size, std::uint32_t serial, bool arm, ShadowEnd r, std::uint64_t ms,
    std::uint32_t arrived, std::uint32_t judged)
{
    return std::snprintf(buf, size, "[GroupMgr] event=shadow_end group=%u gather=%u result=%s ms=%llu arrived=%u/%u", serial,
        arm ? 1u : 0u, ShadowEndName(r), (unsigned long long)ms, arrived, judged);
}

struct ShadowInput
{
    std::uint32_t nowMs = 0;              // the world ms clock of this pass (snapshot ages)
    std::uint64_t mgrMs = 0;              // the manager clock (AdvanceClock) of this pass
    bool resumed = false;                 // the inbox pass resumed after an off gap (tables cleared, T30 ran no end rule)
    std::uint32_t gather = 0;             // the effective switch (arm label)
    std::uint32_t rendezvousMinutes = 0;  // QuestGroups.RendezvousMinutes (the virtual deadline)
    GatherRules rules;
    StatusTable const* status = nullptr;
    PlaceTable const* places = nullptr;
    std::vector<SweptGroup> const* swept = nullptr;   // the groups the directive pass ended and swept (Engine::Swept)
};

// One shadow meeting per manager QUEST group the shadow saw forming.
struct ShadowMeeting
{
    bool tracked = false;          // first seen forming: measured from formed (a group first seen later is never measured)
    bool formed = false;
    bool open = false;             // the T33 rule has neither gathered it nor seen its deadline pass
    bool t33First = false;         // counted in shadow_t33_first
    bool t30First = false;         // counted in shadow_t30_first
    std::uint32_t leaderLow = 0;   // the leader of the last pass (the arm of a meeting whose group is gone)
    std::uint32_t judged = 0;      // members judged in the last verdict
    std::uint64_t formedMs = 0;    // the manager clock at formed (F16 (a)); the event=phase ms count from here
    std::uint64_t deadlineMs = 0;  // formed + RendezvousMinutes; a resume restarts it
    quest_groups::Phase phase = quest_groups::Phase::Rendezvous;   // T30's phase after the last pass
    std::vector<std::uint32_t> arrived;   // members already counted arrived in this meeting
    std::uint64_t seenPass = 0;
};

class GatherShadow
{
public:
    // From the inbox drain: the latest GatherPlace of a bot. A far jump from its previous place is counted, except while the
    // bot is busy (taxi, teleport, band trip) at either place: a taxi covers > 200 yd per status and is not a 2.8 source. The
    // drain stores the bot's Status before its GatherPlace (posted in that order), so status holds the current flags. A
    // previous place older than maxAgeMs (PlaceMaxAgeMs: the bot stopped posting, e.g. between two groups) is no base for a
    // jump: walking between two group memberships is not a 2.8 source.
    void StorePlace(PlaceTable& places, StatusTable const& status, manager_inbox::Event const& e, std::uint32_t nowMs,
        std::uint32_t maxAgeMs)
    {
        PlaceSnapshot s = PlaceSnapshotOf(e, nowMs);
        auto const st = status.find(e.botLow);
        s.busy = st != status.end() && manager_inbox::UnpackFlags(st->second.flags).busy;
        auto const it = places.find(e.botLow);
        if (it == places.end())
        {
            places.emplace(e.botLow, s);
            return;
        }
        bool const base = !it->second.busy && !s.busy && manager_inbox::Fresh(it->second.receivedMs, nowMs, maxAgeMs);
        PlaceJump const jump = base ? JumpOf(it->second.place, s.place, kPlaceJumpYards) : PlaceJump::None;
        counts_.jumpSame += jump == PlaceJump::SameMap ? 1 : 0;
        counts_.jumpOther += jump == PlaceJump::OtherMap ? 1 : 0;
        it->second = s;
    }

    // After the directive pass (world thread): every QUEST group of the registry, read only. Order per group: formed ->
    // the verdicts of this pass -> the phase line of the phase T30 left it in.
    template <typename Sink>
    void Pass(std::unordered_map<std::uint32_t, ManagedGroup> const& groups, ShadowInput const& in, Sink const& line)
    {
        on_ = true;
        ++passNo_;
        std::uint64_t const legMs = std::uint64_t(in.rendezvousMinutes) * 60000u;
        if (in.resumed)
        {
            // 2.5: after an off gap the open meetings get a fresh deadline; none ends in this pass.
            ++counts_.resume;
            for (auto& entry : meetings_)
                if (entry.second.open)
                    entry.second.deadlineMs = in.mgrMs + legMs;
        }
        for (auto const& entry : groups)
        {
            ManagedGroup const& g = entry.second;
            if (g.type != GroupType::Quest)
                continue;
            auto const ins = meetings_.emplace(g.serial, ShadowMeeting());
            ShadowMeeting& s = ins.first->second;
            if (ins.second)
                s.tracked = g.state == GroupState::Forming;
            s.seenPass = passNo_;
            if (!s.tracked)
                continue;
            s.leaderLow = g.leaderLow;
            if (!s.formed)
            {
                if (!g.formedSec)
                    continue;   // still forming, or ended before it formed
                s.formed = true;
                s.open = true;
                s.formedMs = in.mgrMs;
                s.deadlineMs = in.mgrMs + legMs;
                s.phase = quest_groups::Phase::Rendezvous;
                PhaseLine(line, g, quest_groups::Phase::Rendezvous, s, in);
            }
            Judge(g, s, in, line);
            if (g.phase != s.phase)
            {
                s.phase = g.phase;
                PhaseLine(line, g, g.phase, s, in);
            }
        }
        // A group the registry no longer holds: its meeting is over. Still open: if the directive pass ended and swept it in
        // this pass, the verdict of that end (T30EndVerdict), else closed without a verdict.
        for (auto it = meetings_.begin(); it != meetings_.end();)
        {
            if (it->second.seenPass == passNo_)
            {
                it = std::next(it);
                continue;
            }
            if (it->second.open)
            {
                ShadowEnd r = ShadowEnd::Closed;
                if (in.swept)
                    for (SweptGroup const& sg : *in.swept)
                        if (sg.serial == it->first)
                            r = T30EndVerdict(sg.why, in.resumed, in.mgrMs, it->second.deadlineMs);
                CountEnd(r);
                EndLine(line, it->first, it->second, r, in);
            }
            it = meetings_.erase(it);
        }
        MinuteLine(in, line);
    }

    // Gather 0: nothing is kept, so a later switch-on starts with fresh meetings, counts and minute window.
    void Off()
    {
        if (!on_)
            return;
        on_ = false;
        meetings_.clear();
        counts_ = GatherCounts();
        seen_ = GatherCounts();
        minuteStarted_ = false;
        linesThisMinute_ = 0;
    }

    // Tests.
    GatherCounts const& Totals() const { return counts_; }
    std::unordered_map<std::uint32_t, ShadowMeeting> const& Meetings() const { return meetings_; }

private:
    // The T33 verdict of this pass and, where T30 judged the rendezvous in the same pass, the comparison with it.
    template <typename Sink>
    void Judge(ManagedGroup const& g, ShadowMeeting& s, ShadowInput const& in, Sink const& line)
    {
        bool const ending = g.state == GroupState::Ending;
        // T30 judged the rendezvous in this pass: in it before the pass, active after it, its end rules ran (no resume), and
        // not straight to TurnIn (all complete is decided before the gathered rule).
        bool const compare = s.phase == quest_groups::Phase::Rendezvous && !ending && !in.resumed &&
            g.phase != quest_groups::Phase::TurnIn;
        if (!s.open && !compare)
            return;
        GatherPoint leg;
        leg.deadlineMs = s.deadlineMs;
        if (s.open && ending)
        {
            // T30 ended the group. Its rendezvous timeout runs on wall-clock seconds without the gap rule, so it is the
            // shadow's expiry only once the virtual deadline on the manager clock has passed too (within T30's second,
            // kT30SecondSlackMs); before that it is a T30 cut. Any other end closes the meeting without a verdict.
            s.open = false;
            ShadowEnd const r = T30EndVerdict(g.endReason, in.resumed, in.mgrMs, s.deadlineMs);
            CountEnd(r);
            EndLine(line, g.serial, s, r, in);
            return;
        }
        verdicts_.clear();
        for (Member const& m : g.m)
            if (m.state == MemberState::Acked)
            {
                MemberVerdict v;
                v.low = m.low;
                verdicts_.push_back(v);
            }
        GroupVerdict const v = ArrivedForGroup(g.leaderLow, verdicts_, *in.status, *in.places, in.nowMs, in.rules);
        if (compare)
        {
            bool const t30Gathered = g.phase == quest_groups::Phase::Goal;
            ++counts_.compared;
            if (v.gathered != t30Gathered)
            {
                ++counts_.disagree;
                bool& first = v.gathered ? s.t33First : s.t30First;
                if (!first)
                {
                    first = true;
                    ++(v.gathered ? counts_.t33First : counts_.t30First);
                }
            }
        }
        if (!s.open)
            return;
        s.judged = v.judged;
        for (MemberVerdict const& m : verdicts_)
        {
            if (!m.arrived || std::find(s.arrived.begin(), s.arrived.end(), m.low) != s.arrived.end())
                continue;
            s.arrived.push_back(m.low);
            ++counts_.arrived;
            counts_.deadAtPoint += m.dead ? 1 : 0;
            counts_.zOut += m.zOut ? 1 : 0;
            counts_.regionMismatch += m.regionMismatch ? 1 : 0;
        }
        if (v.gathered)
        {
            s.open = false;
            ++counts_.gathered;
            EndLine(line, g.serial, s, ShadowEnd::Gathered, in);
            return;
        }
        if (!in.resumed && LegExpired(in.mgrMs, leg))
        {
            s.open = false;
            ++counts_.legExpired;
            EndLine(line, g.serial, s, ShadowEnd::Expired, in);
        }
    }

    void CountEnd(ShadowEnd r)
    {
        counts_.legExpired += r == ShadowEnd::Expired ? 1 : 0;
        counts_.t30Cut += r == ShadowEnd::T30Cut ? 1 : 0;
    }

    // The shadow's event line budget (kLinesPerMinute per minute, like the engine's, kept apart from it because the shadow
    // never writes the engine; the rest is counted as shadow_lines_dropped).
    template <typename Sink>
    void EventLine(Sink const& line, char const* text)
    {
        if (linesThisMinute_ >= kLinesPerMinute)
        {
            ++counts_.linesDropped;
            return;
        }
        ++linesThisMinute_;
        line(text);
    }

    template <typename Sink>
    void PhaseLine(Sink const& line, ManagedGroup const& g, quest_groups::Phase phase, ShadowMeeting const& s, ShadowInput const& in)
    {
        char buf[160];
        FormatPhaseLine(buf, sizeof(buf), g.serial, phase, in.mgrMs - s.formedMs, GatherArm(in.gather, g.leaderLow));
        EventLine(line, buf);
    }

    template <typename Sink>
    void EndLine(Sink const& line, std::uint32_t serial, ShadowMeeting const& s, ShadowEnd r, ShadowInput const& in)
    {
        char buf[160];
        FormatShadowEnd(buf, sizeof(buf), serial, GatherArm(in.gather, s.leaderLow), r, in.mgrMs - s.formedMs,
            std::uint32_t(s.arrived.size()), s.judged);
        EventLine(line, buf);
    }

    // Once a minute (the window opens on the first pass, no line for a partial minute).
    template <typename Sink>
    void MinuteLine(ShadowInput const& in, Sink const& line)
    {
        if (!minuteStarted_)
        {
            minuteStarted_ = true;
            minuteStartMs_ = in.nowMs;
            seen_ = counts_;
            return;
        }
        if (std::uint32_t(in.nowMs - minuteStartMs_) < 60000)
            return;
        minuteStartMs_ = in.nowMs;
        linesThisMinute_ = 0;
        std::uint64_t open = 0;
        for (auto const& entry : meetings_)
            open += entry.second.open ? 1 : 0;
        // Only fresh places: a bot that left its group stops posting but keeps its entry until it leaves the bot map.
        std::uint64_t places = 0;
        if (in.places)
            for (auto const& entry : *in.places)
                places += manager_inbox::Fresh(entry.second.receivedMs, in.nowMs, in.rules.maxAgeMs) ? 1 : 0;
        char buf[768];
        FormatGatherMinute(buf, sizeof(buf), in.gather, GatherDelta(counts_, seen_), places, open);
        seen_ = counts_;
        line(buf);
    }

    std::unordered_map<std::uint32_t, ShadowMeeting> meetings_;
    std::vector<MemberVerdict> verdicts_;   // reused per group
    GatherCounts counts_;
    GatherCounts seen_;                     // counts_ at the last minute line
    bool on_ = false;
    std::uint64_t passNo_ = 0;
    bool minuteStarted_ = false;
    std::uint32_t minuteStartMs_ = 0;
    std::uint32_t linesThisMinute_ = 0;
};
}
