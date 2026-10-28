// twow-repo#601 S1 "latch semantics": failure classes, taker pre-validation, grind yield, resets
// (QuestFailureClassPolicy.h) and the structural park of QuestTurnInParkPolicy.h.
// Hand-rolled Require(), std::exit(1) on failure, no gtest, no game library.
#include "QuestFailureClassPolicy.h"
#include "QuestTurnInParkPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::uint32_t ParkMs = 3600 * 1000;
}

int main()
{
    using namespace ai::quest_failure_class;
    using ai::quest_search::RejectCounts;

    // 0) Switch off: no class, the rejection counts as before.
    {
        RejectCounts rejects;
        rejects.crossMap = 3;
        Decision const off = Classify(false, rejects, 1, 1);
        Require(off.failureClass == FailureClass::None, "switch off: no class");
        Require(!off.parkTakerless && !off.parkAllOffered, "switch off: nothing parked");
        Require(CountsTowardGrind(off.failureClass), "switch off: the failure counts as before");
        Require(std::strcmp(ClassName(FailureClass::None), "none") == 0, "name of no class");
    }

    // 1) Every RejectCounts reason is in exactly one class sum (a new reason must be classified on purpose).
    {
        // A 17th reason field changes the size: classify it in QuestFailureClassPolicy.h and count it below. The two diagnostic
        // splits (notActiveReason, noMarkWhy) are no reasons and sit in the size apart.
        static_assert(sizeof(RejectCounts) == (16 + RejectCounts::InactiveReasons + ai::quest_search::NoMarkReasons) * sizeof(std::uint32_t),
            "classify the new reject reason (StructuralRejects / TransientRejects / OtherRejects)");
        RejectCounts all;
        all.turnInSuppressed = 1; all.deathSuppressed = 1; all.otherMapGather = 1; all.knownInactive = 1;
        all.hubFilter = 1; all.notActive = 1; all.enemyZone = 1; all.crossMap = 1; all.zoneLevel = 1;
        all.dangerMap = 1; all.rangeSkip = 1; all.movedAway = 1; all.resumeSkipped = 1;
        all.questLoopSuppressed = 1; all.battlegroundQuest = 1; all.inactiveReselect = 1;
        Require(StructuralRejects(all) == 2, "structural: cross_map, zone_level");
        Require(TransientRejects(all) == 8, "transient: turn-in/death/quest-loop suppression, hub, range skip, moved away, resume skip, emptied camp");
        Require(OtherRejects(all) == 6, "other: other-map gather, known inactive, not active, enemy zone, danger map, battleground");
        Require(StructuralRejects(all) + TransientRejects(all) + OtherRejects(all) == 16, "16 reasons, each counted once");
        // The P3-0 split of not_active is no extra reason: it is part of notActive.
        RejectCounts split;
        split.notActiveReason[RejectCounts::InactiveNoMark] = 5;
        Require(OtherRejects(split) == 0 && TransientRejects(split) == 0, "the not_active split adds nothing");
        // twow-repo#601 step 1b: the no_mark split (NoMarkOf) is a split of notActiveReason[InactiveNoMark] - no reason either.
        RejectCounts noMark;
        noMark.noMarkWhy[ai::quest_search::NoMarkOther] = 4;
        Require(StructuralRejects(noMark) == 0 && TransientRejects(noMark) == 0 && OtherRejects(noMark) == 0, "the no_mark split adds nothing");
    }

    // 2) Structural: a turn-in-only request whose takers were all deferred - every offered turn-in is parked.
    {
        RejectCounts crossOnly;
        crossOnly.crossMap = 2;
        Decision const d = Classify(true, crossOnly, 1, 0);
        Require(d.failureClass == FailureClass::Structural, "cross_map only, turn-in-only: structural");
        Require(d.parkAllOffered && !d.parkTakerless, "cross_map only: park the offered turn-in");
        Require(std::strcmp(d.reason, "structural_cross_map") == 0, "reason cross map");
        Require(d.abandonable, "cross map only: may end in an abandon");
        Require(CountsTowardGrind(d.failureClass), "structural counts here; the park resets the counter (fail closed)");

        RejectCounts zoneOnly;
        zoneOnly.zoneLevel = 1;
        Require(std::strcmp(Classify(true, zoneOnly, 2, 0).reason, "structural_zone_level") == 0, "reason zone level");
        Require(!Classify(true, zoneOnly, 2, 0).abandonable, "zone level: never abandoned");
        RejectCounts both;
        both.zoneLevel = 1;
        both.crossMap = 1;
        Require(std::strcmp(Classify(true, both, 2, 0).reason, "structural_route") == 0, "reason mixed route danger");
        Require(!Classify(true, both, 2, 0).abandonable, "mixed with zone level: never abandoned");
        Require(!Classify(true, RejectCounts(), 1, 1).abandonable, "takerless only: the caller decides (no destination)");
    }

    // 3) Structural: no taker point for an offered turn-in (no spawn, script-only GO, excluded zone): the global
    //    quest giver fallback ran and gave no_mark rejections - the takerless quest is parked exactly.
    {
        RejectCounts fallback;
        fallback.notActive = 12;
        Decision const d = Classify(true, fallback, 1, 1);
        Require(d.failureClass == FailureClass::Structural, "takerless: structural despite the giver no_mark rejects");
        Require(d.parkTakerless && !d.parkAllOffered, "takerless: only the takerless quest is parked");
        Require(Classify(true, fallback, 1, 5).parkTakerless, "takerless is clamped to the offered count");
    }

    // 4) Transient wins: something lifts by itself - backoff only, no count, no park on a guess.
    {
        char const* const names[] = { "turn-in suppression", "death suppression", "quest loop", "hub filter", "range skip",
            "moved away", "resume skip" };
        for (std::uint32_t i = 0; i < 7; ++i)
        {
            RejectCounts rejects;
            rejects.crossMap = 1;
            switch (i)
            {
                case 0: rejects.turnInSuppressed = 1; break;
                case 1: rejects.deathSuppressed = 1; break;
                case 2: rejects.questLoopSuppressed = 1; break;
                case 3: rejects.hubFilter = 1; break;
                case 4: rejects.rangeSkip = 1; break;
                case 5: rejects.movedAway = 1; break;
                default: rejects.resumeSkipped = 1; break;
            }
            Decision const d = Classify(true, rejects, 1, 0);
            if (d.failureClass != FailureClass::Transient || d.parkAllOffered || CountsTowardGrind(d.failureClass))
            {
                std::cerr << "FAILED: transient " << names[i] << '\n';
                return 1;
            }
        }
        // A takerless turn-in is parked even next to a transient reason (exact attribution), the class stays transient.
        RejectCounts mixed;
        mixed.deathSuppressed = 1;
        Decision const d = Classify(true, mixed, 2, 1);
        Require(d.failureClass == FailureClass::Transient && d.parkTakerless && !d.parkAllOffered,
            "transient + takerless: park the takerless one only");
        // Transient on a giver/objective request (QL-1 suppressions) does not count either.
        RejectCounts loop;
        loop.questLoopSuppressed = 4;
        loop.notActive = 2;
        Require(Classify(true, loop, 0, 0).failureClass == FailureClass::Transient, "quest loop on an objective request: transient");
    }

    // 5) Empty offer: today's behaviour (counts) until S5.
    {
        RejectCounts noMark;
        noMark.notActive = 3;
        Decision const d = Classify(true, noMark, 0, 0);
        Require(d.failureClass == FailureClass::EmptyOffer && CountsTowardGrind(d.failureClass), "no giver mark: empty offer, counts");
        Require(Classify(true, RejectCounts(), 0, 0).failureClass == FailureClass::EmptyOffer, "an empty list: empty offer");
        // Structural reasons on a giver/objective request: nothing to park - counts as today.
        RejectCounts objectivesAway;
        objectivesAway.crossMap = 4;
        Decision const away = Classify(true, objectivesAway, 0, 0);
        Require(away.failureClass == FailureClass::EmptyOffer && !away.parkAllOffered, "cross_map objectives: no park, counts");
        // Structural mixed with a judged-and-refused taker: no park on a guess.
        RejectCounts mixed;
        mixed.crossMap = 1;
        mixed.notActive = 1;
        Require(Classify(true, mixed, 2, 0).failureClass == FailureClass::EmptyOffer, "cross_map + not_active: empty offer");
        RejectCounts enemy;
        enemy.zoneLevel = 1;
        enemy.enemyZone = 1;
        Require(!Classify(true, enemy, 1, 0).parkAllOffered, "zone_level + enemy zone: no park");
        RejectCounts cluster;
        cluster.dangerMap = 2;
        Require(Classify(true, cluster, 1, 0).failureClass == FailureClass::EmptyOffer, "danger map: today's behaviour");
    }

    // 6) Taker pre-validation: fold, structural, abandonable, taker-only gate.
    {
        Require(FoldTakerCheck(true, true, true, true) == TakerCheck::Routable, "one routable point validates");
        Require(FoldTakerCheck(false, true, true, false) == TakerCheck::NotNow, "a suppressed point: not now, no park");
        Require(FoldTakerCheck(false, false, true, false) == TakerCheck::CrossMap, "only cross-map points: cross map");
        Require(FoldTakerCheck(false, false, true, true) == TakerCheck::ZoneLevel, "cross map + zone level: zone level (not abandoned)");
        Require(FoldTakerCheck(false, false, false, false) == TakerCheck::NoDestination, "no point at all: no destination");
        Require(IsStructural(TakerCheck::NoDestination) && IsStructural(TakerCheck::CrossMap) && IsStructural(TakerCheck::ZoneLevel),
            "structural checks park");
        Require(!IsStructural(TakerCheck::Routable) && !IsStructural(TakerCheck::NotNow), "routable / not now never park");
        Require(AbandonableReason(TakerCheck::NoDestination) && AbandonableReason(TakerCheck::CrossMap), "abandonable: no spawn, other continent");
        Require(!AbandonableReason(TakerCheck::ZoneLevel), "a zone above the bot heals with levels: never abandoned");
        Require(!AbandonableReason(TakerCheck::NotNow) && !AbandonableReason(TakerCheck::Routable), "not structural: never abandoned");
        Require(std::strcmp(TakerCheckName(TakerCheck::CrossMap), "structural_cross_map") == 0, "park reason name");
        Require(TakerOnlyOffer(false, false), "switch off: every taker fetch kept");
        Require(TakerOnlyOffer(true, true), "validated taker: taker-only");
        Require(!TakerOnlyOffer(true, false), "not validated: no taker-only, the normal fetch runs");
        Require(AbandonParkedTurnIn(true, true, false), "switch on, second structural park: abandon");
        Require(!AbandonParkedTurnIn(false, true, false), "switch off: never abandon");
        Require(!AbandonParkedTurnIn(true, false, false), "first park: keep");
        Require(!AbandonParkedTurnIn(true, true, true), "class quest (or unknown): keep");
    }

    // 6b) TransientCountsAfter: 0 = a transient rejection never counts (OB-10); N = the N-th in a row counts once.
    {
        Require(!TransientStreakCounts(1, 0) && !TransientStreakCounts(50, 0), "key 0: never counted (OB-10's rule)");
        Require(!TransientStreakCounts(0, 3), "no transient rejection: nothing extra");
        Require(!TransientStreakCounts(2, 3) && TransientStreakCounts(3, 3), "the 3rd in a row counts");
        Require(TransientStreakCounts(1, 1), "N = 1: every transient rejection counts");
        Require(std::strcmp(TransientStreakValue, "quest transient streak") == 0, "streak value name");
    }

    // 7) Grind yields to routable open turn-ins (8.5 generalised); id lists.
    {
        Require(GrindYieldsToTurnIn(true, 1, false), "a routable open turn-in, backoff over: no grind");
        Require(!GrindYieldsToTurnIn(true, 0, false), "none: grind as before");
        Require(!GrindYieldsToTurnIn(false, 3, false), "a bot led by a player: unchanged");
        Require(!GrindYieldsToTurnIn(true, 2, true), "backoff still running: grind on (no quest request can run yet)");
        std::string list;
        ListAdd(list, 310);
        ListAdd(list, 3100);
        ListAdd(list, 310);
        Require(list == "310,3100", "list without duplicates");
        Require(ListHas(list, 310) && ListHas(list, 3100), "both ids found");
        Require(!ListHas(list, 31) && !ListHas(list, 10) && !ListHas(list, 100), "no partial match");
        Require(!ListHas(list, 0) && !ListHas(std::string(), 310), "id 0 and the empty list");
        std::string single = "55002";
        Require(ListHas(single, 55002) && !ListHas(single, 5500), "single id");
    }

    // 8) Pre-loop: no range skip while the hub filter holds the bot.
    {
        Require(RangeSkipAllowed(false, true) && RangeSkipAllowed(false, false), "switch off: the 10 % skip stays");
        Require(!RangeSkipAllowed(true, true), "hub filter on: no range skip");
        Require(RangeSkipAllowed(true, false), "no hub filter: skip as before");
    }

    // 9) Resets: events, level-up, XP keeps the idle clock but no longer resets the stage.
    {
        Require(ResetEventNames(ResetRescue | ResetBgReturn) == "rescue,bg_return", "event names");
        Require(ResetEventNames(ResetLevelUp | ResetLogin) == "level_up,login", "event names 2");
        Require(ResetEventNames(0) == "none", "no event");
        Require((ResetRescue & ResetLevelUp) == 0 && (ResetLogin & ResetBgReturn) == 0 && (ResetRescue & ResetLogin) == 0,
            "distinct event bits");
        Require(LevelUp(14, 15) && !LevelUp(15, 15) && !LevelUp(0, 15) && !LevelUp(15, 14), "level-up only upwards, not on the first check");
        Require(ProgressResetsStage(false, false), "switch off: XP resets the stage as before");
        Require(!ProgressResetsStage(true, false), "switch on: XP alone keeps the stage");
        Require(ProgressResetsStage(true, true), "switch on: a level-up resets the stage");
        // Plan 4.1: a quest-state change resets the stage only; the first check of a login is no change.
        Require(ResetEventNames(ResetQuestState) == "quest_state", "quest-state event name");
        Require(StageOnlyReset(ResetQuestState), "quest state alone: stage only");
        Require(!StageOnlyReset(ResetQuestState | ResetLevelUp) && !StageOnlyReset(ResetRescue), "any other event: full reset");
        Require((ResetQuestState & (ResetRescue | ResetLevelUp | ResetLogin | ResetBgReturn)) == 0, "distinct quest-state bit");
        // twow-repo#601 S5a (BandLadder): the band step bit is its own, named, and a full reset.
        Require(ResetEventNames(ResetBandStep) == "band_step", "band step event name");
        Require((ResetBandStep & (ResetRescue | ResetLevelUp | ResetLogin | ResetBgReturn | ResetQuestState)) == 0,
            "distinct band step bit");
        Require(!StageOnlyReset(ResetBandStep), "band step: full reset");
        std::uint32_t const logA = MixQuestState(MixQuestState(QuestStateSeed, 310, false), 3100, false);
        std::uint32_t const logB = MixQuestState(MixQuestState(QuestStateSeed, 310, true), 3100, false);
        std::uint32_t const logC = MixQuestState(QuestStateSeed, 310, false);
        Require(logA != logB, "a quest finished: changed");
        Require(logA != logC, "a quest handed in or dropped: changed");
        Require(logA == MixQuestState(MixQuestState(QuestStateSeed, 310, false), 3100, false), "same log: same state");
        Require(QuestStateSeed != 0 && logA != 0, "a state is never 0");
        Require(!QuestStateChanged(0, logA), "first check of a login: no change");
        Require(QuestStateChanged(logA, logB) && !QuestStateChanged(logA, logA), "change only on a difference");
        // The idle clock is not part of this decision: ProgressTracker::Update moves lastChange on any progress.
        ai::quest_search::ProgressTracker progress;
        Require(progress.Update(100, 1000), "first sample");
        Require(progress.Update(101, 1060) && progress.IdleSeconds(1060) == 0, "XP gain: progress for the idle clock");
    }

    // 10) Structural park at once (QuestTurnInParkPolicy.h ParkNow) and "parked twice -> abandon".
    {
        using namespace ai::turnin_park;
        Book book;
        ParkResult first = ParkNow(book, 42055, 1000, ParkMs, true);
        Require(first.parked && !first.abandonDue, "first structural park: parked, not abandoned");
        Require(IsParked(book, 42055, 1001), "parked at once (no counting)");
        Require(!ParkNow(book, 42055, 2000, ParkMs, true).parked, "no second park while parked");
        ParkResult second = ParkNow(book, 42055, 1000 + ParkMs, ParkMs, true);
        Require(second.parked && second.abandonDue, "second structural park in a row: abandon due");
        Require(IsParked(book, 42055, 1000 + ParkMs + 1), "the second park holds too");
        ParkResult third = ParkNow(book, 42055, 1000 + 2 * ParkMs, ParkMs, true);
        Require(third.parked && !third.abandonDue, "after an abandon report the series starts anew");

        // Not abandonable (zone level): never abandoned, and it ends a series.
        Book zone;
        Require(!ParkNow(zone, 55002, 1000, ParkMs, false).abandonDue, "zone level: no abandon");
        Require(!ParkNow(zone, 55002, 1000 + ParkMs, ParkMs, false).abandonDue, "zone level twice: no abandon");
        Book series;
        ParkNow(series, 310, 1000, ParkMs, true);
        ParkNow(series, 310, 1000 + ParkMs, ParkMs, false);
        Require(!ParkNow(series, 310, 1000 + 2 * ParkMs, ParkMs, true).abandonDue, "a non-structural park ends the series");

        // A counted #485 park (RecordFailure) ends the series as well.
        Book counted;
        ParkNow(counted, 931, 1000, ParkMs, true);
        Require(RecordFailure(counted, 931, 1000 + ParkMs, 1, ParkMs, ParkMs), "a counted park");
        Require(!ParkNow(counted, 931, 1000 + 2 * ParkMs, ParkMs, true).abandonDue, "the counted park ended the series");

        Require(!ParkNow(book, 0, 1000, ParkMs, true).parked, "quest 0 is never parked");
        Require(!ParkNow(book, 77, 1000, 0, true).parked, "a park of 0 ms never parks");
        Require(AbandonAfterStructuralParks == 2, "parked twice -> abandon");
    }

    // 11) An abandoned quest is marked and not taken again (bounded ring, quest 0 never marked).
    {
        using namespace ai::turnin_park;
        AbandonMarks marks;
        Require(!IsAbandonMarked(marks, 42055), "nothing marked at first");
        MarkAbandoned(marks, 42055);
        Require(IsAbandonMarked(marks, 42055), "abandoned quest marked");
        MarkAbandoned(marks, 42055);
        Require(marks.next == 1, "marked once");
        MarkAbandoned(marks, 0);
        Require(!IsAbandonMarked(marks, 0) && marks.next == 1, "quest 0 is never marked");
        for (std::uint32_t quest = 1; quest < AbandonMarkSize; ++quest)
            MarkAbandoned(marks, 1000 + quest);
        Require(IsAbandonMarked(marks, 42055) && IsAbandonMarked(marks, 1000 + AbandonMarkSize - 1), "a full ring keeps every mark");
        MarkAbandoned(marks, 5000);
        Require(IsAbandonMarked(marks, 5000) && !IsAbandonMarked(marks, 42055), "bounded: the oldest mark gives way");
        MarkAbandoned(marks, 5001);
        Require(IsAbandonMarked(marks, 5000) && IsAbandonMarked(marks, 5001) && !IsAbandonMarked(marks, 1001),
            "the ring goes on in order (the next oldest gives way, not the newest)");
    }

    // twow-repo#627 S7 hook (FailureClasses.GroupNeeded): only an empty offer, only with the key, only when every open quest waits.
    {
        Require(WithGroupNeeded(FailureClass::EmptyOffer, true, 2, 0) == FailureClass::GroupNeeded, "every open quest queued: group_needed");
        Require(WithGroupNeeded(FailureClass::EmptyOffer, false, 2, 0) == FailureClass::EmptyOffer, "key off: as Classify said");
        Require(WithGroupNeeded(FailureClass::EmptyOffer, true, 2, 1) == FailureClass::EmptyOffer, "another open quest: empty offer");
        Require(WithGroupNeeded(FailureClass::EmptyOffer, true, 0, 0) == FailureClass::EmptyOffer, "nothing queued: empty offer");
        Require(WithGroupNeeded(FailureClass::Transient, true, 2, 0) == FailureClass::Transient, "transient stays transient");
        Require(WithGroupNeeded(FailureClass::Structural, true, 2, 0) == FailureClass::Structural, "structural stays (its park)");
        Require(WithGroupNeeded(FailureClass::None, true, 2, 0) == FailureClass::None, "no class stays none");
        Require(!CountsTowardGrind(FailureClass::GroupNeeded) && CountsTowardGrind(FailureClass::EmptyOffer), "group_needed does not count");
        Require(std::strcmp(ClassName(FailureClass::GroupNeeded), "group_needed") == 0, "name");
    }

    std::cout << "quest_failure_class_policy_tests passed\n";
    return 0;
}
