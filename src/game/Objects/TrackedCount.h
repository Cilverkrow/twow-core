#pragma once

#include <atomic>
#include <cstdint>

// twow-repo#452: a count that follows its owner through copies and destruction
// and adds up in one process-wide total per Tag, for the hourly [MemStores] line.
// A member TrackedCount<Item> live{1} counts the live Items; a TrackedCount whose
// value is Set() to a vector size counts the elements of all those vectors.
// Diagnostic only: relaxed atomics, no ordering with anything else.
template <typename Tag>
class TrackedCount
{
public:
    TrackedCount() = default;
    explicit TrackedCount(int64_t value) : m_value(value) { s_total.fetch_add(value, std::memory_order_relaxed); }
    TrackedCount(TrackedCount const& other) : m_value(other.m_value) { s_total.fetch_add(m_value, std::memory_order_relaxed); }
    TrackedCount& operator=(TrackedCount const& other)
    {
        Set(other.m_value);
        return *this;
    }
    ~TrackedCount() { s_total.fetch_sub(m_value, std::memory_order_relaxed); }

    void Set(int64_t value)
    {
        s_total.fetch_add(value - m_value, std::memory_order_relaxed);
        m_value = value;
    }

    static int64_t Total() { return s_total.load(std::memory_order_relaxed); }

private:
    int64_t m_value = 0;
    static inline std::atomic<int64_t> s_total{0};
};
