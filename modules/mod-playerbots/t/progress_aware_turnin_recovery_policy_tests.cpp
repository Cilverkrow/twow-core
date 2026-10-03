// The CI gate builds Release (-DNDEBUG), which compiled every assert below out
// and left this suite checking nothing. Keep assertions live in this test.
#undef NDEBUG
#include "playerbot/ProgressAwareTurnInRecoveryPolicy.h"
#include "playerbot/TransportStallPolicy.h"

#include <cassert>
#include <iostream>

using ai::turnin_recovery::Observation;
using ai::turnin_recovery::RecoveryAction;
using ai::turnin_recovery::State;

namespace
{
Observation At(std::uint32_t now, float x, float distance, bool paused = false, std::uint32_t map = 0,
    std::uint32_t zone = 1, std::uint32_t targetMap = 0)
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
    observation.targetMapId = targetMap;
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

    // #307: deaths on the same turn-in route are failed attempts. The graveyard
    // teleport and target re-selection (ResetProgress) must not forget them.
    State dying;
    std::uint32_t const deathCooldown = 3600;
    assert(!ai::turnin_recovery::RecordDeathOnRoute(dying, 61746, 40001, 0, 100, 2, deathCooldown));
    dying.ResetProgress();
    assert(!ai::turnin_recovery::IsSuppressed(dying, 101, 61746, 0));
    assert(ai::turnin_recovery::RecordDeathOnRoute(dying, 61746, 40001, 0, 200, 2, deathCooldown));
    assert(ai::turnin_recovery::IsSuppressed(dying, 201, 61746, 0));
    assert(ai::turnin_recovery::IsSuppressed(dying, 3799, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(dying, 3800, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(dying, 201, 60517, 0));

    // A death on a different turn-in starts a fresh count.
    State switching;
    assert(!ai::turnin_recovery::RecordDeathOnRoute(switching, 61746, 40001, 0, 100, 2, deathCooldown));
    assert(!ai::turnin_recovery::RecordDeathOnRoute(switching, 60517, 40273, 0, 200, 2, deathCooldown));
    assert(!ai::turnin_recovery::IsSuppressed(switching, 201, 60517, 0));

    // 0 disables the death rule.
    State disabled;
    for (std::uint32_t i = 0; i < 10; ++i)
        assert(!ai::turnin_recovery::RecordDeathOnRoute(disabled, 61746, 40001, 0, i, 0, deathCooldown));

    // #329: the same turn-in dropped and re-selected every few seconds keeps its
    // progress state, so the stall window runs out (live: 96 re-selections of
    // Pumpmaster Galvax at a constant 223 yards, never recovered).
    State churn;
    assert(ai::turnin_recovery::Observe(churn, At(0, 0.0f, 223.0f), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 5; t < 300; t += 5)
    {
        assert(!ai::turnin_recovery::ShouldResetProgressOnSetTarget(churn, true, 60517, 40273));
        assert(!ai::turnin_recovery::ShouldResetProgressOnSetTarget(churn, false, 0, 0));
        assert(ai::turnin_recovery::Observe(churn, At(t, 0.0f, 223.0f), stall, cooldown) == RecoveryAction::None);
    }
    assert(ai::turnin_recovery::Observe(churn, At(300, 0.0f, 223.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(churn, At(600, 0.0f, 223.0f), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);

    // A genuinely different quest target still starts fresh.
    assert(ai::turnin_recovery::ShouldResetProgressOnSetTarget(churn, true, 295, 2158));
    State fresh;
    assert(!ai::turnin_recovery::ShouldResetProgressOnSetTarget(fresh, true, 295, 2158));

    // twow-repo#485: a cross-map turn-in (bot on map 1, taker on map 0) is
    // suppressed under the taker's map, the key the route choice asks with.
    // Keyed by the bot's map it never matched: 30 of 101 such targets were
    // chosen again within the 120 s.
    State crossMap;
    assert(ai::turnin_recovery::Observe(crossMap, At(0, 0.0f, 9000.0f, false, 1, 1, 0), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(crossMap, At(300, 0.0f, 9000.0f, false, 1, 1, 0), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(crossMap, At(600, 0.0f, 9000.0f, false, 1, 1, 0), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);
    assert(ai::turnin_recovery::IsSuppressed(crossMap, 601, 60517, 0));
    assert(!ai::turnin_recovery::IsSuppressed(crossMap, 601, 60517, 1));
    assert(!ai::turnin_recovery::IsSuppressed(crossMap, 721, 60517, 0));

    // twow-repo#485: the death cooldown (#307) has its own slot. A stall on
    // another turn-in used to overwrite it, so the bot was back on the deadly
    // route after 120 s instead of 3600 s.
    State both;
    assert(!ai::turnin_recovery::RecordDeathOnRoute(both, 61746, 40001, 0, 100, 2, deathCooldown));
    assert(ai::turnin_recovery::RecordDeathOnRoute(both, 61746, 40001, 0, 200, 2, deathCooldown));
    assert(ai::turnin_recovery::Observe(both, At(300, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(both, At(600, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(both, At(900, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);
    assert(ai::turnin_recovery::IsSuppressed(both, 901, 60517, 0));
    assert(ai::turnin_recovery::IsSuppressed(both, 901, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(both, 1021, 60517, 0));
    assert(ai::turnin_recovery::IsSuppressed(both, 3799, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(both, 3800, 61746, 0));

    // ... and a later death suppression leaves a running stall suppression alone.
    State stallThenDeath;
    assert(ai::turnin_recovery::Observe(stallThenDeath, At(0, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(stallThenDeath, At(300, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(stallThenDeath, At(600, 0.0f, 9000.0f), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);
    assert(!ai::turnin_recovery::RecordDeathOnRoute(stallThenDeath, 61746, 40001, 0, 610, 2, deathCooldown));
    assert(ai::turnin_recovery::RecordDeathOnRoute(stallThenDeath, 61746, 40001, 0, 620, 2, deathCooldown));
    assert(ai::turnin_recovery::IsSuppressed(stallThenDeath, 621, 60517, 0));
    assert(ai::turnin_recovery::IsSuppressed(stallThenDeath, 621, 61746, 0));

    // Hotfix 8.13 + twow-repo#485: the transport abandon in
    // TravelTarget::ObserveTurnInProgress writes the stall slot under the
    // target's map (observation.targetMapId) and resets the progress. A death
    // suppression on another turn-in keeps its own slot and its full cooldown.
    State transport;
    ai::transport_stall::State platform;
    Observation const dock = At(1000, 0.0f, 9000.0f, false, 1, 1, 0);
    std::uint32_t const abandonAt = dock.now + ai::transport_stall::StallMs;
    assert(platform.Observe(dock.targetEntry, dock.questId, dock.targetMapId != dock.mapId, dock.paused,
        dock.distance, dock.now) == ai::transport_stall::Step::None);
    assert(!ai::turnin_recovery::RecordDeathOnRoute(transport, 61746, 40001, 0, abandonAt - 1000, 2, deathCooldown));
    assert(ai::turnin_recovery::RecordDeathOnRoute(transport, 61746, 40001, 0, abandonAt - 500, 2, deathCooldown));
    assert(platform.Observe(dock.targetEntry, dock.questId, dock.targetMapId != dock.mapId, dock.paused,
        dock.distance, abandonAt) == ai::transport_stall::Step::Abandon);
    transport.suppressedEntry = dock.targetEntry;
    transport.suppressedMapId = dock.targetMapId;
    transport.suppressUntil = abandonAt + ai::transport_stall::CooldownMs;
    transport.ResetProgress();
    assert(ai::turnin_recovery::IsSuppressed(transport, abandonAt + 1, 60517, 0));
    assert(!ai::turnin_recovery::IsSuppressed(transport, abandonAt + 1, 60517, 1));
    assert(ai::turnin_recovery::IsSuppressed(transport, abandonAt + 1, 61746, 0));
    assert(ai::turnin_recovery::IsSuppressed(transport, abandonAt - 500 + deathCooldown - 1, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(transport, abandonAt - 500 + deathCooldown, 61746, 0));
    assert(!ai::turnin_recovery::IsSuppressed(transport, abandonAt + ai::transport_stall::CooldownMs, 60517, 0));
    // The reset progress restarts the turn-in observation on the next pick.
    assert(ai::turnin_recovery::Observe(transport, At(abandonAt + 10, 0.0f, 9000.0f, false, 1, 1, 0), stall, cooldown) ==
        RecoveryAction::None);

    std::cout << "progress_aware_turnin_recovery=PASS hillsbrad=PASS transport_pause=PASS "
        "stalled_route_recovery=PASS suppress_cooldown=PASS relog_state_reset=PASS death_route_suppression=PASS "
        "same_target_churn=PASS cross_map_stall_key=PASS death_survives_stall=PASS "
        "transport_abandon_target_map=PASS\n";
}
