#include "playerbot/playerbot.h"
#include "playerbot/PlannerWrite.h"
#include "playerbot/Supervisor.h"
#include "playerbot/SupervisorEnforce.h"
#include "playerbot/SupervisorShadow.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/strategy/AiObjectContext.h"

#include <cstdint>
#include <string>

// A3 step 3 planner P-3a (P3-ENTWURF-v0.2, frozen d45c4961, Nachtraege 1-3; AiPlayerbot.Planner = 2 with
// AiPlayerbot.Planner.WriteCohort, default 0): the planner's one door for writes (design section 7.1; the contract
// a3s3_planner_write pins it). One status write - the expire of the live target, the pattern of TravelValues.cpp (profession
// trip close): ExpireCheck, then the bot's own map thread, the lane check (Questbook, site "planner") and the group copy, then
// WriterScope(WriterId::Planner), SetStatus EXPIRED and ClearValues. Never a new target here: the choice
// (ChooseTravelTargetAction::Execute) writes it, fed with the plan pick first (section 2.4); never a packet.
//
// The group copy (condition 7, P3-2): a member's copy of a group target is written by ChooseGroupTravelTargetAction::Execute
// (ChooseTravelTargetAction.cpp:2424 @9x f4d0ef98, SetGroupCopy) under WriterScope(WriterId::GroupFollow) - questbook lane, so
// the Questbook lane check does not see it; the expire asks target->IsGroupCopy() itself and never closes a group copy.
namespace
{
using namespace ai::planner;

ai::TravelTarget* LiveTarget(PlayerbotAI* ai)
{
    AiObjectContext* const context = ai ? ai->GetAiObjectContext() : nullptr;
    return context ? context->GetValue<ai::TravelTarget*>("travel target")->Get() : nullptr;
}

// READY is open (chosen, not yet travelling): the expire treats it as TRAVEL (section 2.3, condition 2).
TravelPhase OpenPhaseOf(ai::TravelStatus status)
{
    switch (status)
    {
        case ai::TravelStatus::TRAVEL_STATUS_READY:
        case ai::TravelStatus::TRAVEL_STATUS_TRAVEL: return TravelPhase::Travel;
        case ai::TravelStatus::TRAVEL_STATUS_WORK: return TravelPhase::Work;
        case ai::TravelStatus::TRAVEL_STATUS_COOLDOWN: return TravelPhase::Cooldown;
        case ai::TravelStatus::TRAVEL_STATUS_EXPIRED: return TravelPhase::Expired;
        default: return TravelPhase::None;
    }
}

std::uint32_t NowMs()
{
    std::uint32_t const now = WorldTimer::getMSTime();
    return now ? now : 1u;
}

void Count(PlayerbotAI* ai, PlanMinCounts const& c)
{
    Player* const bot = ai ? ai->GetBot() : nullptr;
    if (bot)
        ai::planner_shadow::Count(bot->GetGUIDLow(), c);
}
}

