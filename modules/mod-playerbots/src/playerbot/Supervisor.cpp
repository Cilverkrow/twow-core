#include "playerbot/playerbot.h"
#include "playerbot/Supervisor.h"
#include "playerbot/SupervisorMailPolicy.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/CohortPolicy.h"

namespace ai::supervisor
{
namespace
{
// A3 step 3 T02-a: the null target, the same rule as the shadow's (SupervisorShadow.cpp IsNullDest).
bool IsNullDest(TravelDestination const* dest)
{
    return !dest || dynamic_cast<NullTravelDestination const*>(dest) != nullptr;
}
}

Supervisor::Supervisor(PlayerbotAI* ai) : ai_(ai)
{
}

// Without Release (the bot's AI is torn down): the shadow state is dropped without a line and without touching the bot. A3 step 3
// T02-GROUP: the mailbox registration of this instance goes too (the mailbox lock only; the logout thread may run this, so a held
// group is never left here - the manager ends it without a supervisor).
Supervisor::~Supervisor()
{
    ShadowDrop(shadow_);
    UnregisterMail(mailGuid_, mailToken_);
}

// Behaviour pair 1 (AiPlayerbot.Supervisor.Arbiter): the arbiter shadow closes the bundle of the engine pass before
// (SupervisorShadow.cpp); it only watches. arbiter comes from the gate in PlayerbotAI::UpdateAI (ArbiterValue). A3 step 3 T02-a
// (AiPlayerbot.Supervisor.Arbiter = 2, default 0): below 2 and without an edge the rest is one compare and, at 1, the probe's
// early returns (no write, no line). A3 step 3 T02-GROUP (BotGroups.Manager.Types > 0, default 0): GroupTick runs after the
// shadow hand-over (its writes fall into this pass's bundle) and before the enforcement edges (a switch away from 2 leaves a
// held group before the disarming edge); without a group it is the registration once, then a few compares.
void Supervisor::Tick(std::uint32_t nowMs, std::uint32_t arbiter)
{
    ++ticks_;
    TakeMail();
    ShadowTick(ai_, shadow_, arbiter >= 1);
    GroupTick(nowMs, arbiter);
    bool const enforce = arbiter == 2;
    if (enforce != enforce_)
    {
        enforce_ = enforce;
        if (enforce)
            ArmEnforce();
        else
            DisarmEnforce(arbiter);
    }
    if (arbiter >= 1)
        ProbeTick(nowMs, arbiter);
}

// Mailbox pair (SupervisorMail.cpp): no mail anywhere = one atomic load. The drained events are counted by the mailbox. A3 step 3
// T02-GROUP: HandleMail (SupervisorGroup.cpp) keeps the newest group directive and writes nothing; GroupTick answers it in the
// same tick (the one ack through the manager inbox).
void Supervisor::TakeMail()
{
    if (!MailWaiting())
        return;
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    Event mail[kMailSlots];
    std::size_t const n = Drain(bot->GetGUIDLow(), mail, kMailSlots);
    HandleMail(mail, n);
}

// The supervisor owns nothing of the bot, so there is nothing to hand back; the arbiter shadow closes its open bundle. A3 step 3
// T02-a: the lane owner and the enforcement state go with it; the mode stays (the instance is dropped right after). T02-GROUP:
// a held group is left first (GroupRelease; nothing without one, and no left report: the manager ends it without a supervisor).
void Supervisor::Release()
{
    GroupRelease();
    ShadowRelease(ai_, shadow_);
    owner_ = Lane::None;
    enforce_ = false;
}

// ---- A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0) --------------------------------------------------------
// Ownership holds only on the same write generation, an open target and (group owner) in group mode or during the probe; an owner
// that no longer holds is cleared here (the lazy end: every write raises the generation).
Lane Supervisor::HeldOwner(ai::TravelTarget* target)
{
    if (owner_ == Lane::None)
        return Lane::None;
    if (target && OwnerHolds(owner_, ownerGen_, target->WriteGen(), std::uint8_t(target->GetStatus()),
            IsNullDest(target->GetDestination()), mode_ == Mode::Group || inProbe_))
        return owner_;
    owner_ = Lane::None;
    return Lane::None;
}

void Supervisor::SetOwner(Lane lane, std::uint32_t gen)
{
    owner_ = lane;
    ownerGen_ = gen;
}

// Arming edge: a bot in the quest book scope (roster member, no real player master), not in a battleground and not parked
// (the Passive-by-design cases of SupervisorPolicy.h Mode) leaves Passive for Quest. Every other bot stays Passive.
void Supervisor::ArmEnforce()
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot || mode_ != Mode::Passive)
        return;
    if (!ai_->InQuestBookScope() || bot->InBattleGround() || ai_->IsParked())
        return;
    TransitionResult const result = Decide(Mode::Passive, Mode::Quest, Requester::External, phase_, 0, Cadence{});
    if (result.allowed)
        mode_ = result.to;
    WriteModeLine(Mode::Passive, Mode::Quest, result, "arbiter_enforce", 2);
}

// Disarming edge: no owner any more; a bot that left Passive goes back to it.
void Supervisor::DisarmEnforce(std::uint32_t arbiter)
{
    owner_ = Lane::None;
    if (mode_ == Mode::Passive)
        return;
    Mode const from = mode_;
    TransitionResult const result = Decide(from, Mode::Passive, Requester::External, phase_, 0, Cadence{});
    if (result.allowed)
        mode_ = result.to;
    WriteModeLine(from, Mode::Passive, result, "arbiter_disarm", arbiter, true);
}

// One ev=mode line (FormatModeLine) from the shadow's shared line budget. R9b V12: the disarming edge's line (edgeLine) goes
// outside the budget (EmitEdgeLine), one per bot and edge, so the disarm check can see every bot.
void Supervisor::WriteModeLine(Mode from, Mode to, TransitionResult const& result, char const* cause, std::uint32_t arbiter,
    bool edgeLine)
{
    Player* const bot = ai_ ? ai_->GetBot() : nullptr;
    if (!bot)
        return;
    ModeLine line;
    line.guid = bot->GetGUIDLow();
    line.name = bot->GetName();
    line.cohort = cohort::Letter(line.guid);
    line.arbiter = arbiter;
    line.level = bot->GetLevel();
    line.map = bot->GetMapId();
    line.zone = bot->GetZoneId();
    line.cause = cause;
    line.from = from;
    line.to = to;
    line.phase = phase_;
    line.req = Requester::External;
    line.deny = result.allowed ? Deny::None : result.reason;
    char buf[512];
    FormatModeLine(buf, sizeof(buf), line);
    if (edgeLine)
        EmitEdgeLine(line.guid, buf);
    else
        EmitEnforceLine(line.guid, buf);
}
}
