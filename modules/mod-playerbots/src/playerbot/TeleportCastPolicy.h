#pragma once

#include <cstdint>

namespace ai::teleport_cast
{
// twow-repo#333 finding 4 (CLI-Berufe pattern hunt 27.10.2026, OB-00; M1r run 4: Hearthstone 21 x INTERRUPTED + 1 x
// SPELL_IN_PROGRESS against 8 x OK). A travel step that starts the hearthstone or a teleport spell through DoSpecificAction
// returned true, and the engine then set the AI wait to the travel action's own duration, replacing the cast wait of the
// inner use action. The next pass found the spell not useful (the item cheat sets its cooldown at the cast start) or in
// progress, failed the step and reset the path; the move retries ran out in about a second, and the next movement interrupted
// the cast. Now the outer action holds for the cast (the wait taken from the spell itself, TeleportCastWait.h), the travel
// step and unstuck wait for a teleport the bot already prepares, and a watch in PlayerbotAI::UpdateAI ends the hold as soon
// as the spell is no longer PREPARING (OB-10 review 28.10.: interrupt, fear / stun cancel, failure - in combat the bot must
// fight again at once). AiPlayerbot.CastWait.Preparing = 1 (profile 1) holds the AI centrally for a bot on its own; this
// covers bots with a real player master and the switch off. Pure rules, no game types.

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

// The duration an action holds after a start: the wait for the PREPARING teleport (0 = an instant teleport, or a cast that
// ended or failed at once), else one react delay - nothing left to wait for.
inline std::uint32_t HoldMs(std::uint32_t preparedWaitMs, std::uint32_t reactDelayMs)
{
    return preparedWaitMs ? preparedWaitMs : reactDelayMs;
}

// The hold watch: the Spell object the hold waits for (identity, 0 = none), its id, and the absolute end of the hold
// (wrapping millisecond clock, WorldTimer::getMSTime).
struct HoldWatch
{
    std::uint64_t spell = 0;
    std::uint32_t id = 0;
    std::uint32_t deadline = 0;
};

inline bool Watching(HoldWatch const& w)
{
    return w.spell != 0;
}

inline void Arm(HoldWatch& w, std::uint64_t spell, std::uint32_t id, std::uint32_t nowMs, std::uint32_t holdMs)
{
    w.spell = spell;
    w.id = spell ? id : 0;
    w.deadline = spell ? nowMs + holdMs : 0;
}

inline void Clear(HoldWatch& w)
{
    w = HoldWatch();
}

// Keep = the hold goes on; Release = the spell ended before the hold (cast, interrupted, cancelled, failed): end the hold
// now; Expire = the hold is over anyway: stop watching and leave the AI wait alone (it may already belong to something else).
enum class WatchStep { Keep, Release, Expire };

inline WatchStep Check(HoldWatch const& w, std::uint64_t currentSpell, std::uint32_t currentId, bool preparing, std::uint32_t nowMs)
{
    if (!Watching(w))
        return WatchStep::Keep;
    if (static_cast<std::int32_t>(nowMs - w.deadline) >= 0)
        return WatchStep::Expire;
    if (preparing && currentSpell == w.spell && currentId == w.id)
        return WatchStep::Keep;
    return WatchStep::Release;
}
}
