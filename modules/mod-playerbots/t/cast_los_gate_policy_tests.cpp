// twow-repo#602 ROT-01 hunter LoS (focus check 25.10.2026): AiPlayerbot.Rotation.CastLosGate - a LINE_OF_SIGHT verdict makes the
// cast IMPOSSIBLE; target selection skips an 8.2-unreachable unit only while another candidate exists.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "CastLosGatePolicy.h"

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
    using namespace ai::cast_los_gate;

    Require(!Applies(0, 8) && !Applies(0, 9), "mode 0 applies to nobody");
    Require(Applies(1, 8) && Applies(1, 9), "mode 1 applies to everybody");
    Require(!Applies(2, 8) && Applies(2, 9), "mode 2 applies to cohort B only");
    Require(!Applies(5, 9), "an unknown mode applies to nobody");

    Require(SkipUnreachable(true, true, true), "unreachable with another candidate: skipped");
    Require(!SkipUnreachable(true, true, false), "unreachable but the only candidate: kept (the bot defends itself)");
    Require(!SkipUnreachable(true, false, true), "reachable: kept");
    Require(!SkipUnreachable(false, true, true), "switch off: kept");

    Require(gated.load() == 0 && dropped.load() == 0 && skipped.load() == 0, "counters start at 0");

    std::cout << "cast_los_gate_policy tests passed\n";
    return 0;
}
