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
// counts, an empty stack is Unknown. IfEmpty pushes only on an empty stack (the bot brain fallback in setNewTarget).
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
// ArbiterOn is the gate's value (PlayerbotAI::UpdateAI, once per engine pass; configValue = AiPlayerbot.Supervisor.Arbiter).
// SetArbiterOverride (console "rndbot arbiter on|off", world thread) stores kArbiterOn / kArbiterOff and returns the override
// before the call (kArbiterFollowConfig while the console never set it). A bot sees the new value at its next engine pass; the
// shadow's own on/off handling (ShadowTick) is the same as for a reload of the key.
bool ArbiterOn(std::uint32_t configValue);
int SetArbiterOverride(bool on);
}
