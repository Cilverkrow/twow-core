#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ai::quest_accept
{
// Train 8b (twow-repo#338 measurement, OB-10 analysis 2026-09-30): roster bots took every
// quest a giver offered, and CleanQuestLogAction dropped the red ones again seconds to
// minutes later once the log held 14 quests - 45 % of all drops, some quests taken and
// dropped four times in an hour. A roster bot on its own now skips those quests.

// The red rule of CleanQuestLogAction: the quest level is at least 5 above the bot.
inline bool IsRed(uint32_t botLevel, uint32_t questLevel)
{
    return botLevel + 5 <= questLevel;
}

// Hotfix 8.11 (owner 02.10.2026, quest churn): 8.5/8.7 drop open grey quests of a roster
// bot on its own, but the accept paths only skipped red ones - QuestDetailsAction had no
// filter at all. Bots took a grey quest, dropped it, and took it again at the next giver:
// ~890 accepts and ~940 drops an hour on v24 ("The Grizzled Den" alone 205 times).
// Accepting now uses the drop rule: no grey (XP grey level) and no red quest for a roster
// bot on its own; class quests stay allowed, as the drop rule keeps them.
//
// Hotfix 8.15 (v27, 3 h: still ~275 drops an hour, e.g. one bot took and dropped the same two
// quests within 10 seconds): CleanQuestLogAction drops random quests without progress as soon as
// CleanFreeSlots or fewer slots are free, while the accept paths filled the log to the last slot.
// A roster bot on its own no longer takes a new quest at that fill level.
constexpr uint32_t CleanFreeSlots = 4;

inline bool LogTooFull(uint32_t freeSlots)
{
    return freeSlots <= CleanFreeSlots;
}

inline bool SkipForRosterBot(bool rosterOnItsOwn, uint32_t botLevel, uint32_t questLevel, uint32_t grayLevel,
    bool classQuest, uint32_t freeSlots = 25)
{
    if (!rosterOnItsOwn || classQuest)
        return false;
    return IsRed(botLevel, questLevel) || questLevel <= grayLevel || LogTooFull(freeSlots);
}

// Hotfix 8.17 (v30, 153 min: 127 roster bots held 16-18 quests, took none since 8.15 and
// CleanQuestLogAction keeps 16; quest updates -20 %, XP events -14 %, more idle bots): a roster
// bot on its own that made no progress for IdleRotateSeconds out of combat with a full log
// (at least MaxLog - CleanFreeSlots quests) drops exactly one quest without progress, at most
// once per IdleRotateSeconds, so it can take a nearer one.
//
// Hotfix 8.27 (v33, 45 min: 141 rotations, 88 of them 20 minutes after the world start because the
// idle time counts from login; 112 of another zone - often the quest the bot travelled to; quest
// updates -64 %): a grace period after the bot's first check, a longer threshold, never the quest
// of the current travel target, the oldest first. All of it configurable (AiPlayerbot.QuestRotate.*).
struct RotateConfig
{
    bool enabled = true;
    uint32_t idleSeconds = 40 * 60;     // no progress for this long (also the per-bot interval)
    uint32_t graceSeconds = 30 * 60;    // no rotation this soon after the bot was first seen
};

inline bool IdleRotateDue(bool rosterOnItsOwn, bool inCombat, uint32_t idleSeconds, uint32_t questCount,
    uint32_t maxLog, uint32_t firstSeen, uint32_t lastRotate, uint32_t now, RotateConfig const& config)
{
    if (!config.enabled || !rosterOnItsOwn || inCombat || idleSeconds < config.idleSeconds)
        return false;
    if (questCount + CleanFreeSlots < maxLog)
        return false;
    if (!firstSeen || now - firstSeen < config.graceSeconds)
        return false;
    return !lastRotate || now - lastRotate >= config.idleSeconds;
}

// A quest without progress, not complete and no class quest (the caller filters).
struct RotateCandidate
{
    uint32_t slot = 0;
    bool otherZone = false;     // its zone (ZoneOrSort > 0) is not the bot's zone: far away
};

// Index of the quest to drop, -1 = none: the lowest slot (the log fills from the top, so the lowest
// slot is the oldest entry); another zone only breaks a tie (hotfix 8.27).
inline int PickIdleRotate(std::vector<RotateCandidate> const& candidates)
{
    int best = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (best < 0)
        {
            best = int(i);
            continue;
        }
        RotateCandidate const& c = candidates[i];
        RotateCandidate const& b = candidates[std::size_t(best)];
        if (c.slot != b.slot ? c.slot < b.slot : (c.otherZone && !b.otherZone))
            best = int(i);
    }
    return best;
}
}
