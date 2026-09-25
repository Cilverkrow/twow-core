#include "DestinationDeathPolicy.h"

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
    using namespace ai::destination_death;
    std::uint32_t const cooldown = 3600000;

    // Live: one bot died 60 times at the same fishing spot in six hours.
    Record spot;
    Require(!RecordDeath(spot, 1000, 2, cooldown), "first death only counts");
    Require(!IsSuppressed(spot, 1001), "one death does not suppress");
    Require(RecordDeath(spot, 2000, 2, cooldown), "second death suppresses the spot");
    Require(IsSuppressed(spot, 2001), "spot is skipped during the cooldown");
    Require(!IsSuppressed(spot, 2000 + cooldown), "spot opens again after the cooldown");

    // After the cooldown the count restarts rather than suppressing at once.
    Require(!RecordDeath(spot, 3000 + cooldown, 2, cooldown), "count restarts after the cooldown");

    Record off;
    for (std::uint32_t i = 0; i < 10; ++i)
        Require(!RecordDeath(off, i, 0, cooldown), "0 disables the rule");
    return 0;
}
