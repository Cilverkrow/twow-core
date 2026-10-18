#pragma once

// twow-repo#308 LOOT-ROLE-01 v2 (owner 17.10.2026: "für die Rolle muss sie aber auch passen. Dazu hatten wir ja Ratings
// angefertigt"): the owner stat table v2 (data/stat-weights/stat-priorities-v2.tsv, core#141, approved 26.09.) as code
// constants, without a DB migration. With AiPlayerbot.Loot.RoleWeights = 2 the weight scales of these specs are replaced
// in memory at start: each spec scores an item only by its 5 priorities (100/80/60/40/20 per unit), other stats 0; the
// weapon DPS weight of the old scale is kept (README: weapon DPS first). The contract checks every row against the TSV.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ai::loot_v2
{
    struct Row
    {
        std::uint8_t cls;
        char const* spec;          // premade path name (stat-priorities-v2.tsv `spec`)
        char const* prio[5];
        char const* school;        // spell school for spell power, "-" = none
        char const* scale;         // ai_playerbot_weightscales name it replaces
    };

    // stat-priorities-v2.tsv, 28 specs (shaman tank 7.3 / rogue tank 4.3 stay out: "weight 0 until owner sign-off").
    inline constexpr Row Rows[] = {
        { 1, "fury", { "str", "crit_pct", "hit_pct", "agi", "sta" }, "-", "fury" },
        { 1, "protection", { "defense", "block_pct", "dodge_pct", "sta", "str" }, "-", "prot" },
        { 1, "arms", { "str", "crit_pct", "agi", "hit_pct", "sta" }, "-", "arms" },
        { 2, "holy", { "int", "healing", "spell_crit_pct", "mp5", "spi" }, "holy", "holy" },
        { 2, "protection", { "defense", "block_pct", "dodge_pct", "sta", "str" }, "holy", "prot" },
        { 2, "retribution", { "str", "crit_pct", "agi", "sta", "hit_pct" }, "-", "retrib" },
        { 3, "beastmastery", { "agi", "rap", "crit_pct", "hit_pct", "int" }, "-", "beast" },
        { 3, "marksmanship", { "agi", "crit_pct", "rap", "hit_pct", "int" }, "-", "marks" },
        { 3, "survival", { "agi", "crit_pct", "rap", "hit_pct", "sta" }, "-", "surv" },
        { 4, "combat", { "agi", "hit_pct", "crit_pct", "str", "ap" }, "-", "combat" },
        { 4, "assassination", { "agi", "crit_pct", "hit_pct", "ap", "str" }, "-", "assas" },
        { 4, "subtlety", { "agi", "crit_pct", "ap", "hit_pct", "str" }, "-", "subtle" },
        { 5, "holy", { "int", "spi", "healing", "mp5", "spell_crit_pct" }, "holy", "holy" },
        { 5, "shadow", { "spell_power", "int", "spi", "spell_hit_pct", "sta" }, "shadow", "shadow" },
        { 5, "discipline", { "int", "spi", "healing", "mp5", "spell_power" }, "holy", "disc" },
        { 7, "restoration", { "int", "healing", "mp5", "spell_crit_pct", "spi" }, "nature", "resto" },
        { 7, "enhancement", { "str", "agi", "crit_pct", "hit_pct", "int" }, "-", "enhance" },
        { 7, "elemental", { "spell_power", "int", "spell_crit_pct", "spell_hit_pct", "mp5" }, "nature", "elem" },
        { 8, "arcane", { "int", "spell_power", "spell_crit_pct", "spell_hit_pct", "spi" }, "arcane", "arcane" },
        { 8, "fire", { "spell_power", "spell_crit_pct", "int", "spell_hit_pct", "spi" }, "fire", "fire" },
        { 8, "frost", { "spell_power", "int", "spell_hit_pct", "spell_crit_pct", "spi" }, "frost", "frost" },
        { 9, "affliction", { "spell_power", "sta", "int", "spell_hit_pct", "spi" }, "shadow", "afflic" },
        { 9, "demonology", { "sta", "spell_power", "int", "spell_hit_pct", "spi" }, "shadow", "demo" },
        { 9, "destruction", { "spell_power", "spell_crit_pct", "int", "spell_hit_pct", "sta" }, "fire", "destro" },
        { 11, "balance", { "spell_power", "int", "spell_crit_pct", "spi", "mp5" }, "arcane", "balance" },
        { 11, "feral", { "agi", "crit_pct", "ap", "str", "hit_pct" }, "-", "feraldps" },
        { 11, "restoration", { "int", "spi", "healing", "mp5", "spell_crit_pct" }, "nature", "resto" },
        { 11, "bear", { "defense", "dodge_pct", "armor", "sta", "agi" }, "-", "feraltank" },
    };

    inline constexpr std::uint32_t PrioWeight[5] = { 100, 80, 60, 40, 20 };

    // The weight-scale fields (RandomItemMgr::CalculateSingleStatWeight names) a v2 stat scores through, and the weight
    // per field value for a priority weight w (README units: 1 % = 10 units, AP 2 points = 1 unit, 1 mp5 = 2 units,
    // armour per 10 points). Spell power counts for all schools (splpwr) and for the row's own school field.
    inline std::vector<std::pair<std::string, std::uint32_t>> Fields(std::string const& stat, std::uint32_t w,
        std::string const& school)
    {
        if (stat == "str" || stat == "agi" || stat == "sta" || stat == "int" || stat == "spi")
            return { { stat, w } };
        if (stat == "defense")
            return { { "defrtng", w } };
        if (stat == "ap")
            return { { "atkpwr", w / 2 }, { "feratkpwr", w / 2 } };
        if (stat == "rap")
            return { { "atkpwr", w / 2 } };
        if (stat == "healing")
            return { { "splheal", w } };
        if (stat == "mp5")
            return { { "manargn", w * 2 } };
        if (stat == "armor")
            return { { "armor", w / 10 } };
        if (stat == "hit_pct")
            return { { "hitrtng", w * 10 } };
        if (stat == "crit_pct")
            return { { "critstrkrtng", w * 10 } };
        if (stat == "spell_hit_pct")
            return { { "spellhitrtng", w * 10 } };
        if (stat == "spell_crit_pct")
            return { { "spellcritstrkrtng", w * 10 } };
        if (stat == "dodge_pct")
            return { { "dodgertng", w * 10 } };
        if (stat == "parry_pct")
            return { { "parryrtng", w * 10 } };
        if (stat == "block_pct")
            return { { "blockrtng", w * 10 } };
        if (stat == "spell_power")
        {
            std::vector<std::pair<std::string, std::uint32_t>> out = { { "splpwr", w } };
            if (school == "arcane") out.push_back({ "arcsplpwr", w });
            if (school == "fire") out.push_back({ "firsplpwr", w });
            if (school == "frost") out.push_back({ "frosplpwr", w });
            if (school == "nature") out.push_back({ "natsplpwr", w });
            if (school == "shadow") out.push_back({ "shasplpwr", w });
            return out;
        }
        return {};
    }

    // All fields and weights of one row (a field named twice keeps the larger weight).
    inline std::vector<std::pair<std::string, std::uint32_t>> RowWeights(Row const& row)
    {
        std::vector<std::pair<std::string, std::uint32_t>> out;
        for (std::size_t i = 0; i < 5; ++i)
        {
            for (auto const& [field, weight] : Fields(row.prio[i], PrioWeight[i], row.school))
            {
                bool merged = false;
                for (auto& entry : out)
                {
                    if (entry.first == field)
                    {
                        if (weight > entry.second)
                            entry.second = weight;
                        merged = true;
                    }
                }
                if (!merged)
                    out.push_back({ field, weight });
            }
        }
        return out;
    }

    // The old-scale entries a v2 scale keeps: weapon DPS (README: weapon DPS first for melee/ranged specs).
    inline bool KeepFromOldScale(std::string const& field)
    {
        return field == "mledps" || field == "rgddps";
    }
}
