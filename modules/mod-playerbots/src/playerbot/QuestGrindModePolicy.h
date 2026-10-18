#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::quest_grind_mode
{
// twow-repo#601 GrindMode (owner decision 17.10.2026 via OB-00, strict): "Grinden sollte i.d.R. beim Testen der Quests,
// also bis die Stufe 60 sind, gar nicht greifen, damit wir sehen, ob sie questen - lieber herumstehen und nichts tun
// als grinden, dann faellt es besser auf." Pure rules, no game types, no locks, no config reads.
//
// AiPlayerbot.QuestFirstProgression.GrindMode (restart-only, default 0):
//   0 = today's behaviour (every new branch is skipped).
//   1 = gap filler (S2 / S4, OB-10 rev. 2): the same population as mode 2 grinds only in the gaps of the quest search -
//       while the quest route backoff runs (the last quest search found nothing, any S1 failure class), never through
//       the S0 / 8.32 fallback, the money grind or a refresh renewal, and the grind trip ends with the backoff
//       (GapFiller, GapWindowOpen, GapWindowSeconds below); it grinds on its own map inside the level window
//       [max(gray + 1, L - 5), L + 1] (R3 destinations, R4 plain targets: InGapWindow). Everyone else: exactly like 0.
//   2 = no grind as a substitute activity for quest-first roster bots on their own below the max player level: no
//       8.32 grind fallback, no money grind, no pure grind travel destination, no "attack anything" on a creature that
//       neither attacks the bot / its group nor is needed for an active quest, no kill trip that would end in such a
//       refused kill (skinning / boss purpose, forced guild-order kill trip to a non-quest creature). The bot stands
//       idle and says why ([QuestIdle]). Attacks on attackers (self-defence, group help), quest objective kills,
//       GenericRpg, Explore, mining, herbalism, fishing, skinning own kills and RosterGuild travel stay.
//   Any other value acts like 0.
constexpr std::uint32_t ModeToday = 0;
constexpr std::uint32_t ModeGapFiller = 1;   // S2 / S4: grind only inside the quest route backoff (GapFiller)
constexpr std::uint32_t ModeNoGrind = 2;

// Mode 2 only: no grind as a substitute activity. Mode 1 has its own rules (GapFiller below), never these.
inline bool NoGrind(std::uint32_t grindMode)
{
    return grindMode == ModeNoGrind;
}

// The population: the quest-first roster bots QuestFirstProgression applies to (Enabled + persistent roster member),
// on their own (a real player leads nobody here: his commands stay his), below the max player level ("bis die Stufe
// 60 sind"; maxLevel 0 = no level bound).
inline bool Applies(std::uint32_t grindMode, bool questFirst, bool realMaster, std::uint32_t level, std::uint32_t maxLevel)
{
    return NoGrind(grindMode) && questFirst && !realMaster && (!maxLevel || level < maxLevel);
}

// twow-repo#601 S2 / S4 (OB-10 rev. 2): mode 1 only. Every mode-1 branch asks this first, so modes 0 and 2 keep their
// exact behaviour.
inline bool GapFiller(std::uint32_t grindMode)
{
    return grindMode == ModeGapFiller;
}

// The population of mode 1: the same quest-first roster bots on their own below the max player level as Applies (mode 2).
inline bool GapFillerApplies(std::uint32_t grindMode, bool questFirst, bool realMaster, std::uint32_t level, std::uint32_t maxLevel)
{
    return GapFiller(grindMode) && questFirst && !realMaster && (!maxLevel || level < maxLevel);
}

// R1 (entry): a mode-1 bot of the population grinds only while the quest route backoff runs - the last quest search found
// nothing and the quest request waits for it. Any S1 failure class opens a window, a transient one too (OP4: "no route
// now"; the window is bounded by the backoff anyway). S1's turn-in yield (GrindYieldsToTurnIn) is asked before this and
// never holds while the backoff runs. No refresh renews a mode-1 grind trip (travelRefresh: the "is travel refresh" pass).
inline bool GapWindowOpen(bool routeBackoffRunning, bool travelRefresh)
{
    return routeBackoffRunning && !travelRefresh;
}

// R2 (exit): the window of a mode-1 grind trip - the remaining backoff, at most the longest backoff (maxBackoffSeconds =
// quest_search::RouteBackoffMaxSeconds), plus GapWindowGraceSeconds. The trip expires with the backoff, the request gate
// opens and the next quest search runs: a grind window never outlives its backoff.
constexpr std::uint32_t GapWindowGraceSeconds = 30;

inline std::uint32_t GapWindowSeconds(std::uint32_t backoffUntil, std::uint32_t now, std::uint32_t maxBackoffSeconds)
{
    std::uint32_t const remaining = backoffUntil > now ? backoffUntil - now : 0u;
    return (remaining < maxBackoffSeconds ? remaining : maxBackoffSeconds) + GapWindowGraceSeconds;
}

// The status time (ms) of a mode-1 grind trip in READY, TRAVEL and WORK: the time left until its window end (absolute,
// seconds; 0 = no window, statusMs stays). At or past the end 1 ms: the trip expires at once. Never longer than the
// longest window (fails closed on a clock step back).
inline std::uint32_t GapStatusMs(std::uint32_t statusMs, std::uint32_t windowEnd, std::uint32_t now, std::uint32_t maxBackoffSeconds)
{
    if (!windowEnd)
        return statusMs;
    std::uint32_t const longest = maxBackoffSeconds + GapWindowGraceSeconds;
    std::uint32_t const left = windowEnd > now ? windowEnd - now : 0u;
    return left ? (left < longest ? left : longest) * 1000u : 1u;
}

// R2 for a COOLDOWN of a gap grind trip (a move-retry or group-member cooldown; review C-F1 / B-F1): the shorter of the cooldown
// and the time left until the window end, so no cooldown outlives the window (statusMs 0 = no limit: the window).
// windowEnd 0 = no window: statusMs stays.
inline std::uint32_t GapCooldownMs(std::uint32_t statusMs, std::uint32_t windowEnd, std::uint32_t now, std::uint32_t maxBackoffSeconds)
{
    if (!windowEnd)
        return statusMs;
    std::uint32_t const left = GapStatusMs(statusMs, windowEnd, now, maxBackoffSeconds);
    return statusMs && statusMs < left ? statusMs : left;
}

// R1 (review B-F3): no money grind for the population - the money trigger ("val::should get money", 6.77) never starts a
// gap grind trip, only the plain need-travel-purpose request (6.35, the R1 window) does. Not the gap filler: as before.
inline bool GapGrindRequestAllowed(bool gapGrind, bool moneyCondition)
{
    return !gapGrind || !moneyCondition;
}

// R1 - R3 (review C-F3 / B-F2): no group copy of a grind trip for the population - a member's grind destination stays a grind for
// this bot unless it needs the creature for an active quest itself; its own R1 request opens the window (R2 end, R3 level
// window, own map, state=grind_window). Not the gap filler, or no grind destination: as before.
inline bool GapGroupCopyAllowed(bool gapGrind, bool grindDestination, bool neededForQuest)
{
    return !gapGrind || !grindDestination || neededForQuest;
}

// The S2 level window named on the state=grind_window line: [max(gray + 1, L - 5), L + 1] (grayLevel =
// MaNGOS::XP::GetGrayLevel(L), passed in: no game header here).
inline std::uint32_t GapWindowLowLevel(std::uint32_t level, std::uint32_t grayLevel)
{
    std::uint32_t const belowLevel = level > 5u ? level - 5u : 1u;
    return grayLevel + 1u > belowLevel ? grayLevel + 1u : belowLevel;
}

inline std::uint32_t GapWindowHighLevel(std::uint32_t level)
{
    return level + 1u;
}

// One line per window (one backoff), at its first grind trip; separate from state=grind_fallback (mode 0 only).
inline std::string FormatGapWindowLine(std::uint32_t bot, std::uint32_t level, std::uint32_t backoffSeconds, std::uint32_t lowLevel,
    std::uint32_t highLevel, std::uint32_t map)
{
    return "[QuestSearch] state=grind_window bot=" + std::to_string(bot) + " level=" + std::to_string(level) + " backoff_s=" +
        std::to_string(backoffSeconds) + " window=" + std::to_string(lowLevel) + "-" + std::to_string(highLevel) + " map=" +
        std::to_string(map);
}

// R3 / R4 (S2, OB-10 rev. 2 point 1): a level (template MaxLevel, area level or creature level) inside the window [lowLevel,
// highLevel] of GapWindowLowLevel / GapWindowHighLevel. highLevel 0 = no window (not the population): false, so a caller
// without its scope test fails closed.
inline bool InGapWindow(std::uint32_t lowLevel, std::uint32_t highLevel, std::uint32_t level)
{
    return highLevel && level >= lowLevel && level <= highLevel;
}

// R3 own map (precedent #414 gather_node::StaysOnMap): a mode-1 grind destination only on the bot's own map.
inline bool GapGrindStaysOnMap(bool gapGrind, bool grindDestination, std::uint32_t botMap, std::uint32_t targetMap)
{
    return !gapGrind || !grindDestination || botMap == targetMap;
}

// R4: a plain grind candidate of the population (not needed for an active quest, not on the bot) outside the window is
// skipped. Attackers (taken before any candidate), creatures on the bot and quest mobs - grey ones too - stay.
inline bool GapTargetSkipped(bool gapGrind, bool neededForQuest, bool victimIsBot, std::uint32_t lowLevel, std::uint32_t highLevel,
    std::uint32_t creatureLevel)
{
    return gapGrind && !neededForQuest && !victimIsBot && !InGapWindow(lowLevel, highLevel, creatureLevel);
}

// [GrindWindow] no_dest=: an empty grind fetch inside a window (the backoff runs) counts once per window (backoff,
// identified by its end), and not after a grind trip of that window started (startedFor = the backoff of the last
// state=grind_window line, noDestFor = the backoff already counted).
inline bool GapNoDestDue(bool windowOpen, std::int32_t backoffUntil, std::int32_t startedFor, std::int32_t noDestFor)
{
    return windowOpen && backoffUntil != startedFor && backoffUntil != noDestFor;
}

// [GrindWindow] minute counters (OB-10 rev. 2 points 2 / 5), counted on the map threads (relaxed atomics outside this pure
// header), taken by the world thread. engaged / grey_engaged: plain grind engagements of the population inside a window and
// those without XP - no kill hook sees a grey kill (the XP packet comes only with XP), so the engagement is counted, like
// bot_events and [GrindPick].
enum class WindowEvent : std::uint8_t
{
    Starts = 0,   // state=grind_window lines (one per window)
    Expired,      // gap grind trips ended by their window end (R2), no failure
    Engaged,      // plain grind engagements inside a window
    GreyEngaged,  // of those, the ones without XP
    NoDest,       // windows whose grind fetch found no destination (the bot stands until the backoff ends)
    R4Skips,      // plain grind candidates R4 skipped (per grind target pass, not per engagement)
};
constexpr std::size_t WindowEventCount = 6;

// "[GrindWindow] minute starts= expired= engaged= grey_engaged= no_dest= r4_skips="; empty (no line) when every count is 0.
inline std::string FormatWindowMinuteLine(std::array<std::uint32_t, WindowEventCount> const& counts)
{
    bool any = false;
    for (std::uint32_t const count : counts)
        any = any || count != 0;
    if (!any)
        return std::string();
    return "[GrindWindow] minute starts=" + std::to_string(counts[std::size_t(WindowEvent::Starts)]) +
        " expired=" + std::to_string(counts[std::size_t(WindowEvent::Expired)]) +
        " engaged=" + std::to_string(counts[std::size_t(WindowEvent::Engaged)]) +
        " grey_engaged=" + std::to_string(counts[std::size_t(WindowEvent::GreyEngaged)]) +
        " no_dest=" + std::to_string(counts[std::size_t(WindowEvent::NoDest)]) +
        " r4_skips=" + std::to_string(counts[std::size_t(WindowEvent::R4Skips)]);
}

// Why a bot of the population stands idle instead of grinding.
enum class Reason : std::uint8_t
{
    None = 0,
    Fallback,       // the 8.32 grind fallback (NeedTravelPurpose Grind) or a pure grind request without a money reason
    Money,          // "val::should get money" -> request travel target::Grind (also with GrindFallbackFailures 0)
    Opportunistic,  // "attack anything" on a creature that is no attacker and no quest objective, with a travel
                    // target (at its destination; on the way only when the action would really attack)
    NoTarget,       // the same without an active travel target (the grind strategy's "no target" kill)
};
constexpr std::size_t ReasonCount = 5;

inline char const* ReasonName(Reason reason)
{
    switch (reason)
    {
        case Reason::Fallback: return "fallback";
        case Reason::Money: return "money";
        case Reason::Opportunistic: return "opportunistic";
        case Reason::NoTarget: return "no_target";
        default: return "-";
    }
}

// A grind target candidate: off -> every candidate as before; on -> only an attacker (self-defence, group help) or a
// creature needed for an active quest (kill or drop objective).
inline bool TargetAllowed(bool noGrind, bool attacker, bool neededForQuest)
{
    return !noGrind || attacker || neededForQuest;
}

// A pure grind travel destination (GrindTravelDestination: a creature that drops money): off -> as before; on -> only
// when the creature is needed for an active quest (the quest objective mob destinations stay).
inline bool GrindDestinationActive(bool noGrind, bool neededForQuest)
{
    return !noGrind || neededForQuest;
}

// A forced travel target (the guild Farm / Kill order) never asks IsActive, so a kill trip (grind mob, boss, skinning)
// would be a forced trip to mobs the target rule above then refuses (review fix). off -> as before; on -> a forced
// kill trip only to a creature needed for an active quest; anything that is no kill trip stays.
inline bool ForcedKillTripAllowed(bool noGrind, bool killTrip, bool neededForQuest)
{
    return !noGrind || !killTrip || neededForQuest;
}

// An "attack anything" target that was turned down: on the way somewhere (travel target active) it is opportunistic,
// otherwise it is the grind strategy's idle kill.
inline Reason TargetReason(bool travelTargetActive)
{
    return travelTargetActive ? Reason::Opportunistic : Reason::NoTarget;
}

// The grind target value is computed before AttackAnythingAction's "traveling and free to move -> no attack" test, so a
// bot on its way to a quest past non-quest mobs would be counted although it would not have attacked in mode 0 either
// (review fix). Noted only at the destination (working: opportunistic) or without a travel target (no_target); the pure
// traveling state stays silent.
inline bool TargetNoteDue(bool travelTargetWorking, bool travelTargetTraveling)
{
    return travelTargetWorking || !travelTargetTraveling;
}

// A grind travel request that was turned down: the "should get money" trigger is the money grind, anything else the
// grind fallback.
inline bool IsMoneyCondition(std::string const& condition)
{
    return condition.find("should get money") != std::string::npos;
}

inline Reason RequestReason(bool moneyCondition)
{
    return moneyCondition ? Reason::Money : Reason::Fallback;
}

// The bit of a reason in the per-bot mask of reasons already logged in this idle episode (None has no bit).
inline std::uint8_t ReasonBit(Reason reason)
{
    return reason == Reason::None ? std::uint8_t(0) : std::uint8_t(1u << std::uint8_t(reason));
}

// Bounded log (review fix): one [QuestIdle] line per reason and idle episode, so at most 4 lines per episode however
// the reasons alternate (an idle bot meets money, fallback and no_target in one engine walk). None never logs. The
// episode ends when a quest route is found (the mask is cleared). loggedMask = the reasons already logged.
inline bool LogDue(std::uint8_t loggedMask, Reason reason)
{
    return reason != Reason::None && !(loggedMask & ReasonBit(reason));
}

inline std::uint8_t MarkLogged(std::uint8_t loggedMask, Reason reason)
{
    return std::uint8_t(loggedMask | ReasonBit(reason));
}

// The blocked thing for target=: "what" or "what:entry" (entry 0 = no entry), "-" for nothing.
inline std::string TargetText(char const* what, std::uint32_t entry)
{
    if (!what || !*what)
        return "-";
    std::string text(what);
    if (entry)
        text += ":" + std::to_string(entry);
    return text;
}

// failureClass: the bot's last S1 rejection class in this idle episode (ai::quest_failure_class::ClassName, "none"
// without one) - lets a soak tie idle lines to transient / structural / empty_offer chains (OB-10, 17.10.).
inline std::string FormatIdleLine(Reason reason, std::uint32_t bot, std::uint32_t level, std::uint32_t zone, std::string const& target,
    char const* failureClass)
{
    return "[QuestIdle] reason=" + std::string(ReasonName(reason)) + " bot=" + std::to_string(bot) + " level=" +
        std::to_string(level) + " zone=" + std::to_string(zone) + " target=" + (target.empty() ? std::string("-") : target) +
        " class=" + (failureClass && *failureClass ? std::string(failureClass) : std::string("none"));
}

// Once a minute (world thread): blocked grind decisions since the last line, per reason.
inline std::string FormatMinuteLine(std::array<std::uint32_t, ReasonCount> const& counts)
{
    return "[QuestIdle] minute fallback=" + std::to_string(counts[std::size_t(Reason::Fallback)]) +
        " money=" + std::to_string(counts[std::size_t(Reason::Money)]) +
        " opportunistic=" + std::to_string(counts[std::size_t(Reason::Opportunistic)]) +
        " no_target=" + std::to_string(counts[std::size_t(Reason::NoTarget)]);
}
}
