#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace ai::quest_feasibility
{
// twow-repo#601 S3a (design rev. 2, OB-10 + OB-00 decisions 18.10.2026): feasibility at accept time. Pure rules, no game
// types, no locks, no config reads.
//
// Keys (all default 0 / "" = today's behaviour, restart-only):
//   AiPlayerbot.QuestFeasibility.Enable                        master switch; every other key acts only with it
//   AiPlayerbot.QuestFeasibility.Blocklist                     quest ids a roster bot on its own never accepts
//   AiPlayerbot.QuestFeasibility.BlocklistWhileNoCustomStarts  the same, only while AiPlayerbot.CustomRaceStartZones = 0
//   AiPlayerbot.QuestFeasibility.SoloSkipGroupQuests           a bot not in a group skips Type 1 / 81 / 62 and
//                                                              SuggestedPlayers >= 2 quests (OB-20, OB-00 Q-O1)
//
// Scope: a persistent roster bot on its own (no real player leads it), the scope of quest_accept::SkipForRosterBot.
// Class quests are never skipped or dropped. Accept paths A / B / C (the one accept rule) and the autonomous quest item
// (G); the giver relation of a listed quest is not a travel target. Never the taker relation: a complete listed quest
// may still be turned in (OB-10 rev. 2). Shares, catch-up, escort confirm and guild orders stay a player's decision.

// At most this many ids per list; the rest of the text is ignored (fail closed: a typo never blocks more).
constexpr std::size_t MaxIds = 512;

// "62, 76,984" -> {62, 76, 984}. Blanks and anything but a plain decimal id > 0 are skipped, duplicates dropped, at most
// MaxIds kept (pattern: ai::shortcut::ParseGuidList).
inline std::set<std::uint32_t> ParseQuestIdList(std::string const& text)
{
    std::set<std::uint32_t> ids;
    std::size_t pos = 0;
    while (pos <= text.size())
    {
        std::size_t end = text.find(',', pos);
        if (end == std::string::npos)
            end = text.size();

        std::size_t first = pos;
        std::size_t last = end;
        while (first < last && (text[first] == ' ' || text[first] == '\t'))
            ++first;
        while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t'))
            --last;

        bool valid = first < last && last - first <= 10;
        std::uint64_t value = 0;
        for (std::size_t i = first; valid && i < last; ++i)
        {
            char const c = text[i];
            if (c < '0' || c > '9')
                valid = false;
            else
                value = value * 10 + std::uint64_t(c - '0');
        }
        if (valid && value > 0 && value <= 0xFFFFFFFFull && ids.size() < MaxIds)
            ids.insert(std::uint32_t(value));

        pos = end + 1;
    }
    return ids;
}

// Quest types of the core (src/game/QuestDef.h QuestTypes).
constexpr std::uint32_t TypeElite = 1;
constexpr std::uint32_t TypeRaid = 62;
constexpr std::uint32_t TypeDungeon = 81;

// OB-20's group criterion plus dungeon and raid (OB-00 Q-O1): a quest a bot does not do alone.
inline bool IsGroupQuest(std::uint32_t type, std::uint32_t suggestedPlayers)
{
    return type == TypeElite || type == TypeDungeon || type == TypeRaid || suggestedPlayers >= 2;
}

enum class Reason : std::uint8_t
{
    None = 0,
    Blocklist,       // AiPlayerbot.QuestFeasibility.Blocklist
    BlocklistCrsz0,  // AiPlayerbot.QuestFeasibility.BlocklistWhileNoCustomStarts, CustomRaceStartZones = 0
    GroupSolo,       // SoloSkipGroupQuests, the bot is not in a group
};
constexpr std::size_t ReasonCount = 4;

inline char const* ReasonName(Reason reason)
{
    switch (reason)
    {
        case Reason::Blocklist: return "blocklist";
        case Reason::BlocklistCrsz0: return "blocklist_crsz0";
        case Reason::GroupSolo: return "group_solo";
        default: return "none";
    }
}

// Where the skip happened (path= of the skip line).
enum class Path : std::uint8_t
{
    Accept = 0,  // A / B / C: SkipQuestForRosterBot (AcceptQuestAction.cpp)
    Item,        // G: UseRandomQuestItemAction's candidate filter
    Giver,       // QuestRelationTravelDestination::IsActive, giver relation
};

inline char const* PathName(Path path)
{
    switch (path)
    {
        case Path::Accept: return "accept";
        case Path::Item: return "item";
        case Path::Giver: return "giver";
        default: return "-";
    }
}

// What the game side knows about one quest and one bot.
struct Facts
{
    bool enabled = false;               // AiPlayerbot.QuestFeasibility.Enable
    bool rosterOnItsOwn = false;        // persistent roster member, no real player master
    bool classQuest = false;            // RequiredClasses != 0
    bool inGroup = false;               // the bot is in a group (any group)
    bool listed = false;                // in Blocklist
    bool listedNoCustomStarts = false;  // in BlocklistWhileNoCustomStarts
    bool customRaceStartZones = false;  // AiPlayerbot.CustomRaceStartZones
    bool soloSkipGroupQuests = false;   // AiPlayerbot.QuestFeasibility.SoloSkipGroupQuests
    std::uint32_t type = 0;             // Quest::GetType()
    std::uint32_t suggestedPlayers = 0; // Quest::GetSuggestedPlayers()
};

// The one decision. Enable first, then the scope, then class quests: nothing else is looked at with the switch off.
inline Reason SkipReason(Facts const& facts)
{
    if (!facts.enabled || !facts.rosterOnItsOwn || facts.classQuest)
        return Reason::None;
    if (facts.listed)
        return Reason::Blocklist;
    if (facts.listedNoCustomStarts && !facts.customRaceStartZones)
        return Reason::BlocklistCrsz0;
    if (facts.soloSkipGroupQuests && !facts.inGroup && IsGroupQuest(facts.type, facts.suggestedPlayers))
        return Reason::GroupSolo;
    return Reason::None;
}

// The giver relation is inactive for listed quests only (design: "the giver relation of a blocked quest"); a solo bot's
// group quest keeps today's giver gate (IsPossible: elite / dungeon need "can fight boss").
inline bool GiverInactive(Reason reason)
{
    return reason == Reason::Blocklist || reason == Reason::BlocklistCrsz0;
}

// Drop step (OB-10 rev. 2): only open quests - a complete one may still be turned in. nullptr = keep.
inline char const* DropReasonName(Reason reason, bool complete)
{
    if (complete)
        return nullptr;
    switch (reason)
    {
        case Reason::Blocklist:
        case Reason::BlocklistCrsz0: return "infeasible_blocklist";
        case Reason::GroupSolo: return "infeasible_group_solo";
        default: return nullptr;
    }
}

// Bounded skip lines, per bot (bot's own thread only): one line per (bot, quest) per RepeatSeconds while the quest stays
// in the ring, and at most LinesPerMinute lines per bot and minute however many listed givers a hub walk meets. Small
// fixed ring, nothing per creature.
constexpr std::size_t LogRingSize = 16;
constexpr std::uint32_t RepeatSeconds = 3600;
constexpr std::uint32_t LinesPerMinute = 4;

struct SkipLog
{
    std::array<std::uint32_t, LogRingSize> quests{};
    std::array<std::uint32_t, LogRingSize> loggedAt{};
    std::uint32_t next = 0;
    std::uint32_t minuteStart = 0;
    std::uint32_t minuteLines = 0;
};

// True = write the line now (and remember it); false = stay silent.
inline bool LogDue(SkipLog& log, std::uint32_t questId, std::uint32_t now)
{
    if (!questId)
        return false;
    for (std::size_t i = 0; i < LogRingSize; ++i)
        if (log.quests[i] == questId && now >= log.loggedAt[i] && now - log.loggedAt[i] < RepeatSeconds)
            return false;

    if (now < log.minuteStart || now - log.minuteStart >= 60)
    {
        log.minuteStart = now;
        log.minuteLines = 0;
    }
    if (log.minuteLines >= LinesPerMinute)
        return false;
    ++log.minuteLines;

    std::size_t slot = log.next % LogRingSize;
    for (std::size_t i = 0; i < LogRingSize; ++i)
        if (log.quests[i] == questId)
            slot = i;   // refresh the old entry, keep the others
    if (slot == log.next % LogRingSize)
        log.next = std::uint32_t((log.next + 1) % LogRingSize);
    log.quests[slot] = questId;
    log.loggedAt[slot] = now;
    return true;
}

inline std::string FormatSkipLine(std::uint32_t bot, std::uint32_t level, std::uint32_t questId, Reason reason, Path path)
{
    return "[QuestFeasibility] state=skip bot=" + std::to_string(bot) + " level=" + std::to_string(level) + " quest=" +
        std::to_string(questId) + " reason=" + ReasonName(reason) + " path=" + PathName(path);
}

// Once a minute (world thread, only with Enable): accept / item skips, infeasible drops and giver relations found
// inactive since the last line.
inline std::string FormatMinuteLine(std::uint32_t skips, std::uint32_t drops, std::uint32_t giverInactive)
{
    return "[QuestFeasibility] minute skips=" + std::to_string(skips) + " drops=" + std::to_string(drops) +
        " giver_inactive=" + std::to_string(giverInactive);
}
}
