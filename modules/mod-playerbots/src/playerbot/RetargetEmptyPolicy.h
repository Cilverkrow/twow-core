#pragma once

#include <cstdint>

#include "CohortPolicy.h"

namespace ai::retarget_empty
{
// twow-repo#602 ROT-01 W24 pattern B (OB-00 06.11.2026, decision (A): trace first, the rule after the data; log only, no behaviour change;
// AiPlayerbot.Rotation.RetargetEmptyTrace, default 0). After a kill a tank spun ~20 empty ticks a second for up to 29 s: "invalid target"
// -> "select new target" FAILED while "has attackers" stayed true, because the dps/tank assist picks only from "possible attack targets"
// (PossibleAttackTargetsValue), which had nothing. Which attacker stayed and which filter dropped it is not in any log - this trace says it.
// Pure rules, no game types.

// The switch applies: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h).
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// The first filter of "possible attack targets" an attacker fails, in the order PossibleAttackTargetsValue checks them
// (IsPossibleTarget: attackable, range, immune, tapped; then IsValid: a victim / a target / a player / "attack target" / CC / rti).
// Ok = it passes them all - then the assist itself chose nothing (AssistNone on the line).
enum class Reason : std::uint8_t
{
    Ok = 0,
    Gone,               // the unit is not on the bot's map any more
    Dead,
    NotValidAttack,     // Player::IsValidAttackTarget false (friendly, evading-immune, not attackable)
    OutOfRange,         // beyond sightDistance
    Immune,             // banish, invulnerability, immune shield
    NotTapped,          // an NPC tapped by someone else, not on us / the group / the master
    NoVictimNoTarget,   // no threat victim, no UNIT_FIELD_TARGET, no player, not "attack target", no CC, no rti
    AssistNone,         // every attacker passes, the assist still picked nothing
};

inline char const* ReasonName(Reason reason)
{
    switch (reason)
    {
        case Reason::Ok: return "ok";
        case Reason::Gone: return "gone";
        case Reason::Dead: return "dead";
        case Reason::NotValidAttack: return "not_valid_attack";
        case Reason::OutOfRange: return "out_of_range";
        case Reason::Immune: return "immune";
        case Reason::NotTapped: return "not_tapped";
        case Reason::NoVictimNoTarget: return "no_victim_no_target";
        case Reason::AssistNone: return "assist_none";
    }
    return "unknown";
}

// The first failing filter from the facts, in the value's order. tapped is only read for an NPC; passesRest is the rest of
// PossibleAttackTargetsValue::IsValid (victim / target / player / "attack target" / CC / rti).
inline Reason FirstFailing(bool exists, bool alive, bool validAttack, bool inRange, bool immune, bool isNpc, bool tapped, bool passesRest)
{
    if (!exists)
        return Reason::Gone;
    if (!alive)
        return Reason::Dead;
    if (!validAttack)
        return Reason::NotValidAttack;
    if (!inRange)
        return Reason::OutOfRange;
    if (immune)
        return Reason::Immune;
    if (isNpc && !tapped)
        return Reason::NotTapped;
    if (!passesRest)
        return Reason::NoVictimNoTarget;
    return Reason::Ok;
}

// One line per bot at most every intervalSeconds; the first one at once.
inline bool Due(std::uint64_t lastLine, std::uint64_t now, std::uint32_t intervalSeconds)
{
    return lastLine == 0 || now >= lastLine + intervalSeconds;
}

constexpr std::uint32_t LineIntervalSeconds = 10;
}
