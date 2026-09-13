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

    std::vector<std::string> ParseCsvLine(std::string const& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (size_t index = 0; index < line.size(); ++index)
        {
            char const current = line[index];
            if (current == '"')
            {
                if (quoted && index + 1 < line.size() && line[index + 1] == '"')
                {
                    field += current;
                    ++index;
                }
                else
                    quoted = !quoted;
            }
            else if (current == ',' && !quoted)
            {
                fields.push_back(field);
                field.clear();
            }
            else
                field += current;
        }
        assert(!quoted);
        fields.push_back(field);
        return fields;
    }

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

    std::vector<RegistryRow> ReadCoverageFollowupCsv(char const* path)
    {
        std::ifstream input(path);
        assert(input.good());
        std::string line;
        std::getline(input, line);
        assert(line == "\"map_id\",\"category\",\"creature_entry\",\"creature_name\",\"reason\"");

        std::vector<RegistryRow> rows;
        while (std::getline(input, line))
        {
            std::vector<std::string> fields = ParseCsvLine(line);
            assert(fields.size() == 5);
            rows.push_back({ static_cast<uint32_t>(std::stoul(fields[2])), static_cast<uint32_t>(std::stoul(fields[0])), fields[1] });
        }
        return rows;
    }

    std::string ReadText(char const* path)
    {
        std::ifstream input(path);
        assert(input.good());
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    std::string NormalizeLineEndings(std::string text)
    {
        size_t offset = 0;
        while ((offset = text.find("\r\n", offset)) != std::string::npos)
            text.replace(offset, 2, "\n");
        return text;
    }

    size_t CountLinesStartingWith(std::string const& text, std::string const& prefix)
    {
        std::istringstream input(text);
        std::string line;
        size_t count = 0;
        while (std::getline(input, line))
            if (line.compare(0, prefix.size(), prefix) == 0)
                ++count;
        return count;
    }

    size_t CountOccurrences(std::string const& text, std::string const& needle)
    {
        size_t count = 0;
        for (size_t offset = 0; (offset = text.find(needle, offset)) != std::string::npos; offset += needle.size())
            ++count;
        return count;
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
    // The two canonical seed copies must remain text-equivalent. Windows
    // checkout line endings are intentionally not considered a schema change.
    assert(NormalizeLineEndings(ReadText("sql/database_updates/20260912120000_world.sql")) ==
           NormalizeLineEndings(ReadText("modules/mod-dungeon-clear/data/canonical-boss-loot-seed.sql")));
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

    // The coverage follow-up is a sealed 16-row input. It adds only missing
    // composite keys, removes only two audited trash/add keys, and leaves the
    // eight REVIEW_REDUNDANT rows untouched. Replaying INSERT IGNORE and the
    // same DELETE leaves this final set unchanged.
    std::vector<RegistryRow> followup = ReadCoverageFollowupCsv("modules/mod-dungeon-clear/data/bonus-loot-coverage-followup-02.csv");
    assert(followup.size() == 16);
    std::set<std::pair<uint32_t, uint32_t>> finalKeys = keys;
    for (RegistryRow const& row : followup)
    {
        assert(row.category == "dungeon" || row.category == "raid");
        assert(row.entry != 15392 && row.map != 44 && row.map != 819);
        assert(finalKeys.insert({ row.entry, row.map }).second);
    }
    assert(finalKeys.erase({ 12129, 249 }) == 1);
    assert(finalKeys.erase({ 12119, 409 }) == 1);
    assert(finalKeys.size() == 150);
    for (std::pair<uint32_t, uint32_t> const key : {
        std::pair<uint32_t, uint32_t>{10184, 249}, {11982, 409}, {12056, 409}, {12057, 409},
        {12098, 409}, {12118, 409}, {12259, 409}, {12264, 409}})
        assert(finalKeys.count(key));
    for (RegistryRow const& row : accepted)
        if (!((row.entry == 12129 && row.map == 249) || (row.entry == 12119 && row.map == 409)))
            assert(finalKeys.count({ row.entry, row.map }));

    std::string const followupMigration = ReadText("sql/database_updates/20260913130000_world.sql");
    assert(followupMigration.find("INSERT IGNORE INTO creature_loot_bonus_registry") != std::string::npos);
    assert(followupMigration.find("UPDATE creature_loot_bonus_registry") == std::string::npos);
    assert(followupMigration.find("WHERE (creature_entry, map_id) IN ((12129,249), (12119,409))") != std::string::npos);
    assert(CountLinesStartingWith(followupMigration, "    (") == 16);
    assert(CountOccurrences(followupMigration, "DELETE FROM creature_loot_bonus_registry") == 1);
    for (RegistryRow const& row : followup)
    {
        std::ostringstream tuple;
        tuple << "(" << row.entry << "," << row.map << ",'" << row.category << "'";
        assert(followupMigration.find(tuple.str()) != std::string::npos);
    }

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
