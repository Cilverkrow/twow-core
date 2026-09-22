#include "playerbot/ProgressAwareTurnInRecoveryPolicy.h"

#include <cassert>
#include <iostream>

using ai::turnin_recovery::Observation;
using ai::turnin_recovery::RecoveryAction;
using ai::turnin_recovery::State;

namespace
{
Observation At(std::uint32_t now, float x, float distance, bool paused = false, std::uint32_t map = 0,
    std::uint32_t zone = 1)
{
    Observation observation;
    observation.now = now;
    observation.targetEntry = 60517;
    observation.questId = 40273;
    observation.mapId = map;
    observation.zoneId = zone;
    observation.x = x;
    observation.distance = distance;
    observation.paused = paused;
    return observation;
}
}

int main()
{
    State state;
    constexpr std::uint32_t stall = 300;
    constexpr std::uint32_t cooldown = 120;

    // Hillsbrad's 40273 -> 60517 route starts a non-expiring, observed target.
    assert(ai::turnin_recovery::Observe(state, At(0, 0.0f, 8000.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(state, At(299, 0.0f, 8000.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(state, At(300, 30.0f, 7970.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(state, At(900, 90.0f, 7800.0f), stall, cooldown) == RecoveryAction::None);

    // Flights, boats/trains, map loading, combat, death/revive and a real
    // master are pauses/progress, never a wall-clock failure.
    assert(ai::turnin_recovery::Observe(state, At(1300, 90.0f, 7800.0f, true), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(state, At(1600, 90.0f, 7800.0f, false, 1, 2), stall, cooldown) == RecoveryAction::None);

    // A genuinely stalled repeated route is recovered in two bounded stages.
    State stalled;
    assert(ai::turnin_recovery::Observe(stalled, At(0, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(stalled, At(300, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(stalled, At(600, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);
    assert(ai::turnin_recovery::IsSuppressed(stalled, 601, 60517, 0));
    assert(!ai::turnin_recovery::IsSuppressed(stalled, 721, 60517, 0));

    // A new target has independent progress state; a short same-map walk and
    // a long, progressive route are both valid.
    Observation next = At(800, 0.0f, 20.0f);
    next.targetEntry = 295;
    next.questId = 2158;
    assert(ai::turnin_recovery::Observe(stalled, next, stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(stalled, At(1100, 30.0f, 0.0f), stall, cooldown) == RecoveryAction::None);

    std::cout << "progress_aware_turnin_recovery=PASS hillsbrad=PASS transport_pause=PASS "
        "stalled_route_recovery=PASS suppress_cooldown=PASS relog_state_reset=PASS\n";
}
