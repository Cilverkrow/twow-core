// twow-repo#602 ROT-01 (L1-b finding 19.10.2026, OB-10): names of the SpellCastResult codes for the action log, so a failed cast says
// why it failed (CAST_FAIL ... reason=SPELL_IN_PROGRESS) instead of "result=1". Pure (std only); the order mirrors enum SpellCastResult
// in src/game/Spells/SpellDefines.h exactly (code 0 = AFFECTING_COMBAT ... last code), checked by cast_result_reason_source_contract.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ai
{
namespace cast_result
{
// SPELL_FAILED_<name> without the prefix, index = code
inline char const* const kNames[] = {
    "AFFECTING_COMBAT",
    "ALREADY_AT_FULL_HEALTH",
    "ALREADY_AT_FULL_POWER",
    "ALREADY_BEING_TAMED",
    "ALREADY_HAVE_CHARM",
    "ALREADY_HAVE_SUMMON",
    "ALREADY_OPEN",
    "AURA_BOUNCED",
    "AUTOTRACK_INTERRUPTED",
    "BAD_IMPLICIT_TARGETS",
    "BAD_TARGETS",
    "CANT_BE_CHARMED",
    "CANT_BE_DISENCHANTED",
    "CANT_BE_PROSPECTED",
    "CANT_CAST_ON_TAPPED",
    "CANT_DUEL_WHILE_INVISIBLE",
    "CANT_DUEL_WHILE_STEALTHED",
    "CANT_STEALTH",
    "CASTER_AURASTATE",
    "CASTER_DEAD",
    "CHARMED",
    "CHEST_IN_USE",
    "CONFUSED",
    "DONT_REPORT",
    "EQUIPPED_ITEM",
    "EQUIPPED_ITEM_CLASS",
    "EQUIPPED_ITEM_CLASS_MAINHAND",
    "EQUIPPED_ITEM_CLASS_OFFHAND",
    "ERROR",
    "FIZZLE",
    "FLEEING",
    "FOOD_LOWLEVEL",
    "HIGHLEVEL",
    "HUNGER_SATIATED",
    "IMMUNE",
    "INTERRUPTED",
    "INTERRUPTED_COMBAT",
    "ITEM_ALREADY_ENCHANTED",
    "ITEM_GONE",
    "ITEM_NOT_FOUND",
    "ITEM_NOT_READY",
    "LEVEL_REQUIREMENT",
    "LINE_OF_SIGHT",
    "LOWLEVEL",
    "LOW_CASTLEVEL",
    "MAINHAND_EMPTY",
    "MOVING",
    "NEED_AMMO",
    "NEED_AMMO_POUCH",
    "NEED_EXOTIC_AMMO",
    "NOPATH",
    "NOT_BEHIND",
    "NOT_FISHABLE",
    "NOT_HERE",
    "NOT_INFRONT",
    "NOT_IN_CONTROL",
    "NOT_KNOWN",
    "NOT_MOUNTED",
    "NOT_ON_TAXI",
    "NOT_ON_TRANSPORT",
    "NOT_READY",
    "NOT_SHAPESHIFT",
    "NOT_STANDING",
    "NOT_TRADEABLE",
    "NOT_TRADING",
    "NOT_UNSHEATHED",
    "NOT_WHILE_GHOST",
    "NO_AMMO",
    "NO_CHARGES_REMAIN",
    "NO_CHAMPION",
    "NO_COMBO_POINTS",
    "NO_DUELING",
    "NO_ENDURANCE",
    "NO_FISH",
    "NO_ITEMS_WHILE_SHAPESHIFTED",
    "NO_MOUNTS_ALLOWED",
    "NO_PET",
    "NO_POWER",
    "NOTHING_TO_DISPEL",
    "NOTHING_TO_STEAL",
    "ONLY_ABOVEWATER",
    "ONLY_DAYTIME",
    "ONLY_INDOORS",
    "ONLY_MOUNTED",
    "ONLY_NIGHTTIME",
    "ONLY_OUTDOORS",
    "ONLY_SHAPESHIFT",
    "ONLY_STEALTHED",
    "ONLY_UNDERWATER",
    "OUT_OF_RANGE",
    "PACIFIED",
    "POSSESSED",
    "REAGENTS",
    "REQUIRES_AREA",
    "REQUIRES_SPELL_FOCUS",
    "ROOTED",
    "SILENCED",
    "SPELL_IN_PROGRESS",
    "SPELL_LEARNED",
    "SPELL_UNAVAILABLE",
    "STUNNED",
    "TARGETS_DEAD",
    "TARGET_AFFECTING_COMBAT",
    "TARGET_AURASTATE",
    "TARGET_DUELING",
    "TARGET_ENEMY",
    "TARGET_ENRAGED",
    "TARGET_FRIENDLY",
    "TARGET_IN_COMBAT",
    "TARGET_IS_PLAYER",
    "TARGET_NOT_DEAD",
    "TARGET_NOT_IN_PARTY",
    "TARGET_NOT_LOOTED",
    "TARGET_NOT_PLAYER",
    "TARGET_NO_POCKETS",
    "TARGET_NO_WEAPONS",
    "TARGET_UNSKINNABLE",
    "THIRST_SATIATED",
    "TOO_CLOSE",
    "TOO_MANY_OF_ITEM",
    "TOTEMS",
    "TRAINING_POINTS",
    "TRY_AGAIN",
    "UNIT_NOT_BEHIND",
    "UNIT_NOT_INFRONT",
    "WRONG_PET_FOOD",
    "NOT_WHILE_FATIGUED",
    "TARGET_NOT_IN_INSTANCE",
    "NOT_WHILE_TRADING",
    "TARGET_NOT_IN_RAID",
    "DISENCHANT_WHILE_LOOTING",
    "PROSPECT_WHILE_LOOTING",
    "PROSPECT_NEED_MORE",
    "TARGET_FREEFORALL",
    "NO_EDIBLE_CORPSES",
    "ONLY_BATTLEGROUNDS",
    "TARGET_NOT_GHOST",
    "TOO_MANY_SKILLS",
    "TRANSFORM_UNUSABLE",
    "WRONG_WEATHER",
    "DAMAGE_IMMUNE",
    "PREVENTED_BY_MECHANIC",
    "PLAY_TIME",
    "REPUTATION",
    "MIN_SKILL",
    "UNKNOWN",
};
inline constexpr std::size_t kCount = sizeof(kNames) / sizeof(kNames[0]);   // 146: AFFECTING_COMBAT (0x00) .. UNKNOWN
inline constexpr std::uint8_t kCastOk = 0xFF;                                // SPELL_CAST_OK: no failure code was sent

// nullptr for SPELL_CAST_OK and for codes outside the table
inline char const* Name(std::uint8_t code)
{
    return code < kCount ? kNames[code] : nullptr;
}

// Spell::m_spellState when finish() ran. "null" = the spell was never prepared: a bot-side cancel before any server check
// (PlayerbotAI::CastSpell, e.g. while moving), which sends no cast result.
inline char const* StateName(std::uint8_t state)
{
    switch (state)
    {
        case 0: return "null";
        case 1: return "preparing";
        case 2: return "casting";
        case 3: return "finished";
        case 4: return "idle";
        case 5: return "delayed";
        default: return "?";
    }
}
}
}
