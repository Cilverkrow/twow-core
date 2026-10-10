#pragma once

namespace ai::bgmaster
{
// twow-repo#541 (audit A09, AiPlayerbot.Perf.BgMasterCacheRef): read-only lookups in the global battlemaster
// cache (RandomPlayerbotMgr::BattleMastersCache, team -> bg type -> creature entries) without the deep
// copy that getBattleMastersCache() makes. The map is shared by all map threads and written only by
// LoadBattleMastersCache (PlayerbotAIConfig::Initialize), so lookups here use find() only - never
// operator[] (it would insert into the shared map) and never at() (it throws on an absent key).
// nullptr means "no entries", exactly what operator[] on the old by-value copy yielded (an empty list
// inserted into the copy only).

// The entry list for team/bgType, or nullptr when either key is absent.
template <class Cache>
typename Cache::mapped_type::mapped_type const* FindEntries(Cache const& cache,
    typename Cache::key_type team, typename Cache::mapped_type::key_type bgType)
{
    auto const teamIt = cache.find(team);
    if (teamIt == cache.end())
        return nullptr;
    auto const bgIt = teamIt->second.find(bgType);
    if (bgIt == teamIt->second.end())
        return nullptr;
    return &bgIt->second;
}

// True when entry is in the list; nullptr is the empty list.
template <class EntryList, class Entry>
bool Contains(EntryList const* entries, Entry entry)
{
    if (!entries)
        return false;
    for (auto const& e : *entries)
        if (e == entry)
            return true;
    return false;
}

// Appends the list in its order; nullptr appends nothing.
template <class Out, class EntryList>
void Append(Out& out, EntryList const* entries)
{
    if (entries)
        out.insert(out.end(), entries->begin(), entries->end());
}
}
