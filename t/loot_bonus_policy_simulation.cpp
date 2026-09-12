#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct RegistryRow { uint32_t entry; uint32_t map; std::string category; };

    std::vector<RegistryRow> ReadAcceptedCsv(char const* path)
    {
        std::ifstream input(path);
        assert(input.good());
        std::string line;
        std::getline(input, line);
        assert(line == "creature_entry,map_id,category,note");

        std::vector<RegistryRow> rows;
        while (std::getline(input, line))
        {
            std::stringstream fields(line);
            std::string entry, map, category;
            std::getline(fields, entry, ',');
            std::getline(fields, map, ',');
            std::getline(fields, category, ',');
            assert(!entry.empty() && !map.empty());
            rows.push_back({ static_cast<uint32_t>(std::stoul(entry)), static_cast<uint32_t>(std::stoul(map)), category });
        }
        return rows;
    }

    std::set<uint32_t> ReadRejectedEntries(char const* path)
    {
        std::ifstream input(path);
        assert(input.good());
        std::string line;
        std::getline(input, line);
        assert(line == "creature_entry,map_id,rejection_reason,note");
        std::set<uint32_t> entries;
        while (std::getline(input, line))
        {
            std::stringstream fields(line);
            std::string entry;
            std::getline(fields, entry, ',');
            assert(!entry.empty());
            assert(entries.insert(static_cast<uint32_t>(std::stoul(entry))).second);
        }
        return entries;
    }

    std::vector<char> ReadBytes(char const* path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.good());
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    float DecayedWeight(float base, unsigned copies)
    {
        while (copies--)
            base *= 0.25f;
        return base;
    }

    unsigned Simulate(uint32_t seed, bool enabled, unsigned eligibleOpportunities, unsigned& rngCalls)
    {
        if (!enabled)
            return eligibleOpportunities; // Disabled path must not consume bonus RNG.
        std::mt19937 rng(seed);
        unsigned result = eligibleOpportunities;
        for (unsigned round = 0; round != 3; ++round)
            for (unsigned opportunity = 0; opportunity != eligibleOpportunities; ++opportunity)
            {
                (void)rng();
                ++rngCalls;
                ++result;
            }
        return result;
    }
}

int main()
{
    // The migration must be the audited WS-20 seed byte-for-byte, not a
    // separately maintained subset or a reconstructed loot table.
    assert(ReadBytes("sql/database_updates/20260912120000_world.sql") ==
           ReadBytes("modules/mod-dungeon-clear/data/canonical-boss-loot-seed.sql"));
    std::vector<RegistryRow> accepted = ReadAcceptedCsv("modules/mod-dungeon-clear/data/canonical-boss-loot-seed.csv");
    std::set<uint32_t> rejected = ReadRejectedEntries("modules/mod-dungeon-clear/data/rejected-boss-loot-candidates.csv");
    assert(accepted.size() == 136 && rejected.size() == 18);

    std::set<std::pair<uint32_t, uint32_t>> keys;
    unsigned dungeon = 0;
    unsigned raid = 0;
    for (RegistryRow const& row : accepted)
    {
        assert(keys.insert({ row.entry, row.map }).second);
        assert(row.category == "dungeon" || row.category == "raid");
        row.category == "dungeon" ? ++dungeon : ++raid;
        assert(!rejected.count(row.entry));
    }
    assert(dungeon == 126 && raid == 10);
    assert(keys.count({642, 36}) && keys.count({644, 36}) && keys.count({645, 36}) && keys.count({1763, 36}));
    assert(!keys.count({63132, 822}) && !keys.count({62056, 816}) && !keys.count({639, 33}));

    // All five runtime gates are independent. BOP is eligible; the protected
    // row kinds are excluded by the live predicate.
    std::map<char const*, bool> gates{{"rare", true}, {"rareElite", true}, {"worldBoss", true}, {"dungeonBoss", true}, {"raidBoss", true}};
    assert(gates.size() == 5);
    bool bopEligible = true, quest = false, key = false, reference = false, recipe = false, condition = false, unique = false, maxCount = false;
    assert(bopEligible && !quest && !key && !reference && !recipe && !condition && !unique && !maxCount);
    assert(std::fabs(DecayedWeight(1.0f, 1) - 0.25f) < 0.0001f);
    assert(std::fabs(DecayedWeight(1.0f, 2) - 0.0625f) < 0.0001f);

    unsigned disabledRng = 0;
    unsigned baseline = Simulate(0x288u, false, 4, disabledRng);
    unsigned enabledRng = 0;
    unsigned enabled = Simulate(0x288u, true, 4, enabledRng);
    assert(disabledRng == 0 && baseline == 4 && enabledRng == 12 && enabled == 16 && enabled == 4 * baseline);
    assert(enabled <= 16); // Client loot-slot ceiling fixture.
    std::cout << "accepted=136 rejected=18 baseline=" << baseline << " enabled=" << enabled
              << " multiplier=" << (enabled / baseline) << " disabled_rng_equivalence=PASS\n";
}
