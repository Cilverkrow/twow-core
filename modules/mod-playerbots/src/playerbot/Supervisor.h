#pragma once

#include "playerbot/SupervisorPolicy.h"
#include "playerbot/SupervisorShadow.h"
#include "playerbot/ArbiterLanePolicy.h"

#include <cstddef>
#include <cstdint>

class PlayerbotAI;

namespace ai
{
class TravelTarget;
}

// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor, default 0): one supervisor per bot, on
// the bot's map thread (PlayerbotAI::UpdateAI, in the engine pass). Design: ws-00/projekt-durchleuchtung/A3-S3-ENTWURF.md
// (c98e6ded). The instance exists only while the switch applies to the bot; Tick counts the engine passes and, with
// AiPlayerbot.Supervisor.Arbiter = 1 (behaviour pair 1, default 0), runs the arbiter shadow, which only watches the live travel
// target (SupervisorShadow.h). Below Arbiter 2 it writes nothing of the bot: no travel target, no status, no delay, no strategy.
// A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0; design A3-S3-T02-ENTWURF.md v0.3): the supervisor keeps the
// lane that owns the live travel target (the gate in SupervisorEnforce.cpp admits or denies the writes on it), sets its mode
// via SupervisorPolicy::Decide on the arming and disarming edges, and runs the probe (AiPlayerbot.Supervisor.ArbiterProbe).
// The cadence floor and the group mode come in their own pairs, each behind this switch and each with its own A/B (ADR-0041
// rules 1 and 3); legacy writers are deleted only after adoption.
namespace ai::supervisor
{
// R5 target memory: where the bot was and why a target was closed (filled from the behaviour pairs on).
struct TargetMemory
{
    std::uint32_t destId = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t arrivedMs = 0;
    std::uint32_t closedUntilMs = 0;
    std::uint8_t closeReason = 0;
};
constexpr std::size_t kTargetMemorySlots = 8;

class Supervisor
{
public:
    explicit Supervisor(PlayerbotAI* ai);
    ~Supervisor();   // out of line in Supervisor.cpp: PlayerbotAI.h holds a unique_ptr to the incomplete type

    Supervisor(Supervisor const&) = delete;
    Supervisor& operator=(Supervisor const&) = delete;

    // Once per engine pass while the switch applies: the counter, the mail, then the arbiter shadow (shadowOn = arbiter >= 1) closes
    // the bundle of the pass before. arbiter is the gate's value (ArbiterValue: 0 off, 1 shadow, 2 enforce). A3 step 3 T02-a:
    // enforce = arbiter == 2; its edges set the mode (arming: Passive -> Quest in the quest book scope; disarming: -> Passive),
    // then the probe runs (AiPlayerbot.Supervisor.ArbiterProbe = 1, probe bots only). Below 2 nothing is written to the bot.
    void Tick(std::uint32_t nowMs, std::uint32_t arbiter);

    // The switch no longer applies (reload 1 -> 0, or the bot left the cohort): leave the travel target as it is; the shadow
    // closes its open bundle. A3 step 3 T02-a: the lane owner and the enforcement state are dropped, the mode stays. The caller
    // drops the instance right after.
    void Release();

    std::uint64_t Ticks() const { return ticks_; }
    Mode CurrentMode() const { return mode_; }
    Phase CurrentPhase() const { return phase_; }

    // Behaviour pair 1: the arbiter shadow of this bot (SupervisorShadow.h; used under its file-local mutex only).
    ShadowState& Shadow() { return shadow_; }

    // ---- A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): the lane owner, bot thread only --------------------
    // Enforcement acts on a bot only while arbiter 2 is armed and the bot left Passive (ArmEnforce moves only the questbook-scope
    // bots to Quest; a Passive bot - real-player master, battleground, parked - writes as before, no owner, no deny, no probe).
    bool EnforceOn() const { return enforce_ && mode_ != Mode::Passive; }
    bool InProbe() const { return inProbe_; }
    // The lane that holds the live target now (ArbiterLanePolicy.h OwnerHolds with the target's write generation, status and
    // destination; a group owner holds in group mode, and during the probe). An owner that no longer holds is cleared here.
    Lane HeldOwner(ai::TravelTarget* target);
    // The lane owns the target for the write generation gen.
    void SetOwner(Lane lane, std::uint32_t gen);
    // Two steps, so a write that never happens (the #485 parked refusal runs after the gate) leaves no owner: the gate stages the
    // lane of an admitted write (None for a neutral one), the write commits it with the generation it created (SupervisorEnforce.h).
    void StageOwner(Lane lane) { stagedOwner_ = lane; staged_ = true; }
    void CommitOwner(std::uint32_t gen)
    {
        if (!staged_)
            return;
        staged_ = false;
        if (stagedOwner_ != Lane::None)
            SetOwner(stagedOwner_, gen);
    }

private:
    // Mailbox pair: takes this bot's mail, oldest first, once per tick; the mailbox counts it (GroupDirective separately). Nothing
    // acts on it in that pair.
    void TakeMail();

    // A3 step 3 T02-a: the arming (false -> true) and disarming (true -> false) edges of enforcement, and the mode line of a Decide.
    void ArmEnforce();
    void DisarmEnforce(std::uint32_t arbiter);
    void WriteModeLine(Mode from, Mode to, TransitionResult const& result, char const* cause, std::uint32_t arbiter);
    // A3 step 3 T02-a: the probe (AiPlayerbot.Supervisor.ArbiterProbe). Defined in SupervisorEnforce.cpp next to the gate it
    // tests; at arbiter 2 it tries three denied writes as Select under a group owner, at 1 it only counts the paired control.
    void ProbeTick(std::uint32_t nowMs, std::uint32_t arbiter);

    PlayerbotAI* ai_;
    Mode mode_ = Mode::Passive;
    Phase phase_ = Phase::Plan;
    std::uint64_t ticks_ = 0;
    [[maybe_unused]] TargetMemory memory_[kTargetMemorySlots];
    ShadowState shadow_;
    // A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0).
    bool enforce_ = false;
    Lane owner_ = Lane::None;
    std::uint32_t ownerGen_ = 0;
    Lane stagedOwner_ = Lane::None;
    bool staged_ = false;
    bool inProbe_ = false;
    std::uint32_t nextProbeMs_ = 0;   // 0 = not scheduled yet
};

// The module-wide mailbox (design section 1.10), keyed by the bot's low guid: other threads (the group manager on the world
// thread, the bot brain's answer thread) only know the guid; no pointer to a Supervisor or PlayerbotAI leaves the bot's thread.
// Defined by the mailbox pair in SupervisorMail.cpp (pure rules SupervisorMailPolicy.h, contract
// a3s3_supervisor_mailbox_source_contract_tests.cmake): Drain only in Supervisor::TakeMail (the tick), Forget only in ~PlayerbotAI
// (logout and every other AI teardown). Post has no caller in that pair; the group manager's directives (T30) are the first.
bool Post(std::uint32_t guidLow, Event const& event);              // fixed ring per guid under a mutex; full -> dropped
std::size_t Drain(std::uint32_t guidLow, Event* out, std::size_t max);   // bot thread only, from the tick
void Forget(std::uint32_t guidLow);                                 // logout / AI teardown: drop the ring
bool MailWaiting();                                                 // any ring holds an event (one atomic load, no lock)
}
