#pragma once

#include "Config/Config.h"

class AhBotConfig
{
public:
    AhBotConfig();
    static AhBotConfig& instance()
    {
        static AhBotConfig instance;
        return instance;
    }

public:
    bool Initialize();

    // twow-repo#597 (TICK-01): defined before Initialize(). Without an ahbot.conf Initialize() returns before it reads
    // any of these, and the singleton comes from operator new with an empty user constructor, so they were
    // indeterminate (live logged updateInterval as 0 and ForceUpdate saw enabled as false). 0/false is that observed
    // behaviour; a found ahbot.conf overwrites every one of them exactly as before.
    bool enabled = false;
    bool configLoaded = false;  // twow-repo#597 (TICK-01): true once ahbot.conf was found and read (AhBot.Enabled may still be 0)
    uint64 guid = 0;
    uint32 updateInterval = 0;
    uint32 historyDays = 0, maxSellInterval = 0;
    uint32 itemBuyMinInterval = 0, itemBuyMaxInterval = 0;
    uint32 itemSellMinInterval = 0, itemSellMaxInterval = 0;
    uint32 alwaysAvailableMoney = 0;
    float priceMultiplier = 0.0f, priceQualityMultiplier = 0.0f;
    uint32 defaultMinPrice = 0, stackReducePrice = 0;
    uint32 maxItemLevel = 0, maxRequiredLevel = 0;
    float underPriceProbability = 0.0f;
    std::set<uint32> ignoreItemIds;
    std::set<uint32> ignoreVendorItemIds;
    bool sendmail = false;

    float GetSellPriceMultiplier(std::string category)
    {
        return GetCategoryParameter(sellPriceMultipliers, "PriceMultiplier.Sell", category, 1.0f);
    }

    float GetBuyPriceMultiplier(std::string category)
    {
        return GetCategoryParameter(buyPriceMultipliers, "PriceMultiplier.Buy", category, 1.0f);
    }

    float GetItemPriceMultiplier(std::string name)
    {
        return GetCategoryParameter(itemPriceMultipliers, "PriceMultiplier.Item", name, 1.0f);
    }

    int32 GetMaxAllowedAuctionCount(std::string category, int32 default_value)
    {
        return (int32)GetCategoryParameter(maxAuctionCount, "MaxAuctionCount", category, default_value);
    }

    int32 GetMaxAllowedItemAuctionCount(std::string category, int32 default_value)
    {
        return (int32)GetCategoryParameter(maxItemAuctionCount, "MaxItemTypeCount", category, default_value);
    }    

    std::string GetStringDefault(const char* name, const char* def)
    {
        return config.GetStringDefault(name, def);
    }

    bool GetBoolDefault(const char* name, const bool def = false)
    {
        return config.GetBoolDefault(name, def);
    }

    int32 GetIntDefault(const char* name, const int32 def)
    {
        return config.GetIntDefault(name, def);
    }

    float GetFloatDefault(const char* name, const float def)
    {
        return config.GetFloatDefault(name, def);
    }

private:
    float GetCategoryParameter(std::map<std::string, float>& cache, std::string type, std::string category, float defaultValue)
    {
        if (cache.find(category) == cache.end())
        {
            std::ostringstream out; out << "AhBot."<< type << "." << category;
            cache[category] = config.GetFloatDefault(out.str().c_str(), defaultValue);
        }

        return cache[category];
    }

private:
    Config config;
    std::map<std::string, float> sellPriceMultipliers;
    std::map<std::string, float> buyPriceMultipliers;
    std::map<std::string, float> itemPriceMultipliers;
    std::map<std::string, float> maxAuctionCount;
    std::map<std::string, float> maxItemAuctionCount;
};

#define sAhBotConfig MaNGOS::Singleton<AhBotConfig>::Instance()

