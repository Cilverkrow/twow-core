#pragma once

#include "playerbot/PlannerShadow.h"

#include <atomic>
#include <cstdint>
#include <tuple>

// A3 step 3 planner P-3a (P3-ENTWURF-v0.2, frozen d45c4961, Nachtraege 1-3; AiPlayerbot.Planner = 2 with
// AiPlayerbot.Planner.WriteCohort, default 0): the planner's write path. PlannerWrite.cpp is the planner's one door for writes
// (design section 7.1): the expire of the live target under WriterId::Planner, the QUEST <-> GRIND switch
// (Supervisor::PlannerMode) and the bookkeeping of the F1 feed. The feed's own SetBestTarget call stays in
// ChooseTravelTargetAction::Execute (the choice writes its local target as before); it asks FeedBegin / FeedTakes / FeedEnd here
// and the ApplyWatch of setNewTarget reports the outcome. R9 (OnReplanVerdict, EvReplan) is P-3c and not here.
class PlayerbotAI;

namespace ai
{
class TravelTarget;
class TravelDestination;
class WorldPosition;
}

namespace ai::supervisor
{
class Supervisor;
}

namespace ai::planner_write
{
// Condition 1 (section 2.3): the bot writes - Planner = 2, its write cohort, arbiter 2 and EnforceOn. Every other bot plans as at
// Planner = 1 (fb_cohort / fb_arbiter).
bool InWriteCohort(std::uint32_t guidLow);
bool Writes(PlayerbotAI* ai, ai::supervisor::Supervisor const& sup, std::uint32_t arbiter);

// Conditions 2-7 for the pick and, when they hold, the expire of the live target (WriterId::Planner). gate gets ExpireCheck's
// answer (WorkOverride: expire_work_override), sawTravel whether the live target was open but not in WORK (expire_after_work).
ai::planner::ExpireStep Expire(PlayerbotAI* ai, ai::planner::Pick const& pick, ai::planner::PlanState const& st, std::uint32_t nowMs,
    std::uint32_t minBindMs, ai::planner::ExpireGate& gate, bool& sawTravel);

// The F1 feed of one choice (section 2.4, Nachtrag 3). Lives on Execute's stack; ChooseTravelTargetAction holds a pointer while
// the choice runs.
struct Feed
{
    bool tried = false;           // the plan attempt ran in this choice (the log caps are shared with the full search)
    bool attempt = false;         // inside the plan attempt's SetBestTarget (class P is buffered, checked is buffered)
    bool grind = false;           // a grind pick (the list is the pick zone's grind points)
    bool fed = false;             // the plan attempt chose a target; the full search does not run
    std::uint32_t questId = 0;    // a quest pick: DestKey AND quest (OB-60 H1)
    std::uint32_t destKey = 0;
    std::uint32_t zone = 0;       // a grind pick: the zone
    std::uint32_t key = 0;        // the pick key (feedBudgetKey)
    std::uint32_t sinceMs = 0;    // the pick's sinceMs (twow-repo#726 F2 / Q2 (c): key and sinceMs name one pick)
    std::uint32_t takesUs = 0;    // twow-repo#726 F1: microseconds of the FeedTakes filter loop (feed_us / feed_us_max)
    bool turnIn = false;
    std::uint32_t guardLogged = 0;   // [FarMoveGuard] lines of this choice, shared by both calls (LogsPerChoice per choice)
    std::uint32_t checked = 0;       // travel_choose checked of the plan attempt, added only when it chose
};

// Whether this choice tries the plan pick first; counts fallback_purpose / _none / _stale / _budget otherwise (and sets the
// fallback time of rule 5). forcedTarget: pvp, guild meeting / order, petition (a forced choice keeps its own list).
bool FeedBegin(PlayerbotAI* ai, char const* purpose, bool forcedTarget, bool resumePending, Feed& feed);

// The destination belongs to the plan list: a quest pick takes its DestKey AND quest, a grind pick the points in its zone.
bool FeedTakes(Feed const& feed, ai::TravelDestination const* dest, ai::WorldPosition const* point);

// The plan attempt's two counter classes (Nachtrag 3; header-only, t/planner_shadow_policy_tests.cpp tests them):
// class B - this bot's / this action's state - is saved before the attempt and restored when it does not choose;
template <class... T>
class AttemptRestore
{
public:
    explicit AttemptRestore(T&... state) : state_(state...), saved_(state...) {}
    void Restore() { state_ = saved_; }

private:
    std::tuple<T&...> state_;
    std::tuple<T...> saved_;
};

// class P - process-wide atomics - is counted into the attempt's own buffer and added to the shared counter only when the attempt
// chose; a failed attempt drops the buffer. Never a restore and never a fetch_sub on a shared counter (another thread may count).
template <class N>
inline void CommitBuffered(std::atomic<N>& shared, std::atomic<N> const& buffer)
{
    N const n = buffer.load(std::memory_order_relaxed);
    if (n)
        shared.fetch_add(n, std::memory_order_relaxed);
}

// class P outside this action: the quest feasibility counters of PlayerbotAI.cpp (SkipInfeasibleQuest, reached through the
// destinations' IsActive). SetFeasibilitySink points the calling thread's counting at a buffer (nullptr = the shared counters);
// CommitFeasibility adds a buffer to the shared counters. Both are defined in PlayerbotAI.cpp, where the counters live.
struct FeasibilityCounts
{
    std::atomic<std::uint32_t> skips{0};
    std::atomic<std::uint32_t> giverInactive{0};
};
void SetFeasibilitySink(FeasibilityCounts* sink);
void CommitFeasibility(FeasibilityCounts const& buffer);

enum class FeedEnd : std::uint8_t
{
    Fed = 0,         // the plan attempt chose: fed
    Reject = 1,      // SetBestTarget said no: fallback_reject
    ZoneEmpty = 2,   // a grind pick without a point in its zone: fallback_zone_empty once per pick, then zone_empty_held
    Budget = 3,      // #416 in the plan attempt: fallback_budget, no second attempt for this pick (until a pick change / MinBind)
    RejectEmpty = 4, // twow-repo#726 F3: a quest pick's target not in this choice's list: fallback_reject_empty, no lock
};
void FeedResult(PlayerbotAI* ai, Feed const& feed, FeedEnd end);

// setNewTarget of a fed choice: armed only for the plan attempt's own target (feed->fed and the same local target). At the end
// of setNewTarget a new write generation is applied_quest / applied_grind; none is fallback_parked (the #485 refusal expired the
// live target) or fallback_denied (the arbiter refused the copy). Early returns before setNewTarget count none of them.
class ApplyWatch
{
public:
    ApplyWatch(PlayerbotAI* ai, Feed const* feed, ai::TravelTarget* live, bool armed);
    ~ApplyWatch();
    ApplyWatch(ApplyWatch const&) = delete;
    ApplyWatch& operator=(ApplyWatch const&) = delete;

private:
    PlayerbotAI* ai_;
    Feed const* feed_;
    ai::TravelTarget* live_;
    std::uint32_t gen_;
};
}
