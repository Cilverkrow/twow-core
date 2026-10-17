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
//   1 = RESERVED for the gap filler of stages S2 / S4; until then it behaves exactly like 0.
//   2 = no grind as a substitute activity for quest-first roster bots on their own below the max player level: no
//       8.32 grind fallback, no money grind, no pure grind travel destination, no "attack anything" on a creature that
//       neither attacks the bot / its group nor is needed for an active quest. The bot stands idle and says why
//       ([QuestIdle]). Attacks on attackers (self-defence, group help), quest objective kills, GenericRpg, Explore,
//       gathering and RosterGuild travel stay.
//   Any other value acts like 0.
constexpr std::uint32_t ModeToday = 0;
constexpr std::uint32_t ModeGapFiller = 1;   // reserved for S2 / S4, acts like ModeToday until then
constexpr std::uint32_t ModeNoGrind = 2;

// Only mode 2 changes anything; mode 1 is the same as mode 0 until S4.
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

// Why a bot of the population stands idle instead of grinding.
enum class Reason : std::uint8_t
{
    None = 0,
    Fallback,       // the 8.32 grind fallback (NeedTravelPurpose Grind) or a pure grind request without a money reason
    Money,          // "val::should get money" -> request travel target::Grind (also with GrindFallbackFailures 0)
    Opportunistic,  // "attack anything" on a creature that is no attacker and no quest objective, on the way somewhere
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

// An "attack anything" target that was turned down: on the way somewhere (travel target active) it is opportunistic,
// otherwise it is the grind strategy's idle kill.
inline Reason TargetReason(bool travelTargetActive)
{
    return travelTargetActive ? Reason::Opportunistic : Reason::NoTarget;
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

inline std::string FormatIdleLine(Reason reason, std::uint32_t bot, std::uint32_t level, std::uint32_t zone, std::string const& target)
{
    return "[QuestIdle] reason=" + std::string(ReasonName(reason)) + " bot=" + std::to_string(bot) + " level=" +
        std::to_string(level) + " zone=" + std::to_string(zone) + " target=" + (target.empty() ? std::string("-") : target);
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
