#pragma once

#include "CohortPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::elite_quest
{
// AiPlayerbot.QuestElite.Defer (WS30-ELITE-QUEST-01, OB-20 rule 27.11.2026 f6ab4497; design CLI-Deep 28.11. v0.4 0c5c76ad):
// an elite quest is pursued (travel target, target choice, manager / S7 group) only by a group of >= MinGroup members in the
// bot's zone whose lowest level is >= the quest level (rule 1a), or by a bot >= quest level + SoloLevelsAbove, solo or in a
// group (rule 1b). Otherwise the quest is deferred: it stays in the log (accepted as before, never abandoned by this rule) and
// is a candidate again as soon as the rule holds. Pure rules, no game types, no locks, no config reads.
//
// The mode is the shared cohort mode (CohortPolicy.h): 0 off (default), 1 all bots, 2 cohort B (odd low guid), 3 cohort A
// (even low guid); any other value is 0 (the loader writes one error line). The population is a persistent roster bot
// without a real player master; real players' bots are never touched.
constexpr std::uint32_t DefaultMinGroup = 3;
constexpr std::uint32_t MinGroupLow = 2;
constexpr std::uint32_t MinGroupHigh = 5;
constexpr std::uint32_t DefaultSoloLevelsAbove = 5;
constexpr std::uint32_t SoloLevelsAboveHigh = 99;   // OB-20 28.11.: 99 turns rule 1b off without touching 1a

inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

inline std::uint32_t ClampMinGroup(std::uint32_t value)
{
    return std::max(MinGroupLow, std::min(MinGroupHigh, value));
}

inline std::uint32_t ClampSoloLevelsAbove(std::uint32_t value)
{
    return std::min(SoloLevelsAboveHigh, value);
}

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow, bool rosterOnItsOwn)
{
    return rosterOnItsOwn && cohort::Applies(ValidMode(mode) ? mode : cohort::ModeOff, guidLow);
}

// --- scope (rule "Geltung"): Type 1 (elite), or a kill objective on a creature of rank 1 / 2 / 3; never dungeon / raid ----------
constexpr std::uint32_t QuestTypeElite = 1;
constexpr std::uint32_t QuestTypeRaid = 62;
constexpr std::uint32_t QuestTypeDungeon = 81;

inline bool InScope(std::uint32_t questType, bool eliteKillObjective)
{
    if (questType == QuestTypeDungeon || questType == QuestTypeRaid)
        return false;
    return questType == QuestTypeElite || eliteKillObjective;
}

// A creature rank that makes a kill objective "elite": ranks 1, 2, 3 (OB-20 28.11. option (A), rule f6ab4497 scope line 8;
// OB-10 A2). Rank 4 (rare, normal strength) does not count. One constant, here and in the table.
// Ranks: 0 normal, 1 elite, 2 rare elite, 3 boss, 4 rare.
inline bool EliteRank(std::uint32_t rank)
{
    return rank == 1 || rank == 2 || rank == 3;
}

// The effective level the rule compares with (OB-10 28.11. point 2): the quest level, raised to the highest MaxLevel of the
// quest's in-scope kill targets (elite camps sit above the quest level). A quest level <= 0 (scales with the player) gives 0
// here; the caller then uses the bot's own level for it.
inline std::uint32_t EffectiveLevel(std::int32_t questLevel, std::uint32_t highestEliteTargetMaxLevel)
{
    std::uint32_t const ql = questLevel > 0 ? std::uint32_t(questLevel) : 0u;
    return std::max(ql, highestEliteTargetMaxLevel);
}

// The scope table: built once (world thread, after the templates load), sealed (sorted by quest id, first entry of a
// duplicate kept), then read-only (OB-10 A5).
struct ScopeEntry
{
    std::uint32_t questId = 0;
    std::uint32_t level = 0;   // EffectiveLevel
};

inline void Seal(std::vector<ScopeEntry>& table)
{
    std::stable_sort(table.begin(), table.end(), [](ScopeEntry const& a, ScopeEntry const& b) { return a.questId < b.questId; });
    table.erase(std::unique(table.begin(), table.end(), [](ScopeEntry const& a, ScopeEntry const& b) { return a.questId == b.questId; }),
        table.end());
}

// nullptr = not in scope.
inline ScopeEntry const* Find(std::vector<ScopeEntry> const& sealed, std::uint32_t questId)
{
    auto const it = std::lower_bound(sealed.begin(), sealed.end(), questId,
        [](ScopeEntry const& e, std::uint32_t id) { return e.questId < id; });
    return it != sealed.end() && it->questId == questId ? &*it : nullptr;
}

// --- the rule --------------------------------------------------------------------------------------------------------------
// members: alive group members in the bot's zone, the bot included (solo = 1); minLevel: their lowest level (solo = own level).
// questLevel is the table's EffectiveLevel (0 for a scaling quest: the caller passes the bot's own level).
// Pursue never replaces the readiness tests (OB-10 A1): the caller asks Pursue && "can fight elite" for a group and
// Pursue && "can fight equal" for a solo bot.
inline bool Pursue(std::uint32_t members, std::uint32_t minLevel, std::uint32_t botLevel, std::uint32_t questLevel,
    std::uint32_t minGroup, std::uint32_t soloLevelsAbove)
{
    bool const group = members >= ClampMinGroup(minGroup) && minLevel >= questLevel;
    bool const outlevelled = botLevel >= questLevel + ClampSoloLevelsAbove(soloLevelsAbove);
    return group || outlevelled;
}

// Rule 3 (E3, OB-20 28.11. (b)): a bot below the quest level can satisfy neither 1a (lowest level >= quest level) nor 1b, so it
// does not wish the quest and wishes its next one.
inline bool MayWish(std::uint32_t botLevel, std::uint32_t questLevel)
{
    return botLevel >= questLevel;
}

// E4: a group the manager / S7 would form for a scope quest - its size and lowest level; 1b for a group = every member
// >= quest level + SoloLevelsAbove (its lowest level does).
inline bool MayForm(std::uint32_t size, std::uint32_t minLevel, std::uint32_t questLevel, std::uint32_t minGroup,
    std::uint32_t soloLevelsAbove)
{
    return Pursue(size, minLevel, minLevel, questLevel, minGroup, soloLevelsAbove);
}

// Config (ultracode review 28.11. finding "clamped silently"): a raw value outside the range is invalid - the loader writes one
// error line and uses the default, never the nearest (most permissive) bound.
inline bool ValidMinGroup(std::int32_t raw)
{
    return raw >= std::int32_t(MinGroupLow) && raw <= std::int32_t(MinGroupHigh);
}

inline bool ValidSoloLevelsAbove(std::int32_t raw)
{
    return raw >= 0 && raw <= std::int32_t(SoloLevelsAboveHigh);
}

// The entry sets of E2 / the 1b target gate (ultracode review: built from the quests' open-objective destinations - kill
// targets AND item droppers, the source of "needed quest entries"). Inputs are unsorted candidates:
//   deferredElite  elite-rank entries of deferred scope quests,
//   pursuedElite   elite-rank entries of scope quests the rule lets the bot pursue,
//   keep           every entry an incomplete, not deferred quest of the bot needs (elite or not).
// Out: deferred = sorted unique deferredElite minus keep (another quest still needs it); pursued = sorted unique pursuedElite.
inline void FinishEntrySets(std::vector<std::int32_t>& deferredElite, std::vector<std::int32_t>& pursuedElite,
    std::vector<std::int32_t>& keep)
{
    auto const seal = [](std::vector<std::int32_t>& v)
    {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    seal(keep);
    seal(deferredElite);
    seal(pursuedElite);
    deferredElite.erase(std::remove_if(deferredElite.begin(), deferredElite.end(),
        [&keep](std::int32_t entry) { return std::binary_search(keep.begin(), keep.end(), entry); }), deferredElite.end());
}

// The rule that holds for a reward / a decision: "1a" group floor, "1b" outlevelled, "none" (neither).
inline char const* RuleLabel(std::uint32_t members, std::uint32_t minLevel, std::uint32_t botLevel, std::uint32_t questLevel,
    std::uint32_t minGroup, std::uint32_t soloLevelsAbove)
{
    if (members >= ClampMinGroup(minGroup) && minLevel >= questLevel)
        return "1a";
    if (botLevel >= questLevel + ClampSoloLevelsAbove(soloLevelsAbove))
        return "1b";
    return "none";
}

// [QuestElite] reward line (OB-00 28.11.: accepted for the PREREG): one line per rewarded scope quest of a persistent roster
// bot on its own, in BOTH cohorts (the control arm too), with the key on. judged = the rule applies to the bot (cohort, not a
// formed group); formed = a manager / S7 group bot (judged at form time, E4). The solo-1b row and the quest rows come from it.
struct RewardFacts
{
    std::uint32_t guid = 0;
    std::uint32_t questId = 0;
    std::uint32_t questLevel = 0;   // the effective level
    std::uint32_t botLevel = 0;
    std::uint32_t members = 1;
    std::uint32_t minLevel = 0;
    bool judged = false;
    bool formed = false;
};

inline std::string FormatRewardLine(RewardFacts const& f, std::uint32_t minGroup, std::uint32_t soloLevelsAbove)
{
    return "[QuestElite] reward guid=" + std::to_string(f.guid) + " cohort=" + (cohort::InB(f.guid) ? "b" : "a") +
        " quest=" + std::to_string(f.questId) + " level=" + std::to_string(f.questLevel) + " bot_level=" + std::to_string(f.botLevel) +
        " members=" + std::to_string(f.members) + " min_level=" + std::to_string(f.minLevel) +
        " rule=" + RuleLabel(f.members, f.minLevel, f.botLevel, f.questLevel, minGroup, soloLevelsAbove) +
        " judged=" + (f.judged ? "1" : "0") + " formed=" + (f.formed ? "1" : "0");
}

// --- [QuestElite] minute line, per cohort (a = even low guid, b = odd) -------------------------------------------------------
//   travel_deferred  quest objective destinations refused by the rule (E1, per destination check, not bots),
//   target_skipped   grind-scan candidates counted as not needed: an elite target of a deferred quest (E2, per candidate per scan),
//   wish_skipped     scope quests not wished because the bot is below the quest level (E3, per wish read),
//   form_refused     scope-quest groups the manager / S7 did not form (E4, per refused group, leader's cohort),
//   pursued          scope quest destinations the rule let through (E1).
enum class Event : std::uint8_t
{
    TravelDeferred,
    TargetSkipped,
    WishSkipped,
    FormRefused,
    Pursued,
};
constexpr std::size_t EventCount = 5;
constexpr std::size_t SlotCount = 2 * EventCount;

inline std::size_t Slot(Event event, std::uint32_t guidLow)
{
    return std::size_t(event) * 2 + (cohort::InB(guidLow) ? 1 : 0);
}

inline std::string FormatMinuteLine(std::array<std::uint32_t, SlotCount> const& counts)
{
    bool any = false;
    for (std::uint32_t const count : counts)
        any = any || count != 0;
    if (!any)
        return std::string();
    auto const pair = [&counts](Event event, char const* name)
    {
        std::size_t const slot = std::size_t(event) * 2;
        return std::string(" ") + name + "_a=" + std::to_string(counts[slot]) + " " + name + "_b=" + std::to_string(counts[slot + 1]);
    };
    return "[QuestElite] minute" + pair(Event::TravelDeferred, "travel_deferred") + pair(Event::TargetSkipped, "target_skipped") +
        pair(Event::WishSkipped, "wish_skipped") + pair(Event::FormRefused, "form_refused") + pair(Event::Pursued, "pursued");
}
}
