// twow-repo#588 (BUFF-01): stranger cooldown and guild rule for out-of-group buff targets.
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "BuffTargetPolicy.h"

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
    using namespace ai::buff_target;

    // Guild: only a real, shared guild counts.
    Require(SameGuild(12, 12), "same guild");
    Require(!SameGuild(12, 13), "negative: another guild");
    Require(!SameGuild(0, 0), "negative: two guildless bots are no guild");

    // Strangers: the first at once, then not before the cooldown ran out.
    std::uint32_t const now = 100000, cooldown = 900;
    Require(StrangerAllowed(now, 0), "never buffed a stranger: allowed");
    std::uint32_t const next = NextStrangerAllowed(now, cooldown);
    Require(!StrangerAllowed(now + 1, next) && !StrangerAllowed(now + cooldown - 1, next), "within the cooldown: no");
    Require(StrangerAllowed(now + cooldown, next), "after the cooldown: yes");

    // A busy place: a stranger without the buff in every evaluation (every 2 s) - at most 4 buffs per hour.
    std::uint32_t allowedAt = 0, buffs = 0;
    for (std::uint32_t t = now; t < now + 3600; t += 2)
        if (StrangerAllowed(t, allowedAt))
        {
            ++buffs;
            allowedAt = NextStrangerAllowed(t, cooldown);
        }
    Require(buffs == 4, "one stranger buff per 15 minutes");

    // Counters.
    Count(Category::Guild);
    Count(Category::Guild);
    Require(Counter(Category::Guild).exchange(0) == 2 && Counter(Category::Guild).load() == 0, "minute counter");

    std::cout << "buff_target_policy_tests passed\n";
    return 0;
}
