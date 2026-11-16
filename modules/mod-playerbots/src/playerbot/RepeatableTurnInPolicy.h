#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::repeatable_turnin
{
// twow-repo#716 BOT-16 (AiPlayerbot.QuestTurnIn.RepeatableAutoComplete, default 0; bug fix: 1 after the acceptance unit, 0 = rollback).
// Pure rules, no game types, no locks, no config reads.
//
// The bot's "dialog status" (QuestValues.cpp DialogStatusValue::getDialogStatus, the core's GetDialogStatus) gives a complete,
// unrewarded quest of a taker the mark DIALOG_STATUS_REWARD_REP instead of DIALOG_STATUS_REWARD2 when the quest is auto-complete
// (QuestMethod 0) AND repeatable (SpecialFlags 1) - the Blood Shard quests of Mangletooth (889, 5042-5046) are such quests.
// "can turn in quest npc" took only REWARD2 (classic build), so no taker route, no rpg turn-in, and the bots held them complete
// forever. REWARD_REP also marks quests the bot can only TAKE again (not in the log), so the mark alone is no turn-in reason, and
// an AVAILABLE quest of the same NPC (5 > 4) hides it anyway. With the switch, "can turn in quest npc" is also true while the bot
// HOLDS such a quest of that taker complete in its log (and the core would reward it; QuestValues.cpp checks CanRewardQuest).
//
// The mode is a cohort mode (CohortPolicy.h): 0 = off (default), 1 = every bot, 2 = only cohort B, 3 = only cohort A.
// Any other value acts like 0 (the loader logs it once and stores 0).
inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

// True when the switch in `mode` applies to the bot with this low guid (an invalid mode never applies).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ValidMode(mode) && cohort::Applies(mode, guidLow);
}

// One quest of the taker's involved (ender) relations, as this bot sees it.
struct EnderQuest
{
    bool active = false;        // Quest::IsActive
    bool complete = false;      // the bot's quest status is QUEST_STATUS_COMPLETE (in the log, objectives done)
    bool rewarded = false;      // Player::GetQuestRewardStatus (false for every repeatable quest)
    bool autoComplete = false;  // Quest::IsAutoComplete (QuestMethod 0)
    bool repeatable = false;    // Quest::IsRepeatable (SpecialFlags 1)
};

// The quests the old rule misses: held complete and unrewarded, auto-complete AND repeatable. Every other complete, unrewarded
// quest already gives REWARD2 (the highest mark), and a quest that is not complete in the log is never one (take-again case).
inline bool HeldForTurnIn(EnderQuest const& quest)
{
    return quest.active && quest.complete && !quest.rewarded && quest.autoComplete && quest.repeatable;
}
}
