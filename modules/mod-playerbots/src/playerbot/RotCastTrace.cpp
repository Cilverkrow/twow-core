#include "playerbot/playerbot.h"
#include "playerbot/RotCastTrace.h"
#include "playerbot/RotCastPolicy.h"
#include "playerbot/ServerFacade.h"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// twow-repo#602 ROT-01 D0 (AiPlayerbot.Rotation.CastTrace): counters live in a per-thread open-addressing table and
// reach the shared minute book in batches (one short std::mutex per batch), so isPossible on the map threads takes
// no lock per call. The World thread swaps the book out once a minute and writes the lines outside the lock.
namespace
{
using ai::rot_cast::Key;
using ai::rot_cast::Summary;

constexpr unsigned LocalSlotBits = 10;
constexpr std::size_t LocalSlots = std::size_t(1) << LocalSlotBits;
constexpr std::uint32_t LocalFlushEvents = 1024;
constexpr std::uint32_t LocalFlushFill = 512;   // flush before the table is half full
constexpr std::size_t BookCap = 8192;           // keys per minute window, more are counted as dropped
constexpr std::size_t HourCap = 16384;
constexpr std::size_t RoleBookCap = 8192;
constexpr std::uint16_t SlotOther = 1023;
constexpr std::uint32_t UnknownLinesPerMinute = 120;
constexpr std::uint32_t RoleLinesPerMinute = 120;
constexpr std::uint32_t PullLinesPerMinute = 300;
constexpr std::size_t RowsPerMinute = 25;
constexpr std::size_t RowsPerHour = 40;

struct Local
{
    std::uint64_t keys[LocalSlots];
    std::uint32_t counts[LocalSlots];
    std::uint32_t used;
    std::uint32_t events;
    std::uint32_t epoch;   // window epoch of the first count in the table
};

thread_local Local tl;     // zero-initialised (static storage)

std::atomic<std::uint32_t> g_epoch{1};
std::atomic<std::uint32_t> g_discardBefore{0};

std::mutex g_bookMutex;
std::unordered_map<std::uint64_t, std::uint64_t> g_book;
Summary g_summary;
std::uint64_t g_dropped = 0;

std::mutex g_internMutex;
std::unordered_map<std::string, std::uint16_t> g_slots;
std::vector<std::string> g_names{ "-" };   // slot 0 = not attributed

std::mutex g_budgetMutex;
std::uint32_t g_unknownWindow = 0, g_unknownUsed = 0, g_unknownDropped = 0;
std::uint32_t g_roleWindow = 0, g_roleUsed = 0, g_roleDropped = 0;
std::uint32_t g_pullWindow = 0, g_pullUsed = 0, g_pullDropped = 0;
std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint32_t>> g_roles;   // guid -> signature, suppressed

void FlushLocal()
{
    if (tl.used && tl.epoch >= g_discardBefore.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> lock(g_bookMutex);
        for (std::size_t i = 0; i < LocalSlots; ++i)
        {
            std::uint64_t const key = tl.keys[i];
            if (!key)
                continue;
            Key const k = ai::rot_cast::UnpackKey(key);
            ai::rot_cast::AddToSummary(g_summary, k.kind, k.code, tl.counts[i]);
            auto it = g_book.find(key);
            if (it != g_book.end())
                it->second += tl.counts[i];
            else if (g_book.size() < BookCap)
                g_book.emplace(key, tl.counts[i]);
            else
                g_dropped += tl.counts[i];
        }
    }
    if (tl.used)
        std::fill(std::begin(tl.keys), std::end(tl.keys), 0);
    tl.used = 0;
    tl.events = 0;
}

void Record(std::uint64_t key)
{
    if (!tl.used)
        tl.epoch = g_epoch.load(std::memory_order_relaxed);

    std::size_t slot = ai::rot_cast::HomeSlot(key, LocalSlotBits);
    while (true)
    {
        if (tl.keys[slot] == key)
        {
            ++tl.counts[slot];
            break;
        }
        if (!tl.keys[slot])
        {
            tl.keys[slot] = key;
            tl.counts[slot] = 1;
            ++tl.used;
            break;
        }
        slot = (slot + 1) & (LocalSlots - 1);
    }

    if (++tl.events >= LocalFlushEvents || tl.used >= LocalFlushFill)
        FlushLocal();
}

std::string SubjectOf(Key const& key, std::vector<std::string> const& names)
{
    if (key.kind == ai::rot_cast::KindServer)
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(key.id);
        return ai::rot_cast::SpellSubject(key.id, spell ? std::string(spell->SpellName[0]) : std::string());
    }
    return ai::rot_cast::ActionSubject(key.id < names.size() ? names[key.id] : std::string("other"));
}

