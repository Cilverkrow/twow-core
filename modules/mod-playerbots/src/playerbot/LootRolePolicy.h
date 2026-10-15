#pragma once

// twow-repo#308 LOOT-ROLE-01 (owner 15.10.2026: "Was ist mit Quest-Belohnungsauswahl nach Heiler, Tank, Melee-DD,
// Range-DD, Caster-DD?", approved by OB-00 15.10.): with AiPlayerbot.Loot.RoleWeights = 1 the weight scale comes from
// the roster's premade talent path from level 1 (below level 10 the talent tree gave a class default: every warrior
// prot, every paladin retribution, every druid balance, every shaman enhancement), and of several usable quest rewards
// the one with the largest gain wins instead of the first in the list. AiPlayerbot.Loot.FeralFixedSpec (default 1,
// owner 26.09.: bear and cat are fixed paths) ends the 50/50 coin between the bear and the cat scale on every check.

#include <cstdint>
#include <string>
#include <vector>

namespace ai::loot_role
{
    // The ai_playerbot_weightscales name for a premade path (AiPlayerbot.PremadeSpecName.<class>.<n>), "" = unknown
    // path: the old talent-tree rule applies. Tank shaman/rogue have no tank scale in the table yet (train 10, v2).
    inline std::string ScaleForPath(std::uint8_t cls, std::string const& path, std::uint32_t level)
    {
        switch (cls)
        {
        case 1:   // warrior
            if (path == "fury") return "fury";
            if (path == "protection") return "prot";
            if (path == "arms") return "arms";
            break;
        case 2:   // paladin
            if (path == "holy") return "holy";
            if (path == "protection") return "prot";
            if (path == "retribution") return "retrib";
            break;
        case 3:   // hunter
            if (path == "beastmastery") return "beast";
            if (path == "marksmanship") return "marks";
            if (path == "survival") return "surv";
            break;
        case 4:   // rogue
            if (path == "combat" || path == "rogue tank") return "combat";
            if (path == "assassination") return "assas";
            if (path == "subtlety") return "subtle";
            break;
        case 5:   // priest
            if (path == "holy") return "holy";
            if (path == "shadow") return "shadow";
            if (path == "discipline") return "disc";
            break;
        case 7:   // shaman
            if (path == "restoration") return "resto";
            if (path == "enhancement" || path == "shaman tank") return "enhance";
            if (path == "elemental") return "elem";
            break;
        case 8:   // mage
            if (path == "arcane" || path == "fire" || path == "frost") return path;
            break;
        case 9:   // warlock
            if (path == "affliction") return "afflic";
            if (path == "demonology") return "demo";
            if (path == "destruction") return "destro";
            break;
        case 11:  // druid: cat form comes at level 20, below that a feral fights in bear form
            if (path == "balance") return "balance";
            if (path == "restoration") return "resto";
            if (path == "bear") return "feraltank";
            if (path == "feral") return level > 19 ? "feraldps" : "feraltank";
            break;
        default:
            break;
        }
        return "";
    }

    // Feral tree, bear or cat scale. coin = the old urand(0, 100) > 50, used only with the switch off.
    inline bool FeralDps(bool fixedSpec, std::string const& path, std::uint32_t level, std::uint32_t guidLow, bool coin)
    {
        if (level <= 19)
            return false;
        if (!fixedSpec)
            return coin;
        if (path == "bear")
            return false;
        if (path == "feral")
            return true;
        return guidLow % 2 == 1;   // no path: still the same answer on every check
    }

    struct Choice
    {
        std::uint8_t index = 0;
        bool equip = false;          // item usage EQUIP
        std::int64_t gain = 0;       // weight of the reward minus the weight of the item it replaces
        std::uint32_t quality = 0;
        std::uint32_t itemLevel = 0;
    };

    // The equip reward with the largest gain; equal gain: quality, then item level, then list order. -1 = none.
    inline int BestByGain(std::vector<Choice> const& choices)
    {
        Choice const* best = nullptr;
        for (Choice const& c : choices)
        {
            if (!c.equip)
                continue;
            if (!best || c.gain > best->gain || (c.gain == best->gain && (c.quality > best->quality ||
                (c.quality == best->quality && c.itemLevel > best->itemLevel))))
                best = &c;
        }
        return best ? int(best->index) : -1;
    }

    inline std::uint8_t Pick(bool roleWeights, std::uint8_t first, int best)
    {
        return roleWeights && best >= 0 ? std::uint8_t(best) : first;
    }
}
