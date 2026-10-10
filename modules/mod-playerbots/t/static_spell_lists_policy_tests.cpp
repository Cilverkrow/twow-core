#include "SpellListPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <list>

// twow-repo#541 (audit A16): the constant arrays answer every membership question exactly like the
// per-call std::list literals they replace (AiPlayerbot.Perf.StaticSpellLists).

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Verbatim copies of the legacy literals in PlayerbotAI.cpp (the source contract keeps those in
// line with SpellListPolicy.h).
std::list<std::uint32_t> const LegacyNeutral = { 1499, 5384, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317, 27023, 27025, 34600, 49055, 49056, 49066, 49067 };
std::list<std::uint32_t> const LegacyOutOfControl = { 642, 1020, 1499, 1953, 7744, 11958, 13795, 13809, 13813, 14302, 14303, 14304, 14305, 14310, 14311, 14316, 14317, 27023, 27025, 34600, 49055, 49056, 49066, 49067 };

bool InLegacy(std::list<std::uint32_t> const& ids, std::uint32_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}

// Compile-time spot checks.
static_assert(ai::spelllists::IsNeutralSpell(5384), "feign death is neutral");
static_assert(!ai::spelllists::IsNeutralSpell(642), "divine shield is not neutral");
static_assert(ai::spelllists::IsCastableOutOfControl(642), "divine shield is castable out of control");
static_assert(!ai::spelllists::IsCastableOutOfControl(5384), "feign death is not in the out-of-control list");
static_assert(!ai::spelllists::IsNeutralSpell(0) && !ai::spelllists::IsCastableOutOfControl(0), "spell id 0 is in neither list");

int main()
{
    using namespace ai::spelllists;

    Require(NeutralSpellIds.size() == LegacyNeutral.size(), "neutral: same number of ids");
    Require(OutOfControlSpellIds.size() == LegacyOutOfControl.size(), "out of control: same number of ids");
    Require(std::equal(NeutralSpellIds.begin(), NeutralSpellIds.end(), LegacyNeutral.begin()), "neutral: same ids, same order");
    Require(std::equal(OutOfControlSpellIds.begin(), OutOfControlSpellIds.end(), LegacyOutOfControl.begin()), "out of control: same ids, same order");

    // Exhaustive over every id the 1.12 spell tables and the custom ranges can hold, plus the edges.
    for (std::uint32_t id = 0; id <= 100000; ++id)
    {
        Require(IsNeutralSpell(id) == InLegacy(LegacyNeutral, id), "neutral: membership differs from the legacy list");
        Require(IsCastableOutOfControl(id) == InLegacy(LegacyOutOfControl, id), "out of control: membership differs from the legacy list");
    }
    for (std::uint32_t id : {0xFFFFFFFFu, 0x80000000u, 49067u + 0x10000u})
    {
        Require(IsNeutralSpell(id) == InLegacy(LegacyNeutral, id), "neutral: high ids");
        Require(IsCastableOutOfControl(id) == InLegacy(LegacyOutOfControl, id), "out of control: high ids");
    }

    std::cout << "static_spell_lists_policy: OK\n";
    return 0;
}
