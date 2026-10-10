#include "KnownSpellGatePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// twow-repo#541 (audit A22): pure policy of the party buff/cure known-spell gate
// (AiPlayerbot.Perf.PartyBuffKnownSpellGate).
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

// Stand-in for the bot's own "spell id::<name>" value: 0 = no rank known. Records every lookup.
struct CountingLookup
{
    std::map<std::string, std::uint32_t> known;
    std::vector<std::string> calls;

    std::uint32_t operator()(char const* spell)
    {
        calls.push_back(spell);
        auto const it = known.find(spell);
        return it == known.end() ? 0 : it->second;
    }
};
}

int main()
{
    using ai::knownspell::MayScanParty;

    // (1) Switch off, all spells unknown: scan allowed, no lookup at all.
    {
        CountingLookup lookup;
        Require(MayScanParty(false, {"divine spirit"}, lookup), "off allows the scan");
        Require(lookup.calls.empty(), "off does no lookup");
    }

    // (2) Switch on with an empty list: scan allowed, no lookup.
    {
        CountingLookup lookup;
        Require(MayScanParty(true, {}, lookup), "empty list allows the scan");
        Require(lookup.calls.empty(), "empty list does no lookup");
    }

    // (3) On, spell unknown: no scan, exactly one lookup.
    {
        CountingLookup lookup;
        Require(!MayScanParty(true, {"divine spirit"}, lookup), "unknown spell blocks the scan");
        Require(lookup.calls.size() == 1 && lookup.calls[0] == "divine spirit", "one lookup of the listed spell");
    }

    // (4) On, spell known: scan allowed.
    {
        CountingLookup lookup;
        lookup.known["divine spirit"] = 14752;
        Require(MayScanParty(true, {"divine spirit"}, lookup), "known spell allows the scan");
    }

    // (5) On, only the second (alternative) spell known: scan allowed, looked up in list order.
    {
        CountingLookup lookup;
        lookup.known["purify"] = 1152;
        Require(MayScanParty(true, {"cleanse", "purify"}, lookup), "a known alternative allows the scan");
        Require(lookup.calls.size() == 2 && lookup.calls[0] == "cleanse" && lookup.calls[1] == "purify", "list order");
    }

    // (6) On, first spell known: stops after one lookup.
    {
        CountingLookup lookup;
        lookup.known["cleanse"] = 4987;
        Require(MayScanParty(true, {"cleanse", "purify"}, lookup), "first known spell allows the scan");
        Require(lookup.calls.size() == 1, "lazy: stops at the first known spell");
    }

    // (7) On, nothing known: no scan, every spell looked up once.
    {
        CountingLookup lookup;
        Require(!MayScanParty(true, {"a", "b", "c"}, lookup), "nothing known blocks the scan");
        Require(lookup.calls.size() == 3, "each spell looked up once");
    }

    // (8) Only id 0 counts as unknown; any nonzero id counts as known.
    {
        Require(!MayScanParty(true, {"x"}, [](char const*) -> std::uint32_t { return 0; }), "id 0 = unknown");
        Require(MayScanParty(true, {"x"}, [](char const*) -> std::uint32_t { return 1; }), "id 1 = known");
        Require(MayScanParty(true, {"x"}, [](char const*) -> std::uint32_t { return 0xFFFFFFFFu; }), "max id = known");
    }

    std::cout << "known_spell_gate_policy tests passed\n";
    return 0;
}
