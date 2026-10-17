// twow-repo#596 D: the three slowest trigger checks of a pass, for the TIME entry of [BotUpdateSlow].
#include <cstdlib>
#include <iostream>

#include "TriggerTimePolicy.h"

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
    using namespace ai::trigger_time;

    // Morwenna (block 1, arm 32): a 36 ms trigger pass.
    Top top;
    Require(top.Format().empty(), "empty pass: nothing");
    top.Add("often", 12);
    top.Add("not::travel target active", 31000);
    top.Add("no possible targets", 900);
    top.Add("random", 0);   // 0 us: never listed
    top.Add("enemy too close for spell", 4100);
    Require(top.Format() == "not::travel target active:31000,enemy too close for spell:4100,no possible targets:900",
        "the three slowest, slowest first");
    top.Add("often", 5);   // slower than nothing in the top: ignored
    Require(top.Format() == "not::travel target active:31000,enemy too close for spell:4100,no possible targets:900", "unchanged");
    top.Add("rpg", 4100);  // equal to the second: stays behind it
    Require(top.Format() == "not::travel target active:31000,enemy too close for spell:4100,rpg:4100", "ties keep order");

    // Next pass starts empty.
    top.Clear();
    Require(top.Format().empty(), "cleared");
    top.Add("a", 7);
    Require(top.Format() == "a:7", "one check");

    Require(SlowPhaseUs == 20000 && TopCount == 3, "threshold and count");
    std::cout << "trigger_time_policy_tests passed\n";
    return 0;
}
