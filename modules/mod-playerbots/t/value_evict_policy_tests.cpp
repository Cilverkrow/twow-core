#include "ValueEvictPolicy.h"

#include <cstdlib>
#include <iostream>
#include <map>

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

// Today's ClearValues filter: every created name with find(prefix) == 0.
std::vector<std::string> Naive(std::map<std::string, int> const& map, std::string const& prefix)
{
    std::vector<std::string> out;
    for (auto const& entry : map)
        if (prefix.empty() || entry.first.find(prefix) == 0)
            out.push_back(entry.first);
    return out;
}

std::vector<std::string> Fast(std::map<std::string, int> const& map, std::string const& prefix)
{
    std::vector<std::string> out;
    ai::value_evict::AppendKeysWithPrefix(map, prefix, out);
    return out;
}
}

int main()
{
    using namespace ai::value_evict;

    // F1: only unprotected values that expired are dropped.
    Require(Evictable(false, true), "an idle calculated value is dropped");
    Require(!Evictable(false, false), "a value calculated recently stays");
    Require(!Evictable(true, true), "a protected (memory / log) value stays");

    EvictClock clock;
    Require(!clock.Due(1000, 50), "the first call only schedules (staggered)");
    Require(!clock.Due(1049, 50), "not before the stagger");
    Require(clock.Due(1050, 50), "due at the stagger");
    Require(!clock.Due(1649, 50), "then every ten minutes");
    Require(clock.Due(1650, 50), "ten minutes later");
    EvictClock other;
    other.Due(1000, 650);
    Require(other.next == 1050, "stagger wraps at the interval");

    // F2: the prefix search returns exactly what the old filter returned.
    std::map<std::string, int> created = {
        { "attackers", 1 }, { "item usage", 1 }, { "item usage::6948", 1 }, { "item usage::7", 1 },
        { "item usagex", 1 }, { "item", 1 }, { "no active travel destinations::quest", 1 },
        { "no active travel destinations::rpg", 1 }, { "zz", 1 },
    };
    for (std::string const prefix : { "", "item usage", "item", "no active travel destinations", "zz", "zzz", "a", "q" })
        Require(Fast(created, prefix) == Naive(created, prefix), "prefix search equals the old filter");
    Require(Fast(created, "no active travel destinations").size() == 2, "both travel-destination flags");

    // F3: the stable target key has GuidPosition::to_string()'s format, without coordinates.
    Require(StableTargetQualifier(1, 1234567890123ULL) == "1|0|0|0|0|1234567890123", "map and guid only");

    std::cout << "value_evict_policy_tests passed\n";
    return 0;
}
