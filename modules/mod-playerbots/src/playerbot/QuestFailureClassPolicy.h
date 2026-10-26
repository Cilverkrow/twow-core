#pragma once

#include "QuestSearchPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::quest_failure_class
{
// twow-repo#601 S1 "latch semantics" (OB-00 17.10.2026, OB-10's answers 17.10.2026). Pure rules, no game types.
//
// Soak 17.10. (540 bots, 11.9 h): 228 of 265 L10+ bots sat in the grind fallback for ~10 h. One empty quest route
// search counted like any other: a turn-in on the other continent, a taker without a spawn, a death cooldown that
// lifts by itself. Two of them made NeedTravelPurpose(Grind) true for good (GrindFallbackFailures 2 < #485
// TurnInParkFailures 3, so the park that resets the counter was never reached); 209 of 268 L10+ latch entries were
// turn-in-only requests whose taker could not be routed.
//
// AiPlayerbot.QuestFirstProgression.FailureClasses = 1 sorts every no_active_route_candidate rejection by the reject
// counts already written with [QuestFirstRoute] state=rejected:
//   Structural  - the offered turn-in cannot be reached at this level: another continent below
//                 MinLevelForCrossMapQuestRoute (cross_map), a zone clearly above the bot (zone_level), or a taker the
//                 destination job found no point for at all (no spawn, script-only game object, excluded start zone,
//                 instance). The turn-in is parked at once for TurnInParkSeconds (#485 park) - the park resets the
//                 grind-fallback counter as every #485 park does, so the failure never counts toward grinding.
//   Transient   - a filter that lifts by itself took a candidate: turn-in route / death / quest-loop suppression, the
//                 local quest hub filter, a random range skip, a stale or resumed list. Backoff only, no count.
//   EmptyOffer  - everything else (no giver mark, nothing active, enemy zone, danger map, battleground quest, mixed
//                 structural and other reasons): today's behaviour - it counts - until S5 (band step).
//   GroupNeeded - (twow-repo#627 S7 hook, own key) an empty offer while every open quest waits for S7 help: no count, no park.
// 0 = no class (None): the rejection counts exactly as before.
enum class FailureClass : std::uint8_t
{
    None,
    Structural,
    Transient,
    EmptyOffer,
    GroupNeeded,    // twow-repo#627 S7 hook (FailureClasses.GroupNeeded): an empty offer while every open quest waits for help
};

inline char const* ClassName(FailureClass failureClass)
{
    switch (failureClass)
    {
        case FailureClass::Structural: return "structural";
        case FailureClass::Transient: return "transient";
        case FailureClass::EmptyOffer: return "empty_offer";
        case FailureClass::GroupNeeded: return "group_needed";
        case FailureClass::None: break;
    }
    return "none";
}

// The reject counts by class. Every RejectCounts reason is in exactly one of the three sums (the unit test pins it).
inline std::uint32_t StructuralRejects(quest_search::RejectCounts const& rejects)
{
    return rejects.crossMap + rejects.zoneLevel;
}

inline std::uint32_t TransientRejects(quest_search::RejectCounts const& rejects)
{
    return rejects.turnInSuppressed + rejects.deathSuppressed + rejects.questLoopSuppressed + rejects.hubFilter +
        rejects.rangeSkip + rejects.movedAway + rejects.resumeSkipped + rejects.inactiveReselect;
}

inline std::uint32_t OtherRejects(quest_search::RejectCounts const& rejects)
{
    return rejects.otherMapGather + rejects.knownInactive + rejects.notActive + rejects.enemyZone + rejects.dangerMap +
        rejects.battlegroundQuest;
}

struct Decision
{
    FailureClass failureClass = FailureClass::None;
    // Park the offered turn-ins whose taker had no point in the job's list (exact: no candidate of that quest at all).
    bool parkTakerless = false;
    // Every reject was structural: park every offered turn-in (a turn-in-only request offers only takers).
    bool parkAllOffered = false;
    // The park reason for the offered turn-ins that had a taker point (parkAllOffered).
    char const* reason = "";
    // Whether that reason may end in an abandon (AbandonStructuralParked): cross_map only, never a zone level.
    bool abandonable = false;
};

// offeredTurnIns: the bot's own open turn-ins a turn-in-only request offered takers for (0: the request was not
// turn-in-only - nothing can be parked, so it is never Structural). takerless: how many of them had no taker point in
// the destination list. A transient reason wins: something lifts by itself, so no turn-in is parked on a guess - but a
// takerless one is parked anyway, its attribution is exact.
inline Decision Classify(bool enabled, quest_search::RejectCounts const& rejects, std::uint32_t offeredTurnIns,
    std::uint32_t takerless)
{
    Decision decision;
    if (!enabled)
        return decision;

    if (takerless > offeredTurnIns)
        takerless = offeredTurnIns;
    std::uint32_t const structural = StructuralRejects(rejects);
    std::uint32_t const transient = TransientRejects(rejects);
    std::uint32_t const other = OtherRejects(rejects);

    decision.parkTakerless = takerless > 0;
    decision.parkAllOffered = offeredTurnIns > 0 && !transient && !other && structural > 0;
    if (decision.parkAllOffered)
        decision.reason = !rejects.zoneLevel ? "structural_cross_map" : !rejects.crossMap ? "structural_zone_level" :
            "structural_route";
    decision.abandonable = decision.parkAllOffered && !rejects.zoneLevel;

    if (transient)
        decision.failureClass = FailureClass::Transient;
    else if (decision.parkTakerless || decision.parkAllOffered)
        decision.failureClass = FailureClass::Structural;
    else
        decision.failureClass = FailureClass::EmptyOffer;
    return decision;
}

// Whether the rejection adds one to "quest route failures" (the grind-fallback counter). A transient one does not; a
// structural one counts here and the park that follows resets the counter (as every #485 park) - if nothing could be
// parked (parking off, the quest no longer finished) it stays counted: fail closed to today's behaviour.
inline bool CountsTowardGrind(FailureClass failureClass)
{
    return failureClass != FailureClass::Transient && failureClass != FailureClass::GroupNeeded;
}

// twow-repo#627 S7 hook (AiPlayerbot.QuestFirstProgression.FailureClasses.GroupNeeded, OB-10 24.10.2026): an empty offer
// while the bot holds open quests and every one of them is a group quest waiting for help in the S7 queue (queue age > 0)
// is group_needed - not the bot's failure: it does not count toward the grind fallback (like transient) and parks nothing
// (it was no structural class). The backoff and the band streak (F1 / F4, any class) run as before, so the bot steps on
// after N searches and takes the quests along (BandQueuedFreeMinutes). Off: the class stays as Classify said.
inline FailureClass WithGroupNeeded(FailureClass failureClass, bool enabled, std::uint32_t queuedGroupQuests,
    std::uint32_t otherOpenQuests)
{
    return enabled && failureClass == FailureClass::EmptyOffer && queuedGroupQuests && !otherOpenQuests ?
        FailureClass::GroupNeeded : failureClass;
}

// AiPlayerbot.QuestFirstProgression.TransientCountsAfter (S1 review; the value is OB-10's decision): 0 = a transient
// rejection never counts (OB-10 17.10.). N > 0: the N-th transient rejection in a row (no other class and no route found
// in between) counts once toward the grind fallback and the streak starts anew - a transient cause that persists
// (QL-1 cooldown re-armed, death suppression, a hub filter a later check rejects) then reaches the grind fallback
// instead of idling until the quest rescue. streak: this rejection included.
inline bool TransientStreakCounts(std::uint32_t streak, std::uint32_t countsAfter)
{
    return countsAfter && streak >= countsAfter;
}

// The "manual int" value with the transient rejections in a row (ChooseTravelTargetAction, FailureClasses only).
inline constexpr char const* TransientStreakValue = "quest transient streak";

// ---- taker pre-validation (OB-10 answer 3) -----------------------------------------------------------------------

// The taker check of one finished quest at request time (RequestQuestTravelTargetAction), before the fetch is built.
enum class TakerCheck : std::uint8_t
{
    Routable,       // a taker point that is reachable, not deferred, not suppressed, active: taker-only may run
    NoDestination,  // no taker destination at all (no spawn, script-only game object, excluded zone, no path)
    CrossMap,       // every taker point is on the other continent below MinLevelForCrossMapQuestRoute
    ZoneLevel,      // every taker point is in a zone clearly above the bot (or mixed with cross map)
    NotNow,         // suppressed, enemy zone or inactive: not validated, not parked
};

inline char const* TakerCheckName(TakerCheck check)
{
    switch (check)
    {
        case TakerCheck::Routable: return "routable";
        case TakerCheck::NoDestination: return "structural_no_destination";
        case TakerCheck::CrossMap: return "structural_cross_map";
        case TakerCheck::ZoneLevel: return "structural_zone_level";
        case TakerCheck::NotNow: break;
    }
    return "not_now";
}

// Folds the per-point answers of one quest: any routable point validates; a point turned down for a reason that lifts
// by itself keeps the quest unparked; only structural answers (or no point at all) park it.
inline TakerCheck FoldTakerCheck(bool anyRoutable, bool anyNotNow, bool anyCrossMap, bool anyZoneLevel)
{
    if (anyRoutable)
        return TakerCheck::Routable;
    if (anyNotNow)
        return TakerCheck::NotNow;
    if (anyZoneLevel)
        return TakerCheck::ZoneLevel;
    if (anyCrossMap)
        return TakerCheck::CrossMap;
    return TakerCheck::NoDestination;
}

inline bool IsStructural(TakerCheck check)
{
    return check == TakerCheck::NoDestination || check == TakerCheck::CrossMap || check == TakerCheck::ZoneLevel;
}

// "Parked twice -> abandon" only for a reason that no band step heals by itself: no destination, another continent.
// A zone above the bot heals with levels - kept.
inline bool AbandonableReason(TakerCheck check)
{
    return check == TakerCheck::NoDestination || check == TakerCheck::CrossMap;
}

// AiPlayerbot.QuestFirstProgression.AbandonStructuralParked: abandon the quest when the park policy reports the
// second abandonable structural park in a row (turnin_park::ParkNow abandonDue). A class quest (or an unknown one) is
// kept: fail closed. enabled false = never abandon.
inline bool AbandonParkedTurnIn(bool enabled, bool abandonDue, bool keepQuest)
{
    return enabled && abandonDue && !keepQuest;
}

// A turn-in-only request (takers only, no giver or objective fetch) only for a pre-validated taker. enabled false =
// every taker fetch is kept, as before.
inline bool TakerOnlyOffer(bool enabled, bool validated)
{
    return !enabled || validated;
}

// The 8.5 gather rule (QuestSearchPolicy.h GatherYieldsToTurnIn) generalised to grind: a quest-first roster bot on its
// own does not grind while a routable open turn-in waits (validated at the last request, or finished since and not yet
// checked - the next request checks it) - but only once the quest route backoff ran out: until then no quest request
// can run, and a bot that stopped grinding would stand idle (up to 10 min at counter >= 2).
inline bool GrindYieldsToTurnIn(bool rosterOnItsOwn, std::uint32_t routableOpenTurnIns, bool routeBackoffRunning)
{
    return rosterOnItsOwn && routableOpenTurnIns > 0 && !routeBackoffRunning;
}

// Comma-separated quest id lists in a "manual string" value ("quest turnin checked" / "quest turnin validated").
inline bool ListHas(std::string const& list, std::uint32_t questId)
{
    if (!questId)
        return false;
    std::string const id = std::to_string(questId);
    std::size_t at = 0;
    while ((at = list.find(id, at)) != std::string::npos)
    {
        std::size_t const end = at + id.size();
        if ((at == 0 || list[at - 1] == ',') && (end == list.size() || list[end] == ','))
            return true;
        at = end;
    }
    return false;
}

inline void ListAdd(std::string& list, std::uint32_t questId)
{
    if (!questId || ListHas(list, questId))
        return;
    list += (list.empty() ? "" : ",") + std::to_string(questId);
}

// The "manual string" values of the taker pre-validation: the open turn-ins the last request checked, and the ones it
// found routable (written per request by RequestQuestTravelTargetAction, read by the grind yield in TravelValues).
inline constexpr char const* CheckedTurnInsValue = "quest turnin checked";
inline constexpr char const* ValidatedTurnInsValue = "quest turnin validated";

// The local quest hub filter: no random range skip while it holds the bot (a skip past the one eligible local
// candidate leaves only hub-filtered partitions). enabled false = the old 10 % skip.
inline bool RangeSkipAllowed(bool enabled, bool hasActiveLocalQuestHub)
{
    return !enabled || !hasActiveLocalQuestHub;
}

// ---- resets (OB-10 answer 2, AiPlayerbot.QuestFirstProgression.ResetOnProgressEvents) ----------------------------

// Events that reset "quest route failures", "quest route backoff until" and "quest search stage" (a bit mask).
constexpr std::uint32_t ResetRescue = 1u << 0;      // a rescue teleport (RandomPlayerbotMgr::ProcessQuestRescues)
constexpr std::uint32_t ResetLevelUp = 1u << 1;     // the progress watch saw a higher level
constexpr std::uint32_t ResetLogin = 1u << 2;       // the first progress check of a new PlayerbotAI (a login)
constexpr std::uint32_t ResetBgReturn = 1u << 3;    // PlayerbotAI::OnReturnFromBattleground (#517)
// Plan 4.1 (S1 review): a quest-state change (a quest taken, finished, handed in or dropped) resets the search stage only
// - the counter and the backoff stay (a found route resets them anyway).
constexpr std::uint32_t ResetQuestState = 1u << 4;
// twow-repo#601 S5a (AiPlayerbot.QuestFirstProgression.BandLadder only): a band step, a band arrival and a band rescue - the
// counter, the backoff, the transient streak and the stage go to 0 even with ResetOnProgressEvents = 0 (QuestBand.cpp).
constexpr std::uint32_t ResetBandStep = 1u << 5;
constexpr std::uint32_t ResetEventCount = 6;

inline std::string ResetEventNames(std::uint32_t events)
{
    static char const* const names[ResetEventCount] = { "rescue", "level_up", "login", "bg_return", "quest_state",
        "band_step" };
    std::string out;
    for (std::uint32_t bit = 0; bit < ResetEventCount; ++bit)
        if (events & (1u << bit))
            out += std::string(out.empty() ? "" : ",") + names[bit];
    return out.empty() ? std::string("none") : out;
}

inline bool LevelUp(std::uint32_t lastLevel, std::uint32_t level)
{
    return lastLevel && level > lastLevel;
}

// Only a quest-state change: the stage alone is reset. Any other event in the mask resets the counter and the backoff too.
inline bool StageOnlyReset(std::uint32_t events)
{
    return events == ResetQuestState;
}

// The quest log's state for the progress watch: every quest slot's quest id and whether it is finished, folded in slot
// order (bounded by the quest log). Never 0, so 0 means "no check yet" (this login).
constexpr std::uint32_t QuestStateSeed = 2166136261u;

inline std::uint32_t MixQuestState(std::uint32_t state, std::uint32_t questId, bool complete)
{
    state = (state ^ questId) * 16777619u;
    state = (state ^ (complete ? 1u : 2u)) * 16777619u;
    return state ? state : 1u;
}

// A change between two checks; the first check of a login (lastState 0) is no change.
inline bool QuestStateChanged(std::uint32_t lastState, std::uint32_t state)
{
    return lastState && state != lastState;
}

// The progress watch (PlayerbotAI.cpp, #421 C): any progress (level, XP, a gathering skill under a purpose) restarts
// the idle clock as before. With the switch only a level-up still resets the quest search stage - XP alone does not
// (a grinding bot gains XP every minute and searched only its own zone again after each grind phase).
inline bool ProgressResetsStage(bool resetOnProgressEvents, bool levelUp)
{
    return !resetOnProgressEvents || levelUp;
}
}