void WriteRows(char const* tag, std::unordered_map<std::uint64_t, std::uint64_t> const& book, std::size_t limit,
    std::vector<std::string> const& names)
{
    std::vector<std::pair<std::uint64_t, std::uint64_t>> rows(book.begin(), book.end());
    std::size_t const n = (std::min)(limit, rows.size());
    std::partial_sort(rows.begin(), rows.begin() + n, rows.end(), [](auto const& a, auto const& b)
    {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    for (std::size_t i = 0; i < n; ++i)
    {
        Key const key = ai::rot_cast::UnpackKey(rows[i].first);
        sLog.outBasic("%s", ai::rot_cast::FormatRow(tag, uint32(i + 1), key, SubjectOf(key, names), rows[i].second).c_str());
    }
}
}

namespace ai::rot_cast_rt
{
std::uint16_t ActionSlot(std::string const& action)
{
    std::lock_guard<std::mutex> lock(g_internMutex);
    auto it = g_slots.find(action);
    if (it != g_slots.end())
        return it->second;
    if (g_names.size() >= SlotOther)
        return SlotOther;
    std::uint16_t const slot = std::uint16_t(g_names.size());
    g_names.push_back(action);
    g_slots.emplace(action, slot);
    return slot;
}

void RecordEval(std::uint8_t cls, bool cohortB, std::uint16_t slot, std::uint16_t code)
{
    Record(ai::rot_cast::PackKey(cls, cohortB, ai::rot_cast::KindEval, slot, code));
}

void RecordStart(std::uint8_t cls, bool cohortB, std::uint16_t slot, std::uint16_t code)
{
    Record(ai::rot_cast::PackKey(cls, cohortB, ai::rot_cast::KindStart, slot, code));
}

void RecordServer(std::uint8_t cls, bool cohortB, std::uint32_t spellId, std::uint16_t code)
{
    Record(ai::rot_cast::PackKey(cls, cohortB, ai::rot_cast::KindServer, spellId, code));
}

void FlushIfDue()
{
    if (tl.used && tl.epoch != g_epoch.load(std::memory_order_relaxed))
        FlushLocal();
}

bool TakeUnknownBudget(std::uint32_t now)
{
    std::lock_guard<std::mutex> lock(g_budgetMutex);
    if (ai::rot_cast::BudgetTake(g_unknownWindow, g_unknownUsed, now, UnknownLinesPerMinute))
        return true;
    ++g_unknownDropped;
    return false;
}

bool TakePullBudget(std::uint32_t now)
{
    std::lock_guard<std::mutex> lock(g_budgetMutex);
    if (ai::rot_cast::BudgetTake(g_pullWindow, g_pullUsed, now, PullLinesPerMinute))
        return true;
    ++g_pullDropped;
    return false;
}

bool RoleLineDue(std::uint32_t guid, std::uint64_t signature, std::uint32_t now, std::uint32_t& suppressed)
{
    std::lock_guard<std::mutex> lock(g_budgetMutex);
    auto it = g_roles.find(guid);
    if (it != g_roles.end() && it->second.first == signature)
    {
        ++it->second.second;
        return false;
    }
    if (!ai::rot_cast::BudgetTake(g_roleWindow, g_roleUsed, now, RoleLinesPerMinute))
    {
        ++g_roleDropped;   // the signature is not stored: the next reset tries again
        return false;
    }
    if (it == g_roles.end())
    {
        if (g_roles.size() >= RoleBookCap)
            g_roles.clear();
        suppressed = 0;
        g_roles.emplace(guid, std::make_pair(signature, 0u));
    }
    else
    {
        suppressed = it->second.second;
        it->second = std::make_pair(signature, 0u);
    }
    return true;
}

void Report(bool firstCall, std::uint32_t now)
{
    static std::uint32_t lastReport = 0;
    static std::uint32_t minutes = 0;
    static std::uint32_t hourSeconds = 0;
    static std::unordered_map<std::uint64_t, std::uint64_t> hourBook;
    static Summary hourSummary;
    static std::uint64_t hourDropped = 0;
    static std::uint32_t hourUnknownDropped = 0, hourRoleDropped = 0, hourPullDropped = 0;

    if (firstCall)
    {
        // Counts taken before the switch went on (an earlier on-period) are discarded on their next flush.
        g_discardBefore.store(g_epoch.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        FlushLocal();
        {
            std::lock_guard<std::mutex> lock(g_bookMutex);
            g_book.clear();
            g_summary = Summary();
            g_dropped = 0;
        }
        {
            std::lock_guard<std::mutex> lock(g_budgetMutex);
            g_unknownDropped = g_roleDropped = g_pullDropped = 0;
            g_roles.clear();   // every on-period starts with a [RotRole] baseline per bot
        }
        hourBook.clear();
        hourSummary = Summary();
        hourDropped = 0;
        hourUnknownDropped = hourRoleDropped = hourPullDropped = 0;
        minutes = 0;
        hourSeconds = 0;
        lastReport = now;
        return;
    }

    if (now < lastReport)
    {
        lastReport = now;   // the wall clock went back: start a new window instead of falling silent
        return;
    }
    if (now < lastReport + 60)
        return;
    std::uint32_t const window = now - lastReport;
    lastReport = now;

    FlushLocal();   // the World thread's own counts (CastSpell / ResetStrategies run here too)
    std::unordered_map<std::uint64_t, std::uint64_t> book;
    Summary summary;
    std::uint64_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(g_bookMutex);
        book.swap(g_book);
        summary = g_summary;
        g_summary = Summary();
        dropped = g_dropped;
        g_dropped = 0;
    }
    g_epoch.fetch_add(1, std::memory_order_relaxed);

    std::uint32_t unknownDropped = 0, roleDropped = 0, pullDropped = 0;
    {
        std::lock_guard<std::mutex> lock(g_budgetMutex);
        std::swap(unknownDropped, g_unknownDropped);
        std::swap(roleDropped, g_roleDropped);
        std::swap(pullDropped, g_pullDropped);
    }
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(g_internMutex);
        names = g_names;
    }

    sLog.outBasic("%s", ai::rot_cast::FormatSummary("[RotCast]", "window_s", window, summary, uint32(book.size()), dropped,
        unknownDropped, roleDropped, pullDropped).c_str());
    WriteRows("[RotCast]", book, RowsPerMinute, names);

    for (auto const& entry : book)
    {
        auto it = hourBook.find(entry.first);
        if (it != hourBook.end())
            it->second += entry.second;
        else if (hourBook.size() < HourCap)
            hourBook.emplace(entry.first, entry.second);
        else
            hourDropped += entry.second;
    }
    for (std::size_t i = 0; i < ai::rot_cast::SumCount; ++i)
        hourSummary.v[i] += summary.v[i];
    hourDropped += dropped;
    hourUnknownDropped += unknownDropped;
    hourRoleDropped += roleDropped;
    hourPullDropped += pullDropped;
    hourSeconds += window;

    if (++minutes < 60)
        return;

    sLog.outBasic("%s", ai::rot_cast::FormatSummary("[RotCastTotal]", "window_min", hourSeconds / 60, hourSummary,
        uint32(hourBook.size()), hourDropped, hourUnknownDropped, hourRoleDropped, hourPullDropped).c_str());
    WriteRows("[RotCastTotal]", hourBook, RowsPerHour, names);
    hourBook.clear();
    hourSummary = Summary();
    hourDropped = 0;
    hourUnknownDropped = hourRoleDropped = hourPullDropped = 0;
    minutes = 0;
    hourSeconds = 0;
}
}
