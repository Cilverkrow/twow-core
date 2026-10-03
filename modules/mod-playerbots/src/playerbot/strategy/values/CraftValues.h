#pragma once
#include <map>
#include <memory>
#include <vector>
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/NamedObjectContext.h"

namespace ai
{
    class CraftData
    {
    public:
        CraftData() : itemId(0) {}

        CraftData(const CraftData& other) : itemId(other.itemId)
        {
            required.insert(other.required.begin(), other.required.end());
            obtained.insert(other.obtained.begin(), other.obtained.end());
        }

        bool IsEmpty() { return itemId == 0; }
        void Reset() { itemId = 0; }
        bool IsRequired(uint32 item) { return required.find(item) != required.end(); }

        bool IsFulfilled()
        {
            for (std::map<uint32, int>::iterator i = required.begin(); i != required.end(); ++i)
            {
                uint32 item = i->first;
                if (obtained[item] < i->second)
                    return false;
            }

            return true;
        }

        void AddObtained(uint32 itemId, uint32 count)
        {
            if (IsRequired(itemId))
            {
                obtained[itemId] += count;
            }
        }

        void Crafted(uint32 count)
        {
            for (std::map<uint32, int>::iterator i = required.begin(); i != required.end(); ++i)
            {
                uint32 item = i->first;
                if (obtained[item] >= required[item] * (int)count)
                {
                    obtained[item] -= required[item] * (int)count;
                }
            }
        }

    public:
        uint32 itemId;
        std::map<uint32, int> required, obtained;
    };

    class CraftValue : public ManualSetValue<CraftData&>
    {
    public:
        CraftValue(PlayerbotAI* ai, std::string name = "craft") : ManualSetValue<CraftData&>(ai, data, name) {}

    private:
        CraftData data;
    };

    class CraftSpellsValue : public CalculatedValue<std::vector<uint32>> //All crafting spells
    {
    public:
        CraftSpellsValue(PlayerbotAI* ai, std::string name = "craft spells", int checkInterval = 10) : CalculatedValue<std::vector<uint32>>(ai, name, checkInterval) {}
        virtual std::vector<uint32> Calculate() override;
    };

    // Hotfix 8.16a (v31 ticks, KeepCraftMaterials): reagent item id -> the craft spells that use it,
    // in "craft spells" order. ItemUsageValue recalculates on almost every query (checkInterval 1,
    // 0.1 s) and walked every recipe of the bot for every trade good, reagent and misc item; it now
    // looks the item up here. Shared pointer: handing the index out is a pointer copy.
    typedef std::map<uint32, std::vector<uint32>> CraftReagentIndex;

    class CraftReagentIndexValue : public CalculatedValue<std::shared_ptr<const CraftReagentIndex>>
    {
    public:
        CraftReagentIndexValue(PlayerbotAI* ai, std::string name = "craft reagent index", int checkInterval = 60) :
            CalculatedValue<std::shared_ptr<const CraftReagentIndex>>(ai, name, checkInterval) {}
        virtual std::shared_ptr<const CraftReagentIndex> Calculate() override;
    };

    class EnchantSpellsValue : public CalculatedValue<std::vector<uint32>> //All enchanting spells
    {
    public:
        EnchantSpellsValue(PlayerbotAI* ai, std::string name = "enchant spells", int checkInterval = 10) : CalculatedValue<std::vector<uint32>>(ai, name, checkInterval) {}
        virtual std::vector<uint32> Calculate() override;
    };

    class HasReagentsForValue : public Uint32CalculatedValue, public Qualified //Does the bot have reagents to cast this craft spell?
    {
    public:
        HasReagentsForValue(PlayerbotAI* ai, std::string name = "has reagents for", int checkInterval = 1) : Uint32CalculatedValue(ai, name, checkInterval), Qualified() {}
        virtual uint32 Calculate() override;
    };

    class CanCraftSpellValue : public BoolCalculatedValue, public Qualified
    {
    public:
        CanCraftSpellValue(PlayerbotAI* ai, std::string name = "can craft spell", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {}
        virtual bool Calculate() override;
    };

    class ShouldCraftSpellValue : public BoolCalculatedValue, public Qualified
    {
    public:
        ShouldCraftSpellValue(PlayerbotAI* ai, std::string name = "should craft spell", int checkInterval = 10) : BoolCalculatedValue(ai, name, checkInterval), Qualified() {}
        virtual bool Calculate() override;
        static bool SpellGivesSkillUp(uint32 spellId, Player* bot);
    };
}