#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ai::quest_first_order
{
// twow-repo#601 decision order (owner decision 20.10.2026 via OB-00: "es gibt immer eine Quest"): for a quest-first roster
// bot "attack anything" must never win over an active quest objective, quest travel or a turn-in. L1-c (GreyByLevel 1,
// 19./20.10.): the grind strategy's "no target" -> "attack anything" (5.0) ties with "move to rpg target" (5.0, the walk to
// the quest giver) and, now that grinding always finds a target, wins every time - 1 accept, 0 turn-ins, 2,108 attacks in
// the first 3 h; turn-ins per bot-h 3.93 / 2.53 (L1-a / L1-b) -> 0.62. Pure rules, no game types, no locks, no config reads.
//
// AiPlayerbot.Grind.QuestFirstOrder (restart-only, default 0 = as before). With 1, a quest-first roster bot on its own
// below the max player level takes as its "attack anything" (grind) target only
//   - a creature that attacks it or its group (the attacker loop of GrindTargetValue, unchanged),
//   - a creature needed for an active quest (kill or drop objective),
//   - a creature that blocks its way: hostile, not a player, in front of the bot inside 1.5 x its aggro distance, would
//     attack on sight and is below bot level + 3 (the "attack before being attacked" rule of AttackAnythingAction).
// Everything else is skipped, so the quest work (rpg quest giver, quest travel, turn-in) is not interrupted. A pass that
// skipped candidates and has no travel target is "no quest work" - a defect for the quest search (S5a / R08), counted as
// no_quest=, never a reason to grind. The same population takes no kill trip the filter would refuse (CLI-Deep review): the
// 8.32 grind fallback and the money grind are refused (grind_refused=), as are pure grind destinations, boss, skinning and
// forced guild kill trips (PlayerbotAI::IsQuestNoGrindTrip, the GrindMode 2 points). A quest objective of ANOTHER active quest
// still wins the 5.0 tie against the walk to a turn-in (quest work, accepted). skipped= counts candidate skips per value pass,
// not bots or decisions. Not touched: GrindMode 1 / 2 (their own rules come first), mining / herbalism / fishing, dps / tank
// assist, real players' bots.

inline bool Applies(bool enabled, bool questFirstRosterBot, bool realPlayerMaster, std::uint32_t level, std::uint32_t maxLevel)
{
    return enabled && questFirstRosterBot && !realPlayerMaster && level < maxLevel;
}

// withinAggro is the distance test itself: the core's Unit::isInFront(target, distance, arc) ignores the distance (only the
// arc), so the old AttackAnythingAction rule held for every hostile creature ahead within sight - not only one about to attack.
inline bool BlocksPath(bool isPlayer, bool inFront, bool withinAggro, bool attacksOnSight, std::uint32_t mobLevel, std::uint32_t botLevel)
{
    return !isPlayer && inFront && withinAggro && attacksOnSight && mobLevel < botLevel + 3;
}

// A grind target candidate: off -> every candidate as before; on -> only a quest objective or a path blocker (attackers are
// taken before this test).
inline bool TargetAllowed(bool applies, bool neededForQuest, bool blocksPath)
{
    return !applies || neededForQuest || blocksPath;
}

enum class Event : std::uint8_t
{
    Skipped = 0,     // a candidate turned down
    Objective = 1,   // picked: needed for a quest
    Blocker = 2,     // picked: blocks the way, no quest need
    QuestWork = 3,   // a pass left empty by the skips while a travel target is active (the bot goes on questing)
    NoQuest = 4,     // a pass left empty by the skips without a travel target: no quest work found - a defect, not grind
    GrindRefused = 5, // a grind fetch refused (8.32 grind fallback, money grind): per refused request, no quest work either
};
constexpr std::size_t EventCount = 6;

inline Event PickEvent(bool neededForQuest)
{
    return neededForQuest ? Event::Objective : Event::Blocker;
}

inline Event EmptyEvent(bool travelTargetActive)
{
    return travelTargetActive ? Event::QuestWork : Event::NoQuest;
}

inline std::string FormatMinuteLine(std::array<std::uint32_t, EventCount> const& counts)
{
    return "[QuestOrder] minute skipped=" + std::to_string(counts[std::size_t(Event::Skipped)]) +
        " objective=" + std::to_string(counts[std::size_t(Event::Objective)]) +
        " blocker=" + std::to_string(counts[std::size_t(Event::Blocker)]) +
        " quest_work=" + std::to_string(counts[std::size_t(Event::QuestWork)]) +
        " no_quest=" + std::to_string(counts[std::size_t(Event::NoQuest)]) +
        " grind_refused=" + std::to_string(counts[std::size_t(Event::GrindRefused)]);
}
// Per-bot episode (OB-00 20.10.: the owner wants to see how often "no quest found" happens, per bot, band and zone): from the
// first no_quest / grind_refused until the next quest target, each kind logs once - "[QuestOrder] state=no_quest|grind_refused
// bot= level= band= zone= map=" - and the end "[QuestOrder] state=quest_found ... seconds= had=" gives the episode length.
// band = level / 5 (0 = L0-4, ..., 12 = L60), as [BotActivity]. A bot that logs out mid-episode has no end line.
inline std::uint8_t EpisodeBit(Event event)
{
    return event == Event::NoQuest ? std::uint8_t(1) : event == Event::GrindRefused ? std::uint8_t(2) : std::uint8_t(0);
}

inline std::uint32_t BandOf(std::uint32_t level)
{
    return level / 5 < 12 ? level / 5 : 12;
}

inline std::string FormatEpisodeLine(Event event, std::uint32_t bot, std::uint32_t level, std::uint32_t zone, std::uint32_t map)
{
    return std::string("[QuestOrder] state=") + (event == Event::NoQuest ? "no_quest" : "grind_refused") + " bot=" +
        std::to_string(bot) + " level=" + std::to_string(level) + " band=" + std::to_string(BandOf(level)) + " zone=" +
        std::to_string(zone) + " map=" + std::to_string(map);
}

inline std::string FormatEpisodeEndLine(std::uint32_t bot, std::uint32_t level, std::uint32_t zone, std::uint32_t seconds,
    std::uint8_t logged)
{
    std::string had;
    if (logged & 1)
        had = "no_quest";
    if (logged & 2)
        had += had.empty() ? "grind_refused" : ",grind_refused";
    return "[QuestOrder] state=quest_found bot=" + std::to_string(bot) + " level=" + std::to_string(level) + " band=" +
        std::to_string(BandOf(level)) + " zone=" + std::to_string(zone) + " seconds=" + std::to_string(seconds) + " had=" + had;
}
}
