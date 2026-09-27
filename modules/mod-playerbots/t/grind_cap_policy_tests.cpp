#include "GrindCapPolicy.h"

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
    using namespace ai::grind_cap;

    // Live: level 3 goblin attacked level 6 Mudpaw Miners (+3) 123 times.
    Require(MaxLevelsAbove(true, 3, 10, 2) == 2, "low-level roster bot: +2 at most");
    Require(MaxLevelsAbove(true, 10, 10, 2) == 4, "from level 10 the old +4 applies");
    Require(MaxLevelsAbove(false, 3, 10, 2) == 4, "other bots unchanged");
    Require(MaxLevelsAbove(true, 3, 0, 2) == 4, "0 disables the rule");
    Require(MaxLevelsAbove(true, 3, 10, 6) == 4, "never looser than before");

    Record record;
    Require(!RecordDeath(record, 1000, 3, 3600, 3600), "first death counts");
    Require(!RecordDeath(record, 1100, 3, 3600, 3600), "second death counts");
    Require(RecordDeath(record, 1200, 3, 3600, 3600), "third death within the hour: avoid");
    Require(IsAvoided(record, 1201) && !IsAvoided(record, 1200 + 3600), "avoided for an hour");

    Record slow;
    Require(!RecordDeath(slow, 1000, 3, 3600, 3600), "one");
    Require(!RecordDeath(slow, 2000, 3, 3600, 3600), "two");
    Require(!RecordDeath(slow, 5000, 3, 3600, 3600), "window over: counting restarts");
    Require(!IsAvoided(slow, 5001), "deaths spread over hours do not avoid");

    // Bounded shared store: reads never create records.
    AvoidStore store;
    Require(!store.IsAvoided(7, 42, 1000) && store.Size() == 0, "a read creates nothing");
    Record state;
    Require(!store.RecordDeath(7, 42, 1000, 3, 3600, 3600, state), "store: first death");
    Require(!store.RecordDeath(7, 42, 1100, 3, 3600, 3600, state), "store: second death");
    Require(store.RecordDeath(7, 42, 1200, 3, 3600, 3600, state) && state.avoidUntil == 4800, "store: third death avoids");
    Require(store.IsAvoided(7, 42, 1300) && !store.IsAvoided(8, 42, 1300) && !store.IsAvoided(7, 43, 1300), "per bot and entry");
    Require(store.Size() == 1, "one record");
    for (std::uint32_t bot = 0; bot < AvoidStore::MaxRecords + 10; ++bot)
        store.RecordDeath(bot, 1, 100000, 3, 3600, 3600, state);
    Require(store.Size() <= AvoidStore::MaxRecords, "store stays bounded");

    Record off;
    for (int i = 0; i < 10; ++i)
        Require(!RecordDeath(off, 1000 + i, 0, 3600, 3600), "0 disables avoidance");
    return 0;
}