namespace ai::planner_write
{
bool InWriteCohort(std::uint32_t guidLow)
{
    return WritesFor(sPlayerbotAIConfig.planner, sPlayerbotAIConfig.plannerWriteCohort, guidLow);
}

bool Writes(PlayerbotAI* ai, ai::supervisor::Supervisor const& sup, std::uint32_t arbiter)
{
    Player* const bot = ai ? ai->GetBot() : nullptr;
    return bot && arbiter == 2 && sup.EnforceOn() && InWriteCohort(bot->GetGUIDLow());
}

ExpireStep Expire(PlayerbotAI* ai, Pick const& pick, PlanState const& st, std::uint32_t nowMs, std::uint32_t minBindMs,
    ExpireGate& gate, bool& sawTravel)
{
    gate = ExpireGate::Allowed;
    sawTravel = false;
    ai::TravelTarget* const target = LiveTarget(ai);
    ExpireFacts f;
    if (target)
    {
        ai::TravelDestination const* const dest = target->GetDestination();
        f.liveDestKey = ai::planner_shadow::DestKey(dest);
        f.liveNull = f.liveDestKey == 0;
        f.liveQuest = ai::planner_shadow::QuestOfDest(dest);
        f.phase = OpenPhaseOf(target->GetStatus());
        if (pick.kind == Kind::Grind)
            f.liveZone = ai::planner_shadow::ZoneOf(target->GetPosition());
    }
    sawTravel = f.phase == TravelPhase::Travel;
    f.pickGrind = pick.kind == Kind::Grind;
    f.pickDestKey = pick.destKey;
    f.pickQuest = pick.questId;
    f.pickZone = pick.zone;
    f.repeat = st.expiredKey == pick.key && st.expiredSinceMs == pick.sinceMs;
    f.fallbackBlock = FallbackBlocks(st.fallbackAtMs, nowMs, minBindMs);
    gate = ExpireCheck(f.phase, pick.forced);
    ExpireStep const step = ExpireDecide(f, gate);
    if (step != ExpireStep::Go)
        return step;
    if (!ai::supervisor::OnOwnThread(ai))
        return ExpireStep::DeniedThread;
    ExpireStep const admit = ExpireAdmit(ai::supervisor::LaneFree(ai, ai::supervisor::Lane::Questbook, "planner"),
        target->IsGroupCopy());
    if (admit != ExpireStep::Go)
        return admit;
    {
        ai::supervisor::WriterScope const scope(ai::supervisor::WriterId::Planner);
        target->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
        ai->GetAiObjectContext()->ClearValues("no active travel destinations");
    }
    return ExpireStep::Expired;
}

bool FeedBegin(PlayerbotAI* ai, char const* purpose, bool forcedTarget, bool resumePending, Feed& feed)
{
    feed = Feed();
    if (sPlayerbotAIConfig.planner != 2 || !ai)
        return false;
    ai::supervisor::Supervisor* const sup = ai->GetSupervisor();
    if (!sup || !Writes(ai, *sup, ai::supervisor::ArbiterValue(sPlayerbotAIConfig.supervisorArbiter)))
        return false;
    std::string const p = purpose ? purpose : "";
    bool const quest = p == "quest";
    bool const grind = p == std::to_string(std::uint32_t(ai::TravelDestinationPurpose::Grind));
    PlanState& st = sup->Plan();
    Pick const& pick = st.pick;
    std::uint32_t const nowMs = NowMs();
    bool const pickGrind = pick.kind == Kind::Grind;
    FeedSkip const skip = FeedCheck(quest, grind, forcedTarget, pick.has, pickGrind, pick.atMs, nowMs,
        sPlayerbotAIConfig.plannerMinBindSec * 1000u, resumePending, st.feedBudgetKey && st.feedBudgetKey == pick.key);
    if (skip != FeedSkip::Feed)
    {
        PlanMinCounts c;
        FeedFallback fb = FeedFallback::None;
        switch (skip)
        {
            case FeedSkip::Purpose: ++c.fallbackPurpose; fb = FeedFallback::Purpose; break;
            case FeedSkip::None: ++c.fallbackNone; fb = FeedFallback::None; break;
            case FeedSkip::Stale: ++c.fallbackStale; fb = FeedFallback::Stale; break;
            case FeedSkip::Budget: ++c.fallbackBudget; fb = FeedFallback::Budget; break;
            case FeedSkip::Feed: break;
        }
        NoteFallback(st.fallbackAtMs, fb, nowMs);   // rule 5 (OB-10 V-3): only budget locks here; none, stale, purpose never
        Count(ai, c);
        return false;
    }
    feed.tried = true;
    feed.grind = pickGrind;
    feed.questId = pickGrind ? 0 : pick.questId;
    feed.destKey = pickGrind ? 0 : pick.destKey;
    feed.zone = pickGrind ? pick.zone : 0;
    feed.key = pick.key;
    feed.turnIn = pick.kind == Kind::QuestTurnIn;
    return true;
}

bool FeedTakes(Feed const& feed, ai::TravelDestination const* dest, ai::WorldPosition const* point)
{
    if (!dest)
        return false;
    if (feed.grind)
        return feed.zone && point && ai::planner_shadow::ZoneOf(point) == feed.zone;
    std::uint32_t const key = ai::planner_shadow::DestKey(dest);
    return key && key == feed.destKey && ai::planner_shadow::QuestOfDest(dest) == feed.questId;
}

void FeedResult(PlayerbotAI* ai, Feed const& feed, FeedEnd end)
{
    ai::supervisor::Supervisor* const sup = ai ? ai->GetSupervisor() : nullptr;
    if (!sup)
        return;
    PlanState& st = sup->Plan();
    std::uint32_t const nowMs = NowMs();
    PlanMinCounts c;
    switch (end)
    {
        case FeedEnd::Fed:
            ++c.fed;
            break;
        case FeedEnd::Reject:
            ++c.fallbackReject;
            NoteFallback(st.fallbackAtMs, FeedFallback::Reject, nowMs);
            break;
        case FeedEnd::ZoneEmpty:
            ++c.fallbackZoneEmpty;
            NoteFallback(st.fallbackAtMs, FeedFallback::ZoneEmpty, nowMs);
            break;
        case FeedEnd::Budget:
            ++c.fallbackBudget;
            NoteFallback(st.fallbackAtMs, FeedFallback::Budget, nowMs);
            st.feedBudgetKey = feed.key;   // #416: no second attempt for this pick (the full list resumes)
            break;
    }
    Count(ai, c);
}

ApplyWatch::ApplyWatch(PlayerbotAI* ai, Feed const* feed, ai::TravelTarget* live, bool armed)
    : ai_(ai), feed_(armed && feed && feed->fed ? feed : nullptr), live_(live), gen_(live ? live->WriteGen() : 0)
{
}

ApplyWatch::~ApplyWatch()
{
    if (!feed_ || !live_ || !ai_)
        return;
    ai::supervisor::Supervisor* const sup = ai_->GetSupervisor();
    if (!sup)
        return;
    PlanState& st = sup->Plan();
    std::uint32_t const nowMs = NowMs();
    std::uint32_t const gen = live_->WriteGen();
    PlanMinCounts c;
    ApplyOutcome const outcome = ApplyOutcomeOf(gen_, gen, live_->GetStatus() == TravelStatus::TRAVEL_STATUS_EXPIRED);
    if (outcome == ApplyOutcome::Applied)
    {
        ++(feed_->grind ? c.appliedGrind : c.appliedQuest);
        st.appliedGen = gen;
        st.appliedAtMs = nowMs;
        st.appliedGrindZone = feed_->grind;
        st.appliedDest = feed_->grind ? feed_->zone : ai::planner_shadow::DestKey(live_->GetDestination());
        if (feed_->turnIn)
        {
            st.turninQuest = feed_->questId;
            st.turninAtMs = nowMs;
        }
    }
    else
    {
        // No new write generation: the #485 refusal expired the live target (parked), else the arbiter denied the copy.
        bool const parked = outcome == ApplyOutcome::Parked;
        ++(parked ? c.fallbackParked : c.fallbackDenied);
        NoteFallback(st.fallbackAtMs, parked ? FeedFallback::Parked : FeedFallback::Denied, nowMs);
    }
    Count(ai_, c);
}
}

namespace ai::supervisor
{
// Section 2.6: the first caller with Requester::Planner. QUEST -> GRIND waits Planner.MinDwellSec (the dwell since the last mode
// change the planner saw), GRIND -> QUEST does not (SupervisorPolicy.h Decide).
TransitionResult Supervisor::PlannerMode(Mode to, std::uint32_t dwellMs, std::uint32_t arbiter)
{
    Mode const from = mode_;
    Cadence cadence{};
    cadence.minDwellMs = sPlayerbotAIConfig.plannerMinDwellSec * 1000u;
    TransitionResult const result = Decide(from, to, Requester::Planner, phase_, dwellMs, cadence);
    if (result.allowed)
        mode_ = result.to;
    WriteModeLine(from, to, result, "planner", arbiter);
    return result;
}
}
