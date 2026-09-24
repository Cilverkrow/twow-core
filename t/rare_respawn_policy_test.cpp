// twow-repo#298 acceptance values for the accelerated rare respawn policy.
#include "../src/game/FunserverRareRespawn.h"

#include <cstdint>
#include <iostream>

namespace
{
    int failures = 0;

    void Expect(uint32_t oldSeconds, uint32_t expected, char const* label)
    {
        uint32_t const actual = ScaleFunserverRareRespawnDelay(oldSeconds);
        if (actual != expected)
        {
            std::cerr << label << ": " << oldSeconds << "s -> " << actual << "s, expected " << expected << "s\n";
            ++failures;
        }
    }
}

int main()
{
    Expect(14 * 3600, 14 * 60, "14h -> 14m (Bruno, Grimmen Thresher)");
    Expect(27000, 450, "7h30 -> 7m30 (Highlord Mastrogonde)");
    Expect(22 * 3600, 22 * 60, "22h -> 22m");
    Expect(9000, 150, "2.5h -> 2.5m");
    Expect(24 * 3600, 1440, "24h -> 24m");
    Expect(7 * 86400, 1440, "7d capped at 24m");
    Expect(UINT32_MAX, 1440, "overflow-safe cap");
    Expect(300, 60, "short respawn floored at 60s");
    Expect(0, 60, "zero floored at 60s");
    Expect(3659, 60, "rounds down (60.98 -> 60s)");
    Expect(3719, 61, "rounds down (61.98 -> 61s)");

    // twow-repo#322: configurable divisor and bounds.
    auto expectCfg = [](uint32_t oldSeconds, uint32_t div, uint32_t lo, uint32_t hi, uint32_t expected, char const* label)
    {
        uint32_t const actual = ScaleFunserverRareRespawnDelay(oldSeconds, div, lo, hi);
        if (actual != expected)
        {
            std::cerr << label << ": " << oldSeconds << "s -> " << actual << "s, expected " << expected << "s\n";
            ++failures;
        }
    };
    expectCfg(7 * 86400, 60, 60, 7200, 7200, "7d capped at 2h when MaxSeconds=7200");
    expectCfg(3 * 86400, 60, 60, 7200, 4320, "3d -> 72m under 2h cap");
    expectCfg(14 * 3600, 120, 60, 1440, 420, "divisor 120: 14h -> 7m");
    expectCfg(1800, 60, 120, 1440, 120, "MinSeconds 120 floor");
    expectCfg(14 * 3600, 0, 60, 1440, 1440, "divisor 0 treated as 1, capped");

    if (failures)
        return 1;
    std::cout << "RARE_RESPAWN_POLICY=PASS\n";
    return 0;
}
