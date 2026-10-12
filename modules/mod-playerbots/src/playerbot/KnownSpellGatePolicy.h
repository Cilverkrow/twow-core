#pragma once

#include <initializer_list>

// twow-repo#541 (audit A22, AiPlayerbot.Perf.PartyBuffKnownSpellGate, default 0, behaviour change):
// a party buff/cure trigger whose pushed action (and every ACTION_NODE_A alternative of it) needs a
// spell the bot does not know ends IMPOSSIBLE today, after a full party scan. The gate allows the scan
// when the switch is off, when no spell list is given, or when at least one listed spell resolves to
// a known spell id. Pure: the caller passes the bot's own "spell id" lookup, which reads the same Value
// object that PlayerbotAI::CanCastSpell(name) reads. Tested in t/known_spell_gate_policy_tests.cpp.
namespace ai
{
namespace knownspell
{
template <typename SpellIdOf>
inline bool MayScanParty(bool gateEnabled, std::initializer_list<char const*> spells, SpellIdOf&& spellIdOf)
{
    if (!gateEnabled || spells.size() == 0)
        return true;

    for (char const* spell : spells)
    {
        if (spellIdOf(spell) != 0)
            return true;
    }

    return false;
}
}
}
