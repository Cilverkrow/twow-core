// twow-repo#602 ROT-01 (L1-b, OB-10): names of the SpellCastResult codes for the action log's CAST_FAIL reason=.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "CastResultNames.h"

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

bool Is(char const* got, char const* want)
{
    return got && std::strcmp(got, want) == 0;
}
}

int main()
{
    using namespace ai::cast_result;

    Require(kCount == 146, "146 codes, AFFECTING_COMBAT (0x00) .. UNKNOWN (0x91)");
    Require(Is(Name(0x00), "AFFECTING_COMBAT"), "code 0 is a failure code, not success");
    Require(Is(Name(0x2A), "LINE_OF_SIGHT"), "LINE_OF_SIGHT = 0x2A");
    Require(Is(Name(0x61), "SPELL_IN_PROGRESS"), "SPELL_IN_PROGRESS = 0x61 (the L1-b Smite storm)");
    Require(Is(Name(0x91), "UNKNOWN"), "last code");
    Require(Name(0x92) == nullptr && Name(0xFE) == nullptr, "codes past the table have no name");
    Require(Name(kCastOk) == nullptr, "SPELL_CAST_OK = none sent");

    Require(Is(StateName(0), "null"), "never prepared = bot-side cancel");
    Require(Is(StateName(1), "preparing") && Is(StateName(2), "casting") && Is(StateName(5), "delayed"), "spell states");
    Require(Is(StateName(9), "?"), "unknown state");

    for (std::size_t i = 0; i < kCount; ++i)
        Require(kNames[i] && kNames[i][0] != '\0' && std::strncmp(kNames[i], "SPELL_FAILED_", 13) != 0, "names without the prefix");

    std::cout << "cast_result_names_tests passed\n";
    return 0;
}
