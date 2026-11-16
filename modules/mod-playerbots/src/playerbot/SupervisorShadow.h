#pragma once

#include "playerbot/SupervisorPolicy.h"

#include <cstddef>
#include <cstdint>

class PlayerbotAI;

namespace ai
{
class TravelDestination;
class TravelTarget;
}

// A3 step 3, behaviour pair 1 (ADR-0041; AiPlayerbot.Supervisor.Arbiter, default 0): the arbiter shadow. Design:
// ws-00/projekt-durchleuchtung/A3-S3-ARBITER-SCHATTEN-ENTWURF.md v1.2 (OB-00 07.11.: E1-E4 a, build rules 1-4). It watches the
// live travel target of a bot the supervisor runs for and writes [Supervisor] lines and [ArbiterMin] sums. It never writes the
// target, a value, a strategy or the bot. The switch is read only at the gate in PlayerbotAI::UpdateAI (Tick's shadowOn).
namespace ai::supervisor
{
// The thread a write runs on (OB-00 build rule 07.11. point 2): the bot's own map update, the world thread while the maps are
// idle (world hook, console handler), or none of them.
enum class ThreadKind : std::uint8_t
{
    None = 0,
    Bot = 1,
    World = 2,
};

// RAII, first statement of PlayerbotAI::UpdateAI (Bot, owner = that bot), of the world hook and of the console handler (World,
// owner none). keepSet: an outer tag stays (the console handler reached from a bot's map thread keeps that bot's tag).
class ThreadTag
{
public:
    ThreadTag(ThreadKind kind, PlayerbotAI const* owner, bool keepSet = false);
    ~ThreadTag();

    ThreadTag(ThreadTag const&) = delete;
    ThreadTag& operator=(ThreadTag const&) = delete;

private:
    ThreadKind prevKind_;
    PlayerbotAI const* prevOwner_;
};

// RAII writer family of the running entry function (design 4.3, E1 a): a thread_local stack of depth 4, the innermost entry
// counts, an empty stack is Unknown. IfEmpty pushes only on an empty stack (the bot brain fallback in setNewTarget). A push past
// depth 4 is counted (depth_overflow in [ArbiterMin], A3 step 3 T02-a) while a shadow is on.
class WriterScope
{
public:
    explicit WriterScope(WriterId writer);
    static WriterScope IfEmpty(WriterId writer);
    ~WriterScope();

    WriterScope(WriterScope const&) = delete;
    WriterScope& operator=(WriterScope const&) = delete;

private:
    WriterScope(WriterId writer, bool onlyIfEmpty);

