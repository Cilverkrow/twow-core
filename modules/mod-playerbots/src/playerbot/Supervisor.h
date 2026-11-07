#pragma once

#include "playerbot/SupervisorPolicy.h"

#include <cstddef>
#include <cstdint>

class PlayerbotAI;

// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor, default 0): one supervisor per bot, on
// the bot's map thread (PlayerbotAI::UpdateAI, in the engine pass). Design: ws-00/projekt-durchleuchtung/A3-S3-ENTWURF.md
// (c98e6ded). SCAFFOLD: the instance exists only while the switch applies to the bot, and Tick only counts the engine passes.
// It reads and writes nothing of the bot: no travel target, no status, no delay, no strategy, no log line. Mode changes (via
// SupervisorPolicy::Decide), the arbiter on the travel target, the cadence floor and the decision lines come in their own pairs,
// each behind this switch and each with its own A/B (ADR-0041 rules 1 and 3); legacy writers are deleted only after adoption.
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

    // Once per engine pass while the switch applies (scaffold: the bare counter).
    void Tick(std::uint32_t nowMs);

    // The switch no longer applies (reload 1 -> 0, or the bot left the cohort): leave the travel target as it is. The caller
    // drops the instance right after.
    void Release();

    std::uint64_t Ticks() const { return ticks_; }
    Mode CurrentMode() const { return mode_; }
    Phase CurrentPhase() const { return phase_; }

private:
    [[maybe_unused]] PlayerbotAI* ai_;   // read from the first behaviour pair on
    Mode mode_ = Mode::Passive;
    Phase phase_ = Phase::Plan;
    std::uint64_t ticks_ = 0;
    [[maybe_unused]] TargetMemory memory_[kTargetMemorySlots];
};

// The module-wide mailbox (design section 1.10), keyed by the bot's low guid: other threads (the group manager on the world
// thread, the bot brain's answer thread) only know the guid; no pointer to a Supervisor or PlayerbotAI leaves the bot's thread.
// DECLARED ONLY in this scaffold: the pair that wires the first event defines them, together with Forget in ~PlayerbotAI. The
// contract pins that nothing calls them yet.
bool Post(std::uint32_t guidLow, Event const& event);              // fixed ring per guid under a mutex; full -> dropped
std::size_t Drain(std::uint32_t guidLow, Event* out, std::size_t max);   // bot thread only, in UpdateAI
void Forget(std::uint32_t guidLow);                                 // logout / AI teardown: drop the ring
}
