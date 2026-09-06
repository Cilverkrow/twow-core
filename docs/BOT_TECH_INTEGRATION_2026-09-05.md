# CMaNGOS bot technology integration — September 5, 2026

User-directed integration into the current Turtle working tree, above b6be2a57.
Reference: playerbots-mantech-integration 811d6f1e, cross-checked against the
TBC/Wrath bot trees eb90e45d and their native core taxi implementations.
Existing uncommitted system-audit changes are preserved. No new GitHub commit
or push is implied by this document.

## Changes

1. Adapted the CMaNGOS per-engine failed-execution retry cache, exponential
   delay, size/TTL limits, expiry/eviction and existing telemetry. This is NOT
   the old blanket eligibility cache: usefulness, prerequisites and isPossible
   run first; impossible actions are never cached. Combat, reactions, urgent
   actions, human-led bots and explicit commands are excluded. Position,
   health/power/money changes and reset/transition boundaries invalidate work.
   Zero in either retry setting disables it. Default background retry ceiling
   is two seconds; internal conditions not represented by those invalidations
   can still wait until that deadline. No claim of instantaneous background
   recovery under every condition is made.
2. Unified purposeful and ambient RPG taxi activation through the existing
   MovementAction::UseTaxi adapter. It respects both native endpoint masks,
   faction mount availability and matching flight-master interaction. Only the
   source can be discovered legitimately; unknown destinations are rejected
   before native activation. No anticheat bypass or global taxi cheat enabled.
3. MinimalMove retains a rejected flight leg instead of cutting its route and
   reporting success. Existing nextTeleport timing bounds subsequent attempts.
4. RPG taxi temporary funding is restored on failure as well as success; the
   existing free ambient-flight policy remains. No permanent money grant.
5. Corrected wandermaz to wandermax, restoring the medium-range upper bound.
6. Ported CMaNGOS crowd-control target range allowance and its wait-for-attack
   exception for non-threatening actions; preserved Turtle's native movement,
   spell handling, druid strategies and role logic.
7. Restored CMaNGOS default dungeon strategy wiring to combat/noncombat/dead/
   reaction factories. Existing registered strategies handle instance entry
   and exit. No new boss-update loop or fabricated custom-encounter behavior.
8. Ported configurable creature avoidance: command, stored value, creature-ID
   lookup, proximity trigger, strategy and action registration. The movement
   action delegates to Turtle's existing MoveAwayFromCreature implementation.
   Did not import the reference's redundant multiplier returning a constant 1,
   nor its second movement helper that ignores its creature-entry argument.
   Commands: `avoid creature ?`, `avoid creature <entry>`,
   `avoid creature -<entry>`, `avoid creature reset`, using existing bot-command
   authorization and persistence. An empty list performs no creature scans.
9. Staged EnableGreet=0 to match Classic/TBC ambient behavior. The rest of the
   production bot configuration, including 6,000 min/max and account settings,
   is preserved. Wrath has greetings enabled; this is a deliberate profile
   choice, not a claim of different greeting algorithms.

## Kept and consolidated

Kept Turtle's strategy-set no-op detection, deferred engine resets, map and bot
transition generations, native taxi/transport and environmental-hazard logic,
3D loot validation/stale-corpse handling, custom druid/role and battleground
behavior. Existing host hooks and scheduling still own execution. No second AI
engine, replacement movement simulator, or additional spell-capability cache.
No bot population reduction. No unrelated CMaNGOS expansion content imported.

## Validation and limits

Full Windows Release mangosd build succeeded (4 compiler workers).
24/24 architecture tests passed in Release and 24/24 in the ASAN test build.
New tests extract native retry/cache policy and taxi/failed-route functions;
they cover bounds, expiry, time wrap, context recovery, player/combat exclusions,
known/unknown endpoints, both factions, discovery failure, activation failure,
fund rollback, retained route legs and the existing spell-click fallback.
Source contract checks enforce eligibility-before-retry and no explicit-command
retry gating. These use mocks at native service boundaries, not a live realm.

Live 6,000-bot timing, all dungeon/raid tactics, custom encounters and every
class rotation remain unvalidated for this candidate. CMaNGOS-derived features
are not automatically correct for every Turtle content variant. Follow-up
should observe rejected taxis, repeated-action counts, human casting/loot,
formation movement and encounter activation at the unchanged population.

No SQL migration or realmd change is required. Deploy only after mangosd exits;
the user starts the server. Diagnostic controls and overhead are tracked in
doc/TURTLE_DIAGNOSTICS.md. Deployment status/hashes belong to the local package
manifest, not an assumption that a successful build was installed.

## Follow-up: crowded RPG movement, September 5 evening

The deployed taxi corrections stopped new unknown-node warnings in the sampled
boot. This did not establish that every visible direction reversal was fixed.
Comparison reopened the native CMaNGOS RPG choice/approach implementations;
most of this logic is shared, so copying it would also copy these defects.

- Crowd avoidance previously scanned neighbors for each candidate and skipped
  the check entirely at 200 neighbors. GetTargetCounts now makes one local
  tally, then applies the same randomized 5-15 occupancy threshold by lookup.
  Existing safe-player, bot-activity and human-master exemptions remain. It
  is not a global registry, additional scheduler or population reduction.
- RPG approach now uses the point corrected by ClosestCorrectPoint, rather
  than discarding that correction and moving to the original XYZ. Failed
  correction uses the existing ignore/reset path so another target can be
  selected. Missing targets stop the usefulness check immediately.
- Only creature-type Unit targets enter creature patrol-pause handling.
  Player targets previously underwent an invalid static downcast.
- ClosestCorrectPoint leaves coordinates unchanged for a failed/empty/nonfinite
  nav query and returns false when the map query is unavailable. Its other
  consumer, corpse recovery, already checks the failure result. No replacement
  pathfinder or invented navigation point was added.
- RPG target/action weighted shuffles reuse the core random generator instead
  of reinitializing an identical generator from second-resolution time on each
  choice. Weights and native random selection remain. Candidate inspection
  restores its temporary next-action override, including the debug caller.

Full Release mangosd compilation succeeded. BotRpgMovementTest extracts native
code and covers corrected destinations, failed/empty/nonfinite/missing nav
queries, player vs creature targets, stale targets, movement failure, and 1,000
neighbors with the original eligibility exclusions. The 25-test suite passed
the ordinary and ASAN builds. These boundary mocks are not live Southshore
acceptance or proof that every remaining action/rotation is optimal.

The apparent live freeze during this work was accompanied by AFK bots, while
map updates and world telemetry continued. The user reported activity resumed
after switching from the GM observer to a normal mage. The native nearby-player
check excludes invisible GMs. Its idle policy, WHO cooldown, population,
combat activity and all production configuration are unchanged by this follow-up.
No new diagnostic stream/counter was added; no SQL or realmd update is needed.
The candidate is staged separately, not applied to the running world server.
