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
// target (SupervisorShadow.h). Below Arbiter 2 it writes nothing of the bot: no travel target, no status, no delay, no strategy,
// except T02-GROUP's GroupTick in the one tick of a switch away from 2, which leaves a held group (LeaveGroupMode).
// A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0; design A3-S3-T02-ENTWURF.md v0.3): the supervisor keeps the
// lane that owns the live travel target (the gate in SupervisorEnforce.cpp admits or denies the writes on it), sets its mode
// via SupervisorPolicy::Decide on the arming and disarming edges, and runs the probe (AiPlayerbot.Supervisor.ArbiterProbe).
// The cadence floor comes in its own pair, behind this switch and with its own A/B (ADR-0041 rules 1 and 3). The group mode
// (T02-GROUP, SupervisorGroup.cpp) and the group manager T30 are one lever with one A/B (AiPlayerbot.BotGroups.Manager.Types
// at Arbiter 2): the owner's exception to rules 1 and 3 for Q4 (a) (10.11.2026, relayed by OB-00); the T30 A/B is the
// T02-GROUP A/B. Legacy writers are deleted only after adoption.
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
    // then the probe runs (AiPlayerbot.Supervisor.ArbiterProbe = 1, probe bots only). Below 2 nothing is written to the bot,
    // except T02-GROUP's GroupTick in the one tick of a switch away from 2, which leaves a held group (LeaveGroupMode).
    void Tick(std::uint32_t nowMs, std::uint32_t arbiter);

    // The switch no longer applies (reload 1 -> 0, or the bot left the cohort): leave the travel target as it is; the shadow
    // closes its open bundle. A3 step 3 T02-a: the lane owner and the enforcement state are dropped, the mode stays. The caller
    // drops the instance right after. T02-GROUP: a held group is left first (GroupRelease).
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

    // ---- A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0) ----------------
    // Read by SupervisorEnforce.cpp through EnforcingSupervisor only (own bot thread).
    std::uint32_t GroupGoalQuest() const { return mode_ == Mode::Group ? groupQuest_ : 0; }
    bool SelfExitLocked() const { return selfExitSerial_ != 0; }
    void NoteWriter(WriterId writer, std::uint32_t gen) { lastWriter_ = writer; lastWriterGen_ = gen; }
    void AdoptGroupOwner(std::uint32_t gen, EnforceCount why);   // SupervisorGroup.cpp; the only group lane owner write

