#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::duel_rule
{
// twow-repo#602 ROT-01 W24 duel hang (OB-00 06.11.2026, HARM per ADR-0041 R4: 11 bot pairs in 6 runs, 190-640 min each;
// AiPlayerbot.Rotation.DuelRule, code default 1, 0 = rollback). Two bots of one group stood in a duel for the rest of the run:
//  - "rpg duel" (RpgDuelTrigger) challenged a member of the bot's own group;
//  - the challenger also ran "accept duel" on its own SMSG_DUEL_REQUESTED - the core ignores the initiator's accept
//    (DuelHandler.cpp, HandleDuelAcceptedOpcode), but the action returned true and reset the strategies;
//  - the only duel attack, "duel no attackers" -> "attack duel opponent", needed "my attacker count" > 0 (someone already hitting the
//    bot), so between two bots nobody hit first; "select new target" failed on every tick (the opponent counts in "attackers");
//  - the core ends a duel only by distance (> 75 yd) or a map change, never by time.
// The rule: no rpg duel against a member of the own group; accept only as the challenged player (and the decline branch stops after
// the cancel); a running duel picks the opponent as the target; a bot-vs-bot duel without a fight for N seconds is cancelled.
// Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// "rpg duel" is not offered to a member of the bot's own group.
inline bool SkipGroupDuel(bool applies, bool sameGroup)
{
    return applies && sameGroup;
}

// "accept duel" sends the accept only as the challenged player; the initiator's accept is a no-op in the core.
inline bool SkipInitiatorAccept(bool applies, bool isInitiator)
{
    return applies && isInitiator;
}

// "duel no attackers" also fires for a RUNNING duel (countdown over, not finished) whose opponent is not the current target -
// without waiting for the opponent to hit first.
inline bool EngageOpponent(bool applies, bool duelStarted, bool duelFinished, bool targetIsOpponent)
{
    return applies && duelStarted && !duelFinished && !targetIsOpponent;
}

// The quiet clock of a duel: 0 while there is no duel or while the bot or its opponent fights, else the first quiet second.
inline std::uint64_t NextQuietSince(std::uint64_t quietSince, bool inDuel, bool fighting, std::uint64_t now)
{
    if (!inDuel || fighting)
        return 0;
    return quietSince ? quietSince : now;
}

// A bot-vs-bot duel (requested or running) without a fight for cancelSeconds is cancelled. 0 seconds = never.
inline bool CancelDue(bool applies, bool opponentIsBot, std::uint64_t quietSince, std::uint64_t now, std::uint32_t cancelSeconds)
{
    return applies && opponentIsBot && cancelSeconds > 0 && quietSince > 0 && now >= quietSince + cancelSeconds;
}

// [DuelRule] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> skippedGroup{ 0 };     // "rpg duel" not offered to a group member
inline std::atomic<std::uint64_t> skippedAccept{ 0 };    // "accept duel" skipped as the initiator
inline std::atomic<std::uint64_t> engaged{ 0 };          // "duel no attackers" fired for a running duel without an attacker
inline std::atomic<std::uint64_t> cancelled{ 0 };        // bot-vs-bot duels cancelled after cancelSeconds without a fight
}
