#pragma once

#include "PvpValues.h"
#include "QuestValues.h"
#include "TrainerValues.h"
#include "VendorValues.h"
#include "TravelValues.h"
#include "LootValues.h"
#include "MountValues.h"
#include "playerbot/PlayerbotAI.h"
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace ai
{
    class SharedValueContext : public NamedObjectContext<UntypedValue>
    {
    public:
        SharedValueContext() : NamedObjectContext(true)
        {
            creators["bg masters"] = [](PlayerbotAI* ai) { return new BgMastersValue(ai); };

            creators["item drop map"] = [](PlayerbotAI* ai) { return new ItemDropMapValue(ai); };
            creators["drop map"] = [](PlayerbotAI* ai) { return new DropMapValue(ai); };
            creators["item drop list"] = [](PlayerbotAI* ai) { return new ItemDropListValue(ai); };
            creators["entry loot list"] = [](PlayerbotAI* ai) { return new EntryLootListValue(ai); };
            creators["loot chance"] = [](PlayerbotAI* ai) { return new LootChanceValue(ai); };

            creators["vendor map"] = [](PlayerbotAI* ai) { return new VendorMapValue(ai); };
            creators["item vendor list"] = [](PlayerbotAI* ai) { return new ItemVendorListValue(ai); };

            creators["entry quest relation"] = [](PlayerbotAI* ai) { return new EntryQuestRelationMapValue(ai); };

            creators["quest guidp map"] = [](PlayerbotAI* ai) { return new QuestGuidpMapValue(ai); };
            creators["quest givers"] = [](PlayerbotAI* ai) { return new QuestGiversValue(ai); };

            creators["trainable spell map"] = [](PlayerbotAI* ai) { return new TrainableSpellMapValue(ai); };

          

            creators["entry travel purpose"] = [](PlayerbotAI* ai) { return new EntryTravelPurposeMapValue(ai); };
            creators["entry guidps"] = [](PlayerbotAI* ai) { return new EntryGuidpsValue(ai); };

            creators["full mount list"] = [](PlayerbotAI* ai) { return new FullMountListValue(ai); };

            creators["global string"] = [](PlayerbotAI* ai) { return new StringManualSetValue(ai); };
        }
    };


    class SharedObjectContext
    {
    public:
        SharedObjectContext() { valueContexts.Add(new SharedValueContext()); };

    public:
        // twow-repo#563 (X4a): one context for all bots, called from every region thread. The lookup
        // inserts into a std::map (new qualified names such as "loot chance::<item>" keep coming), so it
        // was a race between threads. Existing values: shared lock, no insert. Creating: unique lock,
        // double-checked; the PlayerbotAI dummy the factories need is built only then (it was built and
        // destroyed on every call).
        //
        // OB-30 X4a acceptance (10.10.2026): A800 avg bot update +11 % - every lookup took lock_shared on ONE
        // mutex, an atomic write on a cache line all region threads share. Shared values are never erased
        // (no Reset, no eviction on this context), so a pointer once found stays valid: each thread keeps its
        // own small name -> value cache in front of the lock and only goes to the shared map on a miss.
        virtual UntypedValue* GetUntypedValue(const std::string& name)
        {
            thread_local std::unordered_map<std::string, UntypedValue*> threadCache;
            auto const cached = threadCache.find(name);
            if (cached != threadCache.end())
                return cached->second;

            UntypedValue* value = LockedLookup(name);
            if (value)
            {
                if (threadCache.size() >= ThreadCacheLimit)
                    threadCache.clear();   // qualified names keep coming; bound the per-thread memory
                threadCache.emplace(name, value);
            }
            return value;
        }

    private:
        static constexpr size_t ThreadCacheLimit = 4096;

        UntypedValue* LockedLookup(const std::string& name)
        {
            {
                std::shared_lock<std::shared_mutex> lock(valuesMutex);
                if (UntypedValue* existing = valueContexts.Find(name))
                    return existing;
            }

            std::unique_lock<std::shared_mutex> lock(valuesMutex);
            if (UntypedValue* existing = valueContexts.Find(name))
                return existing;

            PlayerbotAI* ai = new PlayerbotAI();
            UntypedValue* value = valueContexts.GetObject(name, ai);
            delete ai;
            return value;
        }

    public:

        template<class T>
        Value<T>* GetValue(const std::string& name)
        {
            return dynamic_cast<Value<T>*>(GetUntypedValue(name));
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, const std::string& param)
        {
            return GetValue<T>((std::string(name) + "::" + param));
        }

        template<class T>
        Value<T>* GetValue(const std::string& name, int32 param)
        {
            std::ostringstream out; out << param;
            return GetValue<T>(name, out.str());
        }
    protected:
        NamedObjectContextList<UntypedValue> valueContexts;
        std::shared_mutex valuesMutex;   // twow-repo#563 (X4a): guards valueContexts
    };
#define sSharedObjectContext MaNGOS::Singleton<SharedObjectContext>::Instance()
}