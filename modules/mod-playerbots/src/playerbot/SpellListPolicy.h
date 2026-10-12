#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ai::spelllists
{
// twow-repo#541 (audit A16, AiPlayerbot.Perf.StaticSpellLists, default 0 = off): the castability checks in
// PlayerbotAI.cpp (CheckSpellTargetAlignment, CanCastSpell for unit, game object and position)
// built a const std::list<uint32> of 20 or 24 heap nodes on every call, only to ask whether one
// spell id is in it. The same ids as constant arrays; membership is the same linear search.
// With the switch off the legacy literal lists stay in PlayerbotAI.cpp; both must hold the same
// ids in the same order (t/static_spell_lists_source_contract_tests.cmake).

// Spells that are neither positive nor negative (feign death, hunter traps, ...).
constexpr std::array<std::uint32_t, 20> NeutralSpellIds = {{ 1499, 5384, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317, 27023, 27025, 34600, 49055, 49056, 49066, 49067 }};

// Spells a bot may cast while it has lost control (UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL).
constexpr std::array<std::uint32_t, 24> OutOfControlSpellIds = {{ 642, 1020, 1499, 1953, 7744, 11958, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317, 27023, 27025, 34600, 49055, 49056, 49066, 49067 }};

template <std::size_t N>
constexpr bool Contains(std::array<std::uint32_t, N> const& ids, std::uint32_t id)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (ids[i] == id)
            return true;
    }
    return false;
}

constexpr bool IsNeutralSpell(std::uint32_t spellId) { return Contains(NeutralSpellIds, spellId); }
constexpr bool IsCastableOutOfControl(std::uint32_t spellId) { return Contains(OutOfControlSpellIds, spellId); }
}