private:
    // Mailbox pair: takes this bot's mail, oldest first, once per tick; the mailbox counts it (GroupDirective separately).
    // T02-GROUP: the drained events go to HandleMail (SupervisorGroup.cpp), which only keeps the newest group directive.
    void TakeMail();

    // A3 step 3 T02-a: the arming (false -> true) and disarming (true -> false) edges of enforcement, and the mode line of a Decide.
    void ArmEnforce();
    void DisarmEnforce(std::uint32_t arbiter);
    void WriteModeLine(Mode from, Mode to, TransitionResult const& result, char const* cause, std::uint32_t arbiter);
    // A3 step 3 T02-a: the probe (AiPlayerbot.Supervisor.ArbiterProbe). Defined in SupervisorEnforce.cpp next to the gate it
    // tests; at arbiter 2 it tries three denied writes as Select under a group owner, at 1 it only counts the paired control.
    void ProbeTick(std::uint32_t nowMs, std::uint32_t arbiter);

    // ---- A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0) ----------------
    // The bot side of the group manager, defined in SupervisorGroup.cpp (design A3-S3-T02-ENTWURF.md v0.4.3 section 7), bot
    // thread only. HandleMail is the last statement of TakeMail and only keeps the newest group directive; GroupTick runs right
    // after the shadow hand-over and answers it; GroupRelease (first statement of Release) leaves a held group; LeaveGroupMode is
    // the one full reset; PeekOwner is HeldOwner without clearing the owner (the owner sample).
    void HandleMail(Event const* mail, std::size_t n);
    void GroupTick(std::uint32_t nowMs, std::uint32_t arbiter);
    void GroupRelease();
    void LeaveGroupMode(char const* cause);
    Lane PeekOwner(ai::TravelTarget* target) const;
    // Helpers of SupervisorGroup.cpp (state and reason as numbers: the ack constants live in the pure group headers).
    std::uint16_t AnswerDirective(Event const& e, std::uint32_t arbiter);
    void EnterGroup(Event const& e, bool arbiter2, bool handlerOn, std::uint8_t& state, std::uint8_t& reason);
    void ChangeGroup(Event const& e, bool arbiter2, bool handlerOn, std::uint8_t& state, std::uint8_t& reason);
    void SendAck(Event const& e, std::uint8_t state, std::uint8_t reason);
    void SendLeft(std::uint16_t serial, std::uint8_t reason, bool retry);
    void CloseLive(ai::TravelTarget* target, std::uint8_t status, char const* cause, bool loop);
    bool ManagedNow();
    void StartFollow();
    void FollowCheck();
    void UndoFollow();
    bool ApplyMove();
    bool PointWrite();
    void ApplyGoalRule(ai::TravelTarget* target);
    void ForgetGroup();
    void GroupToQuest(Requester who, char const* cause);
    void CommitMode(TransitionResult const& result, Mode from, Mode to, Requester who, char const* cause);
    void GroupModeLine(Mode from, Mode to, TransitionResult const& result, Requester who, char const* cause);

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
    // A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): plain fields, bot thread.
    // The mailbox registration (R10): the guid and the token of this instance (0 = not registered).
    std::uint32_t mailGuid_ = 0;
    std::uint32_t mailToken_ = 0;
    // The newest directive HandleMail took; GroupTick answers it in the same tick.
    bool hasPendingDir_ = false;
    Event pendingDir_{};
    // The held group (GROUP, or RESCUE with a held group): serial, seq, type, the goal quest c, objective, anchor, gather bits.
    std::uint16_t groupSerial_ = 0;
    std::uint32_t groupSeq_ = 0;
    std::uint8_t groupType_ = 0;
    std::uint32_t groupQuest_ = 0;
    std::int8_t groupObjective_ = -1;
    std::uint32_t groupMap_ = 0;
    float groupX_ = 0.0f;
    float groupY_ = 0.0f;
    std::uint8_t gatherKind_ = 0;
    bool gatherFlag_ = false;
    bool gatherDone_ = false;         // an Update cleared the gather flag after it was set (done = 1: no more following)
    std::uint32_t leaderLow_ = 0;     // the core group leader at Enter (0 = none); re-read only on an accepted move
    bool groupHeld_ = false;
    std::uint32_t groupArbiter_ = 0;  // the arbiter value of the running GroupTick (for the lines)
    // Flags: RESCUE entered by the handler; the one adoption after Enter; the follower role is the handler's.
    bool rescueByGroup_ = false;
    bool adoptDue_ = false;
    bool roleByHandler_ = false;
    std::uint8_t followPhase_ = 0;    // FollowPhase (SupervisorGroupPolicy.h): 0 none, 1 follow, 2 turn-in
    bool leaderFar_ = false;
    bool leaderSeen_ = false;         // the leader was near once in this hold (follow_self vs leader_far)
    bool followSelf_ = false;         // follow_self counted in this hold
    bool leadBegun_ = false;
    // Clocks (game ms; 0 = due now).
    std::uint32_t orphanFalseSinceMs_ = 0;
    std::uint32_t orphanNextAskMs_ = 0;
    std::uint32_t nextSecondMs_ = 0;
    std::uint32_t nextSampleMin_ = 0;
    // The left report and the self-exit lock.
    std::uint16_t selfExitSerial_ = 0;
    std::uint32_t selfExitSinceMs_ = 0;
    std::uint16_t leftRetrySerial_ = 0;
    std::uint8_t leftRetryReason_ = 0;   // 0 = no retry pending
    // The writer of the last admitted own-thread write (CommitTargetWrite) and its write generation.
    WriterId lastWriter_ = WriterId::Unknown;
    std::uint32_t lastWriterGen_ = 0;
    // The close ring of this bot (goal_close + prep_cancel times, CloseRing in SupervisorGroupPolicy.h).
    std::uint32_t closeAtMs_[32] = {};
    std::uint8_t closeHead_ = 0;
    std::uint8_t closeUsed_ = 0;
    bool closeBurst_ = false;
    bool closeWarn_ = false;
};

// The module-wide mailbox (design section 1.10), keyed by the bot's low guid: other threads (the group manager on the world
// thread, the bot brain's answer thread) only know the guid; no pointer to a Supervisor or PlayerbotAI leaves the bot's thread.
// Defined by the mailbox pair in SupervisorMail.cpp (pure rules SupervisorMailPolicy.h, contract
// a3s3_supervisor_mailbox_source_contract_tests.cmake): Drain only in Supervisor::TakeMail (the tick), Forget only in ~PlayerbotAI
// (logout and every other AI teardown). Post has no caller in that pair; the group manager's directives (T30) are the first.
// T02-GROUP registers the bot (RegisterMail in GroupTick, UnregisterMail in ~Supervisor): Post opens a ring only for a
// registered guid; the unregister holds a token, so a late teardown of an old instance never drops a newer registration.
bool Post(std::uint32_t guidLow, Event const& event);              // fixed ring per guid under a mutex; full -> dropped
std::size_t Drain(std::uint32_t guidLow, Event* out, std::size_t max);   // bot thread only, from the tick
void Forget(std::uint32_t guidLow);                                 // logout / AI teardown: drop the ring
bool MailWaiting();                                                 // any ring holds an event (one atomic load, no lock)
std::uint32_t RegisterMail(std::uint32_t guidLow);                  // bot thread, first GroupTick: the token (0 = guid 0)
void UnregisterMail(std::uint32_t guidLow, std::uint32_t token);    // ~Supervisor: only while the token is still the guid's
}
