#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::never_possible
{
// twow-repo#602 ROT-01 failure census v3 (OB-00 28.10.2026; AiPlayerbot.Rotation.NeverPossibleSkip, code default 1, 0 = rollback): in hunt2
// 22.8 % of the engine time of all action checks went to spell actions the bot can never cast (Cone of Cold below L26 alone 11 %, other
// races' racials ~6.5 %, talent spells). CastSpellAction::isPossible tested spellId == 0 only for other targets, so self-casts went into
// CanCastSpell every time, and getPrerequisites pushed the reach action (each tick) for a spell the bot does not know. With the switch, a
// spell whose "spell id" value is 0 makes isPossible false at once (IMPOSSIBLE as before, so the engine still pushes the node's
// alternatives - an isUseful early-out would have lost them, e.g. wind shear -> earth shock) and pushes no reach prerequisite.
// Actions whose spell name is a placeholder or changes at runtime (shoot, pull, paladin blessings, shaman imbue, vehicle spells, "mount")
// keep the old path (CastSpellAction::KeepsUnknownSpell). Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The spell is unknown to the bot and the action has no runtime / placeholder name: skip the checks.
inline bool Skip(bool applies, bool keepsUnknown, std::uint32_t knownSpellId)
{
    return applies && !keepsUnknown && knownSpellId == 0;
}

// [NeverPossible] counters. Map threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> possible{ 0 };   // isPossible answered false at once (unknown spell)
inline std::atomic<std::uint64_t> prereq{ 0 };     // reach prerequisites not pushed (unknown spell)
}
