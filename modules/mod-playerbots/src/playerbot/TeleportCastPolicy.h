#pragma once

#include <cstdint>

namespace ai::teleport_cast
{
// twow-repo#333 finding 4 (CLI-Berufe pattern hunt 27.10.2026, OB-00; M1r run 4: Hearthstone 21 x INTERRUPTED + 1 x
// SPELL_IN_PROGRESS against 8 x OK). A travel step that starts the hearthstone or a teleport spell through DoSpecificAction
// returned true, and the engine then set the AI wait to the travel action's own duration, replacing the cast wait of the
// inner use action. The next pass found the spell not useful (the item cheat sets its cooldown at the cast start) or in
// progress, failed the step and reset the path; the move retries ran out in about a second, and the next movement interrupted
// the cast. Now the outer action holds for the cast (the wait taken from the spell itself, TeleportCastWait.h), and the travel
// step and unstuck wait for a teleport the bot already prepares.
// AiPlayerbot.CastWait.Preparing = 1 (profile 1) holds the AI centrally for a bot on its own; this covers bots with a real
// player master and the switch off. Pure rules, no game types.

constexpr std::uint32_t kHearthstone = 8690;
constexpr std::uint32_t kScourgestone = 54403;   // UseHearthStoneAction's stand-in without a Hearthstone

// The bot already prepares the teleport this step asks for: the node's spell itself, and for the hearthstone node also the
// Scourgestone. 0 = no step spell or nothing prepared.
inline bool SameTeleport(std::uint32_t stepSpell, std::uint32_t preparingSpell)
{
    if (!stepSpell || !preparingSpell)
        return false;
    if (preparingSpell == stepSpell)
        return true;
    return stepSpell == kHearthstone && preparingSpell == kScourgestone;
}

// The wait of a step that waits for that teleport: the rest of the cast plus one react delay.
inline std::uint32_t WaitMs(std::uint32_t castRestMs, std::uint32_t reactDelayMs)
{
    return castRestMs + reactDelayMs;
}

// The duration the outer action holds after a started teleport. preparedWaitMs = the wait taken from the spell itself (rest of
// the cast + react delay; neither jittered nor left over from an earlier run), 0 when the spell is not PREPARING (an instant
// teleport, or already cast). Without it, the AI wait the inner action set, at least one react delay.
inline std::uint32_t HoldMs(std::uint32_t preparedWaitMs, std::uint32_t innerWaitMs, std::uint32_t reactDelayMs)
{
    if (preparedWaitMs)
        return preparedWaitMs;
    return innerWaitMs > reactDelayMs ? innerWaitMs : reactDelayMs;
}
}
