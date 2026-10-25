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

// twow-repo#601 S3c: the same observation with the near zone of AiPlayerbot.ZoneExit.TurnInTravelStall.
Observation Near(std::uint32_t now, float x, float distance, float radius, std::uint32_t map = 0, std::uint32_t targetMap = 0)
{
    Observation observation = At(now, x, distance, false, map, 1, targetMap);
    observation.nearRadius = radius;
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

    // twow-repo#601 S3c (ZoneExit.TurnInTravelStall, OB-10 rev. 2 Q-B3): the Azora case. The bot jitters 25-35 yd
    // around the taker (every move picks a new random point) and never gets closer. With the 60 yd near zone that is
    // a stall: stage 1 at 300 s, stage 2 at 600 s. The move runs every few seconds live; every 20 s here (review F1:
    // a gap of more than stall / 2 starts a new trip).
    constexpr float nearRadius = 60.0f;
    auto jitterX = [](std::uint32_t t) { return (t / 20) % 2 ? 30.0f : 0.0f; };
    auto jitterDistance = [](std::uint32_t t) { return 28.0f + float((t / 20) % 4); };   // 28..31, never 3 below 30
    State jitter;
    assert(ai::turnin_recovery::Observe(jitter, Near(0, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t < 300; t += 20)
        assert(ai::turnin_recovery::Observe(jitter, Near(t, jitterX(t), jitterDistance(t), nearRadius), stall, cooldown) ==
            RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(jitter, Near(300, 30.0f, 28.0f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);
    for (std::uint32_t t = 320; t < 600; t += 20)
        assert(ai::turnin_recovery::Observe(jitter, Near(t, jitterX(t), jitterDistance(t), nearRadius), stall, cooldown) ==
            RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(jitter, Near(600, 30.0f, 31.0f, nearRadius), stall, cooldown) ==
        RecoveryAction::SuppressRouteAndCooldown);

    // Switch off (radius 0): the same jitter is progress at every 25 yd shift, as before - no stall at 300 s or 600 s.
    State jitterOff;
    assert(ai::turnin_recovery::Observe(jitterOff, Near(0, 0.0f, 30.0f, 0.0f), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t <= 600; t += 20)
        assert(ai::turnin_recovery::Observe(jitterOff, Near(t, jitterX(t), jitterDistance(t), 0.0f), stall, cooldown) ==
            RecoveryAction::None);

    // 3 yd closer than the best so far is progress and restarts the window; 2.5 yd is not.
    State closer;
    assert(ai::turnin_recovery::Observe(closer, Near(0, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t < 200; t += 20)
        assert(ai::turnin_recovery::Observe(closer, Near(t, jitterX(t), 29.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(closer, Near(200, 0.0f, 27.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 220; t < 500; t += 20)
        assert(ai::turnin_recovery::Observe(closer, Near(t, jitterX(t), 24.5f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(closer, Near(499, 0.0f, 24.5f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(closer, Near(500, 0.0f, 24.5f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);

    // Out of the near zone and back is not "closer": the 25 yd shift out counts (outside, unchanged), the return to
    // 40 yd does not beat the best of 30 yd.
    State wander;
    assert(ai::turnin_recovery::Observe(wander, Near(0, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t < 100; t += 20)
        assert(ai::turnin_recovery::Observe(wander, Near(t, jitterX(t), 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 100; t < 200; t += 20)
        assert(ai::turnin_recovery::Observe(wander, Near(t, 100.0f, 80.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 200; t < 400; t += 20)
        assert(ai::turnin_recovery::Observe(wander, Near(t, jitterX(t), 40.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(wander, Near(400, 30.0f, 40.0f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);

    // The approach from afar into the zone is progress; a cross-map target has no near zone (old rule).
    State approach;
    assert(ai::turnin_recovery::Observe(approach, Near(0, 0.0f, 500.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t < 250; t += 20)   // 36 yd per step: outside the zone, the old rule (best goes down to 68)
        assert(ai::turnin_recovery::Observe(approach, Near(t, 1.8f * float(t), 500.0f - 1.8f * float(t), nearRadius), stall,
            cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(approach, Near(250, 440.0f, 58.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 270; t < 500; t += 20)
        assert(ai::turnin_recovery::Observe(approach, Near(t, 440.0f + jitterX(t), 57.0f, nearRadius), stall, cooldown) ==
            RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(approach, Near(500, 440.0f, 54.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 520; t < 800; t += 20)
        assert(ai::turnin_recovery::Observe(approach, Near(t, 440.0f + jitterX(t), 53.0f, nearRadius), stall, cooldown) ==
            RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(approach, Near(799, 470.0f, 53.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(approach, Near(800, 440.0f, 53.0f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::InNearZone(60.0f, true, nearRadius));
    assert(!ai::turnin_recovery::InNearZone(60.5f, true, nearRadius));
    assert(!ai::turnin_recovery::InNearZone(10.0f, false, nearRadius));
    assert(!ai::turnin_recovery::InNearZone(10.0f, true, 0.0f));

    // Review F1: a stale lastProgressAt never stalls the first near observation of a new trip. The bot reached the
    // taker (best 4 yd), its work phase ran out (300 s, no observation), it walked off to 30 yd and picked the same
    // taker again (#329 keeps the state): a fresh window from there, the stall only 300 s later.
    State staleLeg;
    assert(ai::turnin_recovery::Observe(staleLeg, Near(0, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    for (std::uint32_t t = 20; t <= 100; t += 20)
        assert(ai::turnin_recovery::Observe(staleLeg, Near(t, float(t) / 4.0f, 30.0f - float(t) / 4.0f, nearRadius), stall,
            cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(staleLeg, Near(410, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(staleLeg.lastProgressAt == 410 && staleLeg.bestDistance == 30.0f);
    for (std::uint32_t t = 430; t < 710; t += 20)
        assert(ai::turnin_recovery::Observe(staleLeg, Near(t, jitterX(t), jitterDistance(t), nearRadius), stall, cooldown) ==
            RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(staleLeg, Near(710, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);
    // A gap of exactly stall / 2 is still the same trip (no new window), and a gap outside the near zone keeps the old
    // rule (radius 0 above: the sparse 300 s steps of the older cases still stall).
    State shortGap;
    assert(ai::turnin_recovery::Observe(shortGap, Near(0, 0.0f, 30.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(shortGap, Near(150, 30.0f, 29.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(shortGap, Near(300, 0.0f, 29.0f, nearRadius), stall, cooldown) == RecoveryAction::RecomputeRoute);
    State longGapOutside;
    assert(ai::turnin_recovery::Observe(longGapOutside, Near(0, 0.0f, 9000.0f, nearRadius), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(longGapOutside, Near(300, 0.0f, 9000.0f, nearRadius), stall, cooldown) ==
        RecoveryAction::RecomputeRoute);
    static_assert(ai::turnin_recovery::NearLegGapDivisor == 2, "a new trip after more than half the stall window");
    State otherMap;
    assert(ai::turnin_recovery::Observe(otherMap, Near(0, 0.0f, 30.0f, nearRadius, 1, 0), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(otherMap, Near(300, 30.0f, 32.0f, nearRadius, 1, 0), stall, cooldown) == RecoveryAction::None);

    // OB-10 rev. 2 Q-B4: the second work timeout of the same turn-in reports (the caller parks); the pair then starts
    // anew. Eight other quests push a quest out of the ring; quest 0 is never kept.
    ai::turnin_recovery::WorkTimeoutRing ring;
    assert(!ai::turnin_recovery::RecordWorkTimeout(ring, 41747));
    assert(!ai::turnin_recovery::RecordWorkTimeout(ring, 310));
    assert(ai::turnin_recovery::RecordWorkTimeout(ring, 41747));
    assert(!ai::turnin_recovery::RecordWorkTimeout(ring, 41747));
    assert(ai::turnin_recovery::RecordWorkTimeout(ring, 41747));
    assert(!ai::turnin_recovery::RecordWorkTimeout(ring, 0));
    assert(!ai::turnin_recovery::RecordWorkTimeout(ring, 0));
    ai::turnin_recovery::WorkTimeoutRing evict;
    assert(!ai::turnin_recovery::RecordWorkTimeout(evict, 41747));
    for (std::uint32_t quest = 1; quest <= ai::turnin_recovery::WorkTimeoutRingSize; ++quest)
        assert(!ai::turnin_recovery::RecordWorkTimeout(evict, quest));
    assert(!ai::turnin_recovery::RecordWorkTimeout(evict, 41747));

    // twow-repo#601 3a (ZoneExit.TurnInTravelStallCountsCombat): a fight with pauseKeepsWindow pauses the recovery but
    // keeps the stall window, so the first observation after it without progress recovers on the old clock.
    auto fight = [](std::uint32_t now, float x, float distance)
    {
        Observation observation = At(now, x, distance, true);
        observation.pauseKeepsWindow = true;
        return observation;
    };
    State combatOff;
    assert(ai::turnin_recovery::Observe(combatOff, At(0, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOff, At(200, 0.0f, 900.0f, true), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOff, At(300, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOff, At(499, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOff, At(500, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    State combatOn;
    assert(ai::turnin_recovery::Observe(combatOn, At(0, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOn, fight(200, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOn, At(299, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(combatOn, At(300, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    // A fight never triggers a stage itself, however long it lasts; the stage is not reset by it either.
    State longFight;
    assert(ai::turnin_recovery::Observe(longFight, At(0, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(longFight, At(300, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::RecomputeRoute);
    assert(ai::turnin_recovery::Observe(longFight, fight(1000, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(longFight.recoveryStage == 1);
    assert(ai::turnin_recovery::Observe(longFight, At(1001, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::SuppressRouteAndCooldown);
    // Real progress after the fight still counts: 30 yd moved restarts the window as before.
    State fightThenMove;
    assert(ai::turnin_recovery::Observe(fightThenMove, At(0, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(fightThenMove, fight(290, 0.0f, 900.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(fightThenMove, At(310, 30.0f, 870.0f), stall, cooldown) == RecoveryAction::None);
    assert(ai::turnin_recovery::Observe(fightThenMove, At(609, 30.0f, 870.0f), stall, cooldown) == RecoveryAction::None);

    std::cout << "near_zone_jitter_stall=PASS near_zone_off_unchanged=PASS near_zone_closer_3yd=PASS "
        "near_zone_wander_back=PASS near_zone_approach_cross_map=PASS near_zone_stale_leg=PASS work_timeout_ring=PASS "
        "combat_keeps_window=PASS\n";
}
