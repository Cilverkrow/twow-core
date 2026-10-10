#pragma once
#include <cstdarg>
#include "playerbot/ValueEvictPolicy.h"
#include <algorithm>
#include <vector>
#include <string>
#include <iosfwd>
#include <set>
#include <list>
#include <map>
#include <atomic>
#include <mutex>
#include <shared_mutex>

namespace ai
{
    // twow-repo#563 (X3a): AiPlayerbot.X3a.ContextLock (default 0), set from PlayerbotAIConfig. The guards
    // take the lock only when asked to (the switch), so the old path stays lock-free.
    namespace context_lock
    {
        inline std::atomic<bool>& Enabled()
        {
            static std::atomic<bool> enabled{ false };
            return enabled;
        }

        inline bool On() { return Enabled().load(std::memory_order_relaxed); }

        class Shared
        {
        public:
            explicit Shared(std::shared_mutex& mutex, bool lock = true) : mutex(lock ? &mutex : nullptr)
            {
                if (this->mutex)
                    this->mutex->lock_shared();
            }
            ~Shared() { if (mutex) mutex->unlock_shared(); }
            Shared(Shared const&) = delete;
            Shared& operator=(Shared const&) = delete;

        private:
            std::shared_mutex* mutex;
        };

        class Unique
        {
        public:
            explicit Unique(std::shared_mutex& mutex, bool lock = true) : mutex(lock ? &mutex : nullptr)
            {
                if (this->mutex)
                    this->mutex->lock();
            }
            ~Unique() { if (mutex) mutex->unlock(); }
            Unique(Unique const&) = delete;
            Unique& operator=(Unique const&) = delete;

        private:
            std::shared_mutex* mutex;
        };
    }

    class Qualified
    {
    public:
        Qualified() {};
        Qualified(const std::string& qualifier) : qualifier(qualifier) {}
        Qualified(int32 qualifier1) { Qualify(qualifier1); }

    public:
        virtual void Qualify(int32 qualifier) { std::ostringstream out; out << qualifier; this->qualifier = out.str(); }
        virtual void Qualify(const std::string& qualifier) { this->qualifier = qualifier; }
        std::string getQualifier() const { return qualifier; }
        void Reset() { qualifier.clear(); }

        static std::string MultiQualify(const std::vector<std::string>& qualifiers, const std::string& separator, const std::string_view brackets = "{}")
        { 
            std::stringstream out;
            for (uint8 i = 0; i < qualifiers.size(); i++)
            {
                const std::string& qualifier = qualifiers[i];
                if (i == qualifiers.size() - 1)
                {
                    out << qualifier;
                }
                else
                {
                    out << qualifier << separator;
                }
            }

            if (brackets.empty())
            {
                return out.str();
            }
            else
            {
                return brackets[0] + out.str() + brackets[1];
            }
        }

        static std::vector<std::string> getMultiQualifiers(const std::string& qualifier1, const std::string& separator, const std::string_view brackets = "{}")
        { 
            std::vector<std::string> result;

            std::string view = qualifier1;

            if(view.find(brackets[0]) == 0)
                view = qualifier1.substr(1, qualifier1.size()-2);

            size_t last = 0; 
            size_t next = 0; 

            if (view.find(brackets[0]) == std::string::npos)
            {
                while ((next = view.find(separator, last)) != std::string::npos)
                {

                    result.push_back((std::string)view.substr(last, next - last));
                    last = next + separator.length();
                }

                result.push_back(view.substr(last));
            }
            else
            {
                int8 level = 0;
                std::string sub;
                while (next < view.size() || level < 0)
                {
                    if (view[next] == brackets[0])
                        level++;
                    else if (view[next] == brackets[1])
                        level--;
                    else if (!level && view.substr(next, separator.size()) == separator)
                    {
                        result.push_back(sub);
                        sub.clear();
                        next += separator.size();
                        continue;
                    }
                    
                    sub += view[next];

                    next++;
                }

                result.push_back(sub);
            }

            return result;
        }

        static bool isValidNumberString(const std::string& str)
        {
            bool valid = !str.empty();
            if (valid)
            {
                // Check for sign character at the beginning
                size_t start = 0;
                if (str[0] == '+' || str[0] == '-')
                {
                    start = 1;
                }

                // Loop through each character to check if it's a digit
                for (size_t i = start; i < str.size(); ++i) 
                {
                    if (!std::isdigit(str[i])) 
                    {
                        // Non-numeric character found
                        valid = false;
                        break;
                    }
                }
            }

            return valid;
        }
        