    bool pushed_;
};

// A destination as the shadow keeps it across engine passes: never a pointer. A temporary destination is deleted when the
// target gets another one (TravelMgr.cpp), so the line text is copied while the destination is alive, and the address is kept only
// as a number for identity, with the kind (temporary or not).
constexpr std::size_t kDestNameSize = 96;
struct DestKey
{
    std::uintptr_t id = 0;
    bool temp = false;
    char name[kDestNameSize] = "idle:0:0";
};

// The shadow of one supervisor. Read and written only under the file-local mutex in SupervisorShadow.cpp (build rule 1).
struct ShadowState
{
    bool on = false;      // the last tick ran with the shadow on
    bool init = false;    // the first observed write after on only initialises
    DestKey last;
    std::uint8_t lastStatus = kStatusNone;
    bool lastNull = true;
    std::uint32_t destSinceMs = 0;
    DestKey from;
    bool open = false;    // a bundle is open
    Bundle pending;
};

// The observer: the last statement of the status write on a live target (TravelMgr.cpp), any thread. A write without thread tag,
// or from another bot's map thread, never reads the owner's supervisor: it is counted and marks the target (gap=1 next time).
void ShadowObserve(PlayerbotAI* owner, ai::TravelTarget* target);

// From Supervisor::Tick (once per engine pass, the bot's map thread): closes the open bundle, writes its line under the budget,
// and once per minute the [ArbiterMin] sums. shadowOn false drops the state without a line.
void ShadowTick(PlayerbotAI* owner, ShadowState& state, bool shadowOn);

// From Supervisor::Release: closes the open bundle and drops the state.
void ShadowRelease(PlayerbotAI* owner, ShadowState& state);

// From ~Supervisor (the bot's AI may be in teardown): drops the state without a line and without touching the bot. When the
// last shadow is off, the observer is disarmed again and the minute sums are cleared.
void ShadowDrop(ShadowState& state);

// Live arbiter switch (OB-00 09.11.2026; SupervisorPolicy.h ParseArbiterSwitch / ArbiterEffective). One process-wide atomic, no
// config reload: PlayerbotAIConfig::Initialize is never called for it (map threads read the config without a lock).
// ArbiterValue is the gate's value (PlayerbotAI::UpdateAI, once per engine pass; configValue = AiPlayerbot.Supervisor.Arbiter):
// 0 off, 1 shadow, 2 enforce (A3 step 3 T02-a, AiPlayerbot.Supervisor.Arbiter = 2, default 0). ArbiterOn = ArbiterValue >= 1
// (the shadow runs), kept for the console contract. SetArbiterOverride (console "rndbot arbiter on|off|enforce", world thread)
// stores kArbiterOff / kArbiterOn / kArbiterEnforce (any other value changes nothing) and returns the override before the call
// (kArbiterFollowConfig while the console never set it). A bot sees the new value at its next engine pass; the shadow's own
// on/off handling (ShadowTick) is the same as for a reload of the key.
std::uint32_t ArbiterValue(std::uint32_t configValue);
bool ArbiterOn(std::uint32_t configValue);
int SetArbiterOverride(int value);

// ---- A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): what the enforcing gate needs from the shadow ----------
// The writer family of the running entry: the innermost WriterScope, Unknown on an empty stack (the same value the observer
// records).
WriterId CurrentWriterId();

// The calling thread is this bot's own map update (ThreadTag Bot with this owner): only then may the gate read the bot's
// supervisor. World, untagged and other bots' threads are always admitted (their write raises the write generation, which ends
// any ownership).
bool OnOwnThread(PlayerbotAI const* ai);

// A3 step 3 T02-GROUP (gate_offthread, design 7.12): the tag of the calling thread (Bot = some bot's map update, World, None).
ThreadKind CurrentThreadKind();

// The enforcement counters of the [ArbiterMin] minute (SupervisorPolicy.h ArbiterMinute), per cohort, under the shadow's mutex.
// Counted only while a shadow minute runs (enforcement implies the shadow, 2 >= 1).
// A3 step 3 T02-GROUP (AiPlayerbot.Supervisor.Arbiter = 2, BotGroups.Manager.Types > 0; default 0): the three gate_off_* fields
// of [ArbiterMin], then the [ArbiterGrp] block - one value per SupervisorPolicy.h GrpField, same names, same order (from DirEnter
// to GatherKind3; SupervisorShadow.cpp asserts every value against its GrpField). The final list, in print order:
//   dir_enter dir_update dir_exit dir_bad superseded enter_ok enter_ref update_ok update_ref exit_ok exit_stale bad_ref
//   ack_drop_enter ack_drop_exit left left_drop left_cancel r_deny r16 r19 r20 r21 r22 r23 r24 r25 orphan goal_done goal_lost
//   rescue_in rescue_group rescue_back leader_far follow_make follow_turnin point_reapply held_group_copy goal_close prep_cancel
//   close_burst close_warn band_held goal_adopt_enter goal_adopt_write cross_keep owner_q owner_g nonquest follow_self
//   handler_off gather_kind3
enum class EnforceCount : std::uint8_t
{
    Deny,
    DenyProbe,
    HeldRequest,
    HeldErrand,
    ProbePass,
    ProbeFail,
    ProbeShadow,
    // T02-GROUP gate_offthread (design 7.12): [ArbiterMin] gate_off_world / gate_off_none / gate_off_bot.
    GateOffWorld,
    GateOffNone,
    GateOffBot,
    // T02-GROUP [ArbiterGrp] (design 7.11), GrpField order.
    DirEnter,
    DirUpdate,
    DirExit,
    DirBad,
    Superseded,
    EnterOk,
    EnterRef,
    UpdateOk,
    UpdateRef,
    ExitOk,
    ExitStale,
    BadRef,
    AckDropEnter,
    AckDropExit,
    Left,
    LeftDrop,
    LeftCancel,
    RDeny,
    R16,
    R19,
    R20,
    R21,
    R22,
    R23,
    R24,
    R25,
    Orphan,
    GoalDone,
    GoalLost,
    RescueIn,
    RescueGroup,
    RescueBack,
    LeaderFar,
    FollowMake,
    FollowTurnin,
    PointReapply,
    HeldGroupCopy,
    GoalClose,
    PrepCancel,
    CloseBurst,
    CloseWarn,
    BandHeld,
    GoalAdoptEnter,
    GoalAdoptWrite,
    CrossKeep,
    OwnerQ,
    OwnerG,
    Nonquest,
    FollowSelf,
    HandlerOff,
    GatherKind3,
};
constexpr EnforceCount kGrpFirst = EnforceCount::DirEnter;
static_assert(std::size_t(EnforceCount::GatherKind3) - std::size_t(kGrpFirst) + 1 == kGrpFields,
    "the [ArbiterGrp] block of EnforceCount spans every GrpField");
void CountEnforce(std::uint32_t guidLow, EnforceCount what);

// One enforcement line (ev=deny, ev=probe, ev=mode) from the shared budget of 300 lines per minute; a refused line counts as
// dropped. The text is written as given (already formatted and sanitized by the caller).
void EmitEnforceLine(std::uint32_t guidLow, char const* text);

// R9b V12: the disarming edge's ev=mode line only (cause=arbiter_disarm, Supervisor::DisarmEnforce). Outside the budget, never
// dropped, counted in lines; bounded by one line per bot and disarming edge.
void EmitEdgeLine(std::uint32_t guidLow, char const* text);
}
