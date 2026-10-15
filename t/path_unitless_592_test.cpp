// twow-repo#592 (PATH-02, PathFinder.Unitless): counting of the PathInfo calls that fail closed without
// a Unit, the one-time flag per reason and the [PathUnitless] minute line. The wiring into PathInfo is
// pinned by t/check_path_unitless_592_contract.cmake.
#include "../src/game/Maps/PathUnitless.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void Expect(bool condition, char const* label)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << label << "\n";
            ++failures;
        }
    }

    void ExpectText(std::string const& actual, std::string const& expected, char const* label)
    {
        if (actual != expected)
        {
            std::cerr << label << ": got '" << actual << "', expected '" << expected << "'\n";
            ++failures;
        }
    }

    // Static storage like the counters in PathFinder.cpp.
    path_unitless::Counters counters;
}

int main()
{
    using namespace path_unitless;

    // Names in the lines.
    ExpectText(ReasonName(REJECT_DISABLED), "disabled", "name disabled");
    ExpectText(ReasonName(REJECT_CALC3), "calc3", "name calc3");
    ExpectText(ReasonName(REJECT_CORE_ONLY), "core_only", "name core_only");
    ExpectText(ReasonName(REJECT_NO_TERRAIN), "no_terrain", "name no_terrain");
    ExpectText(ReasonName(REJECT_TRANSPORT), "transport", "name transport");

    // Nothing counted: no minute line at all.
    Totals const empty = counters.Take();
    Expect(empty.Sum() == 0, "starts at zero");
    ExpectText(FormatMinuteLine(empty), "", "no line without rejects");

    // The first reject of a reason asks for the one-time line, later ones do not.
    Expect(counters.Add(REJECT_DISABLED), "first disabled -> one-time line");
    Expect(!counters.Add(REJECT_DISABLED), "second disabled -> no line");
    Expect(!counters.Add(REJECT_DISABLED), "third disabled -> no line");
    Expect(counters.Add(REJECT_TRANSPORT), "first transport -> its own one-time line");
    Expect(counters.Add(REJECT_NO_TERRAIN), "first no_terrain -> its own one-time line");
    Expect(!counters.Add(REJECT_REASON_COUNT), "out of range is ignored");

    Totals const first = counters.Take();
    Expect(first.count[REJECT_DISABLED] == 3, "3 disabled");
    Expect(first.count[REJECT_TRANSPORT] == 1, "1 transport");
    Expect(first.count[REJECT_NO_TERRAIN] == 1, "1 no_terrain");
    Expect(first.count[REJECT_CALC3] == 0 && first.count[REJECT_CORE_ONLY] == 0, "others 0");
    Expect(first.Sum() == 5, "sum 5");
    ExpectText(FormatMinuteLine(first),
        "[PathUnitless] rejects=5 disabled=3 calc3=0 core_only=0 no_terrain=1 transport=1", "minute line");

    // Take() starts the next interval at zero; the one-time flag stays set.
    Expect(counters.Take().Sum() == 0, "taken counts are reset");
    Expect(!counters.Add(REJECT_DISABLED), "one-time line only once since the start");
    Expect(counters.Add(REJECT_CALC3), "first calc3");
    Expect(counters.Add(REJECT_CORE_ONLY), "first core_only");
    ExpectText(FormatMinuteLine(counters.Take()),
        "[PathUnitless] rejects=3 disabled=1 calc3=1 core_only=1 no_terrain=0 transport=0", "second interval");

    // Large counts keep their digits.
    Totals big;
    big.count[REJECT_DISABLED] = 4000000000ull;
    big.count[REJECT_TRANSPORT] = 1;
    ExpectText(FormatMinuteLine(big),
        "[PathUnitless] rejects=4000000001 disabled=4000000000 calc3=0 core_only=0 no_terrain=0 transport=1", "64-bit counts");

    if (failures)
        return 1;
    std::cout << "path_unitless_592 tests passed\n";
    return 0;
}