        static int32 getMultiQualifierInt(const std::string& qualifier1, uint32 pos, const std::string& separator)
        { 
            std::vector<std::string> qualifiers = getMultiQualifiers(qualifier1, separator);
            if (qualifiers.size() > pos && isValidNumberString(qualifiers[pos]))
            {
                return stoi(qualifiers[pos]);
            }

            return 0;
        }

        static std::string getMultiQualifierStr(const std::string& qualifier1, uint32 pos, const std::string& separator)
        { 
            std::vector<std::string> qualifiers = getMultiQualifiers(qualifier1, separator);
            return (qualifiers.size() > pos) ? qualifiers[pos] : "";
        }
    
    protected:
        std::string qualifier;
    };

    template <class T>
    class NamedObjectFactory
    {
    protected:
        using ActionCreator = std::function<T* (PlayerbotAI* ai)>;
        std::map<std::string, ActionCreator> creators;

    public:
        T* Create(std::string_view name, PlayerbotAI* ai)
        {
            std::string_view nameView = name;
            std::string_view qualifierView;

            if (size_t pos = nameView.find("::"); pos != std::string::npos)
            {
                qualifierView = nameView.substr(pos + 2);
                nameView = nameView.substr(0, pos);
            }

            auto it = creators.find(std::string(nameView));
            if (it == creators.end())
                return nullptr;

            T* object = it->second(ai);
            if (object == nullptr)
                return nullptr;

            if (!qualifierView.empty())
            {
                if (auto* q = dynamic_cast<Qualified*>(object))
                    q->Qualify(std::string(qualifierView));
            }

            return object;
        }

        void GetSupportedKeys(std::set<std::string>& keys) const
        {
            for (const auto& entry : creators)
                keys.insert(entry.first);
        }
    };


    template <class T>
    class NamedObjectContext : public NamedObjectFactory<T>
    {
    public:
        NamedObjectContext(bool shared = false, bool supportsSiblings = false) :
            NamedObjectFactory<T>(), shared(shared), supportsSiblings(supportsSiblings) {}

        // twow-repo#563 (X3a, AiPlayerbot.X3a.ContextLock): other bots read and create values in this map from
        // their own region threads (group targets, rpg/travel targets, qualified "possible targets", ...) while
        // the owner inserts and evicts. With the switch, every access to `created` holds createdMutex - shared
        // to look up, unique to insert or erase - and NOTHING else: factories, Update/Reset, the eviction
        // predicate and delete run outside it. A thread therefore never holds two context locks at once, so no
        // lock order between two bots' contexts exists and no deadlock or inversion is possible.
        T* Create(std::string name, PlayerbotAI* ai)
        {
            if (!context_lock::On())
            {
                if (created.find(name) == created.end())
                    return created[name] = NamedObjectFactory<T>::Create(name, ai);

                return created[name];
            }

            {
                context_lock::Shared const lock(createdMutex);
                typename std::map<std::string, T*>::const_iterator const it = created.find(name);
                if (it != created.end())
                    return it->second;
            }

            T* fresh = NamedObjectFactory<T>::Create(name, ai);   // outside the lock
            T* result = nullptr;
            bool inserted = false;
            {
                context_lock::Unique const lock(createdMutex);
                std::pair<typename std::map<std::string, T*>::iterator, bool> const r = created.emplace(name, fresh);
                result = r.first->second;
                inserted = r.second;
            }
            if (!inserted && fresh)
                delete fresh;   // another thread created it first; ours was never visible
            return result;
        }

        virtual ~NamedObjectContext()
        {
            Clear();
        }

        void Clear()
        {
            std::map<std::string, T*> old;
            {
                context_lock::Unique const lock(createdMutex, context_lock::On());
                old.swap(created);
            }
            for (typename std::map<std::string, T*>::iterator i = old.begin(); i != old.end(); i++)
            {
                if (i->second)
                    delete i->second;
            }
        }

        void Erase(const std::string& name)
        {
            T* doomed = nullptr;
            {
                context_lock::Unique const lock(createdMutex, context_lock::On());
                typename std::map<std::string, T*>::iterator const it = created.find(name);
                if (it == created.end())
                    return;
                doomed = it->second;
                created.erase(it);
            }
            delete doomed;
        }

