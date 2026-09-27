#include "LoginWavePolicy.h"

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
    using namespace ai::login_wave;

    // Roster order: three in zone 12 (Elwynn), two in 141 (Teldrassil), one in 14.
    std::vector<Candidate> roster = { {1, 12}, {2, 12}, {3, 141}, {4, 12}, {5, 141}, {6, 14} };
    auto waves = AssignWaves(roster, 2);
    // Zones ascending (12, 14, 141), one bot per zone in turn: 1,6,3 | 2,5 | 4
    // -> index 0:1 1:6 2:3 3:2 4:5 5:4 -> waves 0,0,1,1,2,2
    Require(waves[1] == 0 && waves[6] == 0, "first wave mixes zones 12 and 14");
    Require(waves[3] == 1 && waves[2] == 1, "second wave: 141 then 12");
    Require(waves[5] == 2 && waves[4] == 2, "third wave: the rest");
    Require(waves.size() == 6, "every candidate gets a wave");
    Require(AssignWaves(roster, 2) == waves, "deterministic");
    Require(AssignWaves(roster, 0).empty(), "0 = no waves (everyone at once)");

    // Opening: wave n after n x interval.
    Require(IsOpen(0, 0, 900) && !IsOpen(1, 899, 900) && IsOpen(1, 900, 900), "wave borders");
    Require(OpenWaves(7, 0, 900) == 1 && OpenWaves(7, 900, 900) == 2 && OpenWaves(7, 99999, 900) == 7, "open waves");
    Require(OpenWaves(0, 100, 900) == 0, "no waves");
    return 0;
}
