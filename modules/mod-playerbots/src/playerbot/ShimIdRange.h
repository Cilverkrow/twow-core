#pragma once

// twow-repo#510: the id ranges the creature/GO and faction scans iterate (cmangos-compat-shim.h).
// No core types, so t/shim_id_range_tests.cpp checks them without the server.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cmangos_shim
{
template <class Map>
std::vector<std::uint32_t> SortedKeys(Map const& map)
{
    std::vector<std::uint32_t> keys;
    keys.reserve(map.size());
    for (auto const& kv : map)
        keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Ids to scan: counts [lo, hi) (mode 0, the old loop) or walks a sorted id vector from lo on (mode 1).
class IdRange
{
public:
    class iterator
    {
    public:
        iterator(std::uint32_t const* p, std::uint32_t n) : m_p(p), m_n(n) {}
        std::uint32_t operator*() const { return m_p ? *m_p : m_n; }
        iterator& operator++() { if (m_p) ++m_p; else ++m_n; return *this; }
        bool operator!=(iterator const& o) const { return m_p != o.m_p || m_n != o.m_n; }
    private:
        std::uint32_t const* m_p;  // set mode: position in the vector; nullptr while counting
        std::uint32_t m_n;         // counting mode: the current id
    };

    static IdRange Count(std::uint32_t lo, std::uint32_t hi) { return IdRange(iterator(nullptr, lo), iterator(nullptr, std::max(lo, hi))); }
    static IdRange Set(std::vector<std::uint32_t> const& ids, std::uint32_t lo)
    {
        if (ids.empty())
            return IdRange(iterator(nullptr, 0), iterator(nullptr, 0));
        std::uint32_t const* const first = ids.data();
        std::uint32_t const* const last = first + ids.size();
        return IdRange(iterator(std::lower_bound(first, last, lo), 0), iterator(last, 0));
    }

    iterator begin() const { return m_begin; }
    iterator end() const { return m_end; }
private:
    IdRange(iterator b, iterator e) : m_begin(b), m_end(e) {}
    iterator m_begin;
    iterator m_end;
};

// twow-repo#510 A2: the faction ids a faction loop visits. Off: 0 .. cap (the old loop). On: the ID of every
// entry of a reputation state list (variant a), in list order.
template <class StateList>
class FactionIdRange
{
public:
    class iterator
    {
    public:
        using It = typename StateList::const_iterator;
        iterator(It it, std::uint32_t n, bool list) : m_it(it), m_n(n), m_list(list) {}
        std::uint32_t operator*() const { return m_list ? m_it->second.ID : m_n; }
        iterator& operator++() { if (m_list) ++m_it; else ++m_n; return *this; }
        bool operator!=(iterator const& o) const { return m_list ? m_it != o.m_it : m_n != o.m_n; }
    private:
        It m_it;
        std::uint32_t m_n;
        bool m_list;
    };

    FactionIdRange(StateList const& list, bool on, std::uint32_t cap) : m_list(list), m_on(on), m_cap(cap) {}
    iterator begin() const { return iterator(m_list.begin(), 0, m_on); }
    iterator end() const { return iterator(m_list.end(), m_on ? 0 : m_cap, m_on); }
private:
    StateList const& m_list;
    bool m_on;
    std::uint32_t m_cap;
};
}