        void Update()
        {
            if (!context_lock::On())
            {
                for (typename std::map<std::string, T*>::iterator i = created.begin(); i != created.end(); i++)
                {
                    if (i->second)
                        i->second->Update();
                }
                return;
            }
            for (T* object : Snapshot())
                object->Update();
        }

        void Reset()
        {
            if (!context_lock::On())
            {
                for (typename std::map<std::string, T*>::iterator i = created.begin(); i != created.end(); i++)
                {
                    if (i->second)
                        i->second->Reset();
                }
                return;
            }
            for (T* object : Snapshot())
                object->Reset();
        }

        bool IsShared() const { return shared; }
        bool IsSupportsSiblings() { return supportsSiblings; }

        bool IsCreated(const std::string& name)
        {
            context_lock::Shared const lock(createdMutex, context_lock::On());
            return created.find(name) != created.end();
        }

        // #416 (7.3): [MemStores] value-cache size.
        size_t CreatedCount() const
        {
            context_lock::Shared const lock(createdMutex, context_lock::On());
            return created.size();
        }

        // #416 (7.5) F2: created names with a prefix, via lower_bound.
        void AppendCreatedWithPrefix(std::string const& prefix, std::vector<std::string>& out) const
        {
            context_lock::Shared const lock(createdMutex, context_lock::On());
            ai::value_evict::AppendKeysWithPrefix(created, prefix, out);
        }

        // #416 (7.5) F1: delete the created objects the predicate selects. The predicate (value code) runs
        // outside the lock: candidates are copied under the shared lock, the selected ones erased under the
        // unique lock (only if the map still holds that very object), and deleted after it.
        template <class Pred>
        size_t EraseIf(Pred pred)
        {
            if (!context_lock::On())
            {
                size_t erased = 0;
                for (auto it = created.begin(); it != created.end();)
                {
                    if (it->second && pred(it->second))
                    {
                        delete it->second;
                        it = created.erase(it);
                        ++erased;
                    }
                    else
                        ++it;
                }
                return erased;
            }

            std::vector<std::pair<std::string, T*>> candidates;
            {
                context_lock::Shared const lock(createdMutex);
                for (auto const& entry : created)
                    if (entry.second)
                        candidates.push_back(entry);
            }
            std::vector<std::pair<std::string, T*>> selected;
            for (auto const& candidate : candidates)
                if (pred(candidate.second))
                    selected.push_back(candidate);
            std::vector<T*> doomed;
            {
                context_lock::Unique const lock(createdMutex);
                for (auto const& entry : selected)
                {
                    typename std::map<std::string, T*>::iterator const it = created.find(entry.first);
                    if (it != created.end() && it->second == entry.second)
                    {
                        doomed.push_back(it->second);
                        created.erase(it);
                    }
                }
            }
            for (T* object : doomed)
                delete object;
            return doomed.size();
        }
        void AddBaseNameCounts(std::map<std::string, uint32>& counts) const
        {
            context_lock::Shared const lock(createdMutex, context_lock::On());
            for (auto const& entry : created)
            {
                std::string::size_type const pos = entry.first.find("::");
                ++counts[pos == std::string::npos ? entry.first : entry.first.substr(0, pos)];
            }
        }

        std::set<std::string> GetCreated()
        {
            context_lock::Shared const lock(createdMutex, context_lock::On());
            std::set<std::string> keys;
            for (typename std::map<std::string, T*>::iterator it = created.begin(); it != created.end(); it++)
                keys.insert(it->first);
            return keys;
        }

    protected:
        // The created objects, copied under the shared lock so Update/Reset run without it.
        std::vector<T*> Snapshot() const
        {
            std::vector<T*> objects;
            context_lock::Shared const lock(createdMutex, context_lock::On());
            objects.reserve(created.size());
            for (auto const& entry : created)
                if (entry.second)
                    objects.push_back(entry.second);
            return objects;
        }

        std::map<std::string, T*> created;
        mutable std::shared_mutex createdMutex;   // twow-repo#563 (X3a): guards `created` only
        bool shared;
        bool supportsSiblings;
    };

