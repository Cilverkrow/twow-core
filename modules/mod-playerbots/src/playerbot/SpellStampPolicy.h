#pragma once

// twow-repo#541 (audit A27, AiPlayerbot.CastSpell.RefreshOnLearn, default 0, behaviour-changing):
// CastSpellAction reads its spell id and range once, when a strategy first pushes the action, and actions
// live for the whole session. A spell or rank learned later stayed unknown to isPossible() (id 0 refused
// every non-self target) until the next login. With the switch on, the action compares a cheap per-bot
// "known-spell stamp" at the start of isPossible() and re-reads id and range only when the stamp changed.
//
// This header is the pure part: no core includes, so t/spell_refresh_on_learn_policy_tests.cpp tests it
// without the game library.

#include <cstddef>
#include <cstdint>

namespace ai
{
namespace spellstamp
{
    // Stamp layout (bit 63 is always set, so 0 stays free for "not stamped yet"):
    //   bits 39..62  pet number (24 bits, the pet guid's entry field; only for pet spell actions, else 0)
    //   bits 27..38  free talent points (12 bits): a talent rank swap keeps the spell map size
    //   bits 19..26  level (8 bits): breaks the "+1 point, spent on a NEW talent rank" ABA of size/points
    //   bits  0..18  size of the bot's spell map (19 bits): every newly known spell or rank adds a key
    // It is a state signature, not a generation: a learn plus a save-time erase between two evaluations of
    // the same action (same level and points, e.g. at level 60) can cancel out until the next change.
    inline std::uint64_t Make(std::size_t spellCount, std::uint32_t level, std::uint32_t freeTalentPoints, std::uint32_t petNumber)
    {
        return (std::uint64_t(1) << 63)
             | (std::uint64_t(petNumber & 0xFFFFFFu) << 39)
             | (std::uint64_t(freeTalentPoints & 0xFFFu) << 27)
             | (std::uint64_t(level & 0xFFu) << 19)
             | (std::uint64_t(spellCount) & 0x7FFFFu);
    }

    // Only pet spell actions (warlock Sacrifice / Spell Lock) pass a pet number; every other spell action passes 0,
    // because it resolves from the bot's own spell map, so taxi rides, teleports, map changes, pet deaths and
    // demon swaps do not make it re-read anything.
    //
    // A pet spell action does not refresh while no pet is out: its isPossible() is false without a pet anyway,
    // and a refresh then would drop the id to 0 and re-read it when the same pet comes back (N -> 0 -> N).
    inline bool DeferWithoutPet(bool tracksPet, std::uint32_t petNumber)
    {
        return tracksPet && petNumber == 0u;
    }

    // The range follows the spell only while nobody replaced it since it was last taken from the spell
    // (melee ATTACK_DISTANCE, judgements/blind 10, shouts 8 and vehicles keep their own), and never while it
    // is the melee marker itself: isPossible() treats range == ATTACK_DISTANCE as "melee check", which a
    // relog would always restore for the melee subclasses. The comparison is exact on purpose: both sides
    // hold the same stored float.
    inline bool RangeFollowsSpell(float range, float rangeFromSpell, float meleeRange)
    {
        return range == rangeFromSpell && range != meleeRange;
    }
}
}
