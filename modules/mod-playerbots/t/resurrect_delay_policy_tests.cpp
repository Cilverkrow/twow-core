#include "ResurrectDelayPolicy.h"

#include <cstdlib>
#include <iostream>

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
}

int main()
{
    using namespace ai::resurrect_delay;

    // Loader: 0..3 kept, everything else off.
    Require(ClampMode(0) == 0 && ClampMode(1) == 1 && ClampMode(2) == 2 && ClampMode(3) == 3, "modes 0..3 kept");
    Require(ClampMode(4) == 0 && ClampMode(-1) == 0 && ClampMode(100) == 0, "unknown mode acts like 0");

    // Guid bit 1: 0 -> clear, 1 -> clear, 2 -> set, 3 -> set, 4 -> clear, 6 -> set.
    Require(!Bit1Set(0) && !Bit1Set(1) && Bit1Set(2) && Bit1Set(3) && !Bit1Set(4) && Bit1Set(6), "bit 1 of the low guid");

    for (std::uint32_t guid = 0; guid < 64; ++guid)
    {
        Require(!Applies(0, guid), "mode 0: never");
        Require(Applies(1, guid), "mode 1: every bot");
        Require(Applies(2, guid) == Bit1Set(guid), "mode 2: only bit 1 set");
        Require(Applies(3, guid) == !Bit1Set(guid), "mode 3: only bit 1 clear");
        Require(Applies(2, guid) != Applies(3, guid), "modes 2 and 3 split every bot exactly once");
        Require(!Applies(7, guid), "a value the loader would clamp does nothing here either");
    }

    // Orthogonal to the CohortPolicy parity (bit 0): in every block of 4 guids each (parity, bit 1) pair occurs once,
    // so the H3 groups hold half of each T30 cohort (balanced 2x2).
    int cells[2][2] = {{0, 0}, {0, 0}};
    for (std::uint32_t guid = 1000; guid < 1400; ++guid)
        ++cells[guid & 1u][Applies(2, guid) ? 1 : 0];
    Require(cells[0][0] == 100 && cells[0][1] == 100 && cells[1][0] == 100 && cells[1][1] == 100, "balanced 2x2 with parity");

    std::cout << "resurrect_delay_policy_tests: OK\n";
    return 0;
}