    template <class T> class NamedObjectContextList
    {
    public:
        virtual ~NamedObjectContextList()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                NamedObjectContext<T>* context = *i;
                if (!context->IsShared())
                    delete context;
            }
        }

        void Add(NamedObjectContext<T>* context)
        {
            contexts.push_back(context);
        }

        // Same as Add, but the context is consulted FIRST. Module contexts
        // need this: overriding a stock object by registering the same name
        // (dungeon clear's "auto release"/"loot roll") only works when the
        // module context sits ahead of the stock ones in GetObject's walk -
        // the first Create that answers wins.
        void AddFront(NamedObjectContext<T>* context)
        {
            contexts.push_front(context);
        }

        T* GetObject(const std::string& name, PlayerbotAI* ai)
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                T* object = (*i)->Create(name, ai);
                if (object) return object;
            }
            return NULL;
        }

        void Update()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if (!(*i)->IsShared())
                    (*i)->Update();
            }
        }

        void Reset()
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->Reset();
            }
        }

        std::set<std::string> GetSiblings(const std::string& name)
        {
            std::set<std::string> siblings;
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if ((*i)->IsSupportsSiblings())
                {
                    std::set<std::string> supported;
                    (*i)->GetSupportedKeys(supported);
                    std::set<std::string>::iterator found = supported.find(name);
                    if (found != supported.end())
                    {
                        supported.erase(found);
                        siblings.insert(supported.begin(), supported.end());
                    }
                }
            }

            return siblings;
        }

        void GetSupportedKeys(std::set<std::string>& keys) const
        {
            for (typename std::list<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->GetSupportedKeys(keys);
            }
        }

        bool IsCreated(const std::string& name) const
        {
            for (typename std::list<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                if ((*i)->IsCreated(name))
                    return true;
            }
            return false;
        }

        std::set<std::string> GetCreated()
        {
            std::set<std::string> result;

            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                std::set<std::string> createdKeys = (*i)->GetCreated();

                for (std::set<std::string>::iterator j = createdKeys.begin(); j != createdKeys.end(); j++)
                    result.insert(*j);
            }
            return result;
        }

        void Erase(const std::string& name)
        {
            for (typename std::list<NamedObjectContext<T>*>::iterator i = contexts.begin(); i != contexts.end(); i++)
            {
                (*i)->Erase(name);
            }
        }

        // #416 (7.3): cached objects of this bot's own contexts and of the shared ones.
        std::pair<size_t, size_t> CreatedCounts() const
        {
            std::pair<size_t, size_t> counts{ 0, 0 };
            for (NamedObjectContext<T>* context : contexts)
                (context->IsShared() ? counts.second : counts.first) += context->CreatedCount();
            return counts;
        }

        void AddOwnBaseNameCounts(std::map<std::string, uint32>& counts) const
        {
            for (NamedObjectContext<T>* context : contexts)
                if (!context->IsShared())
                    context->AddBaseNameCounts(counts);
        }

        // #416 (7.5) F2: the created names starting with `prefix` over all
        // contexts, sorted and unique - what ClearValues used to filter from a
        // full copy of every name.
        std::vector<std::string> CreatedWithPrefix(std::string const& prefix) const
        {
            std::vector<std::string> names;
            for (NamedObjectContext<T>* context : contexts)
                context->AppendCreatedWithPrefix(prefix, names);
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }

        // #416 (7.5) F1: only this bot's own contexts; shared ones are other
        // bots' as well and are never touched here.
        template <class Pred>
        size_t EraseOwnIf(Pred pred)
        {
            size_t erased = 0;
            for (NamedObjectContext<T>* context : contexts)
                if (!context->IsShared())
                    erased += context->EraseIf(pred);
            return erased;
        }

    private:
        std::list<NamedObjectContext<T>*> contexts;
    };

    template <class T>
    class NamedObjectFactoryList
    {
    public:
        void Add(std::unique_ptr<NamedObjectFactory<T>> factory)
        {
            factories.emplace_back(std::move(factory));
        }

        T* GetObject(std::string_view name, PlayerbotAI* ai)
        {
            for (auto it = factories.rbegin(); it != factories.rend(); ++it)
            {
                if (T* obj = (*it)->Create(name, ai))
                    return obj;
            }
            return nullptr;
        }

    private:
        std::vector<std::unique_ptr<NamedObjectFactory<T>>> factories;
    };
};
