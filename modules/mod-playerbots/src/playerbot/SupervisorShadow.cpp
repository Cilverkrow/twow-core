#include "playerbot/playerbot.h"
#include "playerbot/SupervisorShadow.h"
#include "playerbot/Supervisor.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/RotCastPolicy.h"
#include "playerbot/CohortPolicy.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

// A3 step 3, behaviour pair 1 (AiPlayerbot.Supervisor.Arbiter, default 0): the arbiter shadow (SupervisorShadow.h). Watches only;
// the pure rules and line formats are in SupervisorPolicy.h (unit test t/supervisor_policy_tests.cpp).
namespace ai::supervisor
{
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_NONE) == kStatusNone, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_PREPARE) == kStatusPrepare, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_READY) == kStatusReady, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_TRAVEL) == kStatusTravel, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_WORK) == kStatusWork, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_COOLDOWN) == kStatusCooldown, "status order");
static_assert(std::uint8_t(TravelStatus::TRAVEL_STATUS_EXPIRED) == kStatusExpired, "status order");

namespace
{
constexpr std::uint32_t LinesPerMinute = 300;   // E2 a (OB-00 07.11.): one global budget; a refused line counts as dropped
constexpr std::uint32_t kWriterDepth = 4;

struct Tag
{
    ThreadKind kind = ThreadKind::None;
    PlayerbotAI const* owner = nullptr;
};

thread_local Tag t_tag;
thread_local WriterId t_writers[kWriterDepth];
thread_local std::uint32_t t_depth = 0;   // may pass kWriterDepth; only the first kWriterDepth entries are kept

// True while at least one shadow is on (g_on > 0; set and cleared under g_mutex). Off, an observed write costs this one load.
std::atomic<bool> g_armed{false};

// Build rule 1 (OB-00 07.11.): one short file-local mutex around the line budget, the minute sums and every ShadowState.
std::mutex g_mutex;
std::uint32_t g_on = 0;             // shadows with on = true
std::uint32_t g_window = 0, g_used = 0;
std::uint32_t g_minute = 0;
ArbiterMinute g_min[2];             // [0] cohort A (even low guid), [1] cohort B
bool g_active[2] = {false, false};  // a shadow tick of that cohort ran in this minute

std::size_t CohortIndex(std::uint32_t guidLow)
{
    return cohort::InB(guidLow) ? 1 : 0;
}

WriterId CurrentWriter()
{
    if (t_depth == 0)
        return WriterId::Unknown;
    return t_writers[(t_depth < kWriterDepth ? t_depth : kWriterDepth) - 1];
}

bool IsNullDest(TravelDestination const* dest)
{
    return !dest || dynamic_cast<NullTravelDestination const*>(dest) != nullptr;
}

// The line text "<short name>:<entry>:<quest id|0>", the null target "idle:0:0". Only while the destination is alive: in
// ShadowObserve, inside the status write that holds it.
void FillName(DestKey& key, TravelDestination const* dest)
{
    if (IsNullDest(dest))
    {
        std::snprintf(key.name, sizeof(key.name), "%s", "idle:0:0");
        return;
    }
    std::uint32_t questId = 0;
    if (auto const* quest = dynamic_cast<QuestTravelDestination const*>(dest))
        questId = quest->GetQuestId();
    std::string name = dest->GetShortName();
    for (char& c : name)
        if (c == ' ' || c == ':' || c == '=')
            c = '_';
    std::snprintf(key.name, sizeof(key.name), "%s:%d:%u", name.empty() ? "-" : name.c_str(), int(dest->GetEntry()), questId);
}

// By address and kind. A freed temporary destination whose address comes back for a new temporary one between two followed
// writes reads as the same (all temporary destinations are "temp:0:0"; only the test command makes them): a misclassified
// bundle at worst, never a dereference.
bool SameDest(DestKey const& kept, DestKey const& now)
{
    return kept.id == now.id && kept.temp == now.temp;
}

// Under g_mutex: the state goes off. The last shadow off disarms the observer and clears the minute sums (the minute in which
// the shadow went off is not reported; a later switch-on starts a clean minute).
void DropLocked(ShadowState& state)
{
    if (state.on && g_on > 0 && --g_on == 0)
    {
        g_armed.store(false, std::memory_order_relaxed);
        for (std::size_t i = 0; i < 2; ++i)
        {
            g_min[i] = ArbiterMinute();
            g_active[i] = false;
        }
        g_minute = 0;
    }
    state = ShadowState();
}

struct Closed
{
    LineKind kind = LineKind::None;
    Bundle bundle;
    DestKey from;
    DestKey to;
};

// Under g_mutex: closes the open bundle, counts it and takes the line budget.
Closed CloseLocked(ShadowState& state, std::size_t cohortIdx, std::uint32_t nowSec)
{
    Closed out;
    if (!state.open)
        return out;
    state.open = false;
    CountBundle(g_min[cohortIdx], state.pending);
    LineKind const kind = LineFor(ClassifyBundle(state.pending));
    if (kind == LineKind::None)
        return out;
    if (ai::rot_cast::BudgetTake(g_window, g_used, nowSec, LinesPerMinute))
    {
        ++g_min[cohortIdx].lines;
        out.kind = kind;
        out.bundle = state.pending;
        out.from = state.from;
        out.to = state.last;
    }
    else
        ++g_min[cohortIdx].dropped;
    return out;
}

struct MinuteOut
{
    bool due = false;
    bool active[2] = {false, false};
    ArbiterMinute sums[2];
};

// Under g_mutex: at the first tick after a minute change, takes the sums of the minute before and clears them.
MinuteOut RollLocked(std::uint32_t nowSec)
{
    MinuteOut out;
    std::uint32_t const minute = nowSec / 60;
    if (g_minute == 0)
    {
        g_minute = minute;
        return out;
    }
    if (minute == g_minute)
        return out;
    g_minute = minute;
    out.due = true;
    for (std::size_t i = 0; i < 2; ++i)
    {
        out.active[i] = g_active[i];
        out.sums[i] = g_min[i];
        g_min[i] = ArbiterMinute();
        g_active[i] = false;
    }
    return out;
}

// Outside the lock; formats only from the copies in Closed.
void Emit(Closed const& closed, std::uint32_t guid)
{
    if (closed.kind == LineKind::None)
        return;
    ShadowLine line;
    line.guid = guid;
    line.cohort = cohort::Letter(guid);
    line.from = closed.from.name;
    line.to = closed.to.name;
    char buf[512];
    if (closed.kind == LineKind::Expire)
        FormatExpireLine(buf, sizeof(buf), line, closed.bundle);
    else
        FormatWriteLine(buf, sizeof(buf), line, closed.bundle);
    sLog.outBasic("%s", buf);
}

void EmitMinute(MinuteOut const& minute)
{
    if (!minute.due)
        return;
    for (std::size_t i = 0; i < 2; ++i)
    {
        if (!minute.active[i])
            continue;
        char buf[512];
        FormatArbiterMin(buf, sizeof(buf), i ? 'B' : 'A', minute.sums[i]);
        sLog.outBasic("%s", buf);
    }
}
}

ThreadTag::ThreadTag(ThreadKind kind, PlayerbotAI const* owner, bool keepSet) : prevKind_(t_tag.kind), prevOwner_(t_tag.owner)
{
    if (keepSet && t_tag.kind != ThreadKind::None)
        return;
    t_tag.kind = kind;
    t_tag.owner = owner;
}

ThreadTag::~ThreadTag()
{
    t_tag.kind = prevKind_;
    t_tag.owner = prevOwner_;
}

WriterScope::WriterScope(WriterId writer) : WriterScope(writer, false)
{
}

WriterScope::WriterScope(WriterId writer, bool onlyIfEmpty) : pushed_(!onlyIfEmpty || t_depth == 0)
{
    if (!pushed_)
        return;
    if (t_depth < kWriterDepth)
        t_writers[t_depth] = writer;
    ++t_depth;
}

WriterScope WriterScope::IfEmpty(WriterId writer)
{
    return WriterScope(writer, true);
}

WriterScope::~WriterScope()
{
    if (pushed_ && t_depth)
        --t_depth;
}

void ShadowObserve(PlayerbotAI* ai, TravelTarget* target)
{
    if (!g_armed.load(std::memory_order_relaxed) || !ai || !target)
        return;

    Tag const tag = t_tag;
    if (tag.kind == ThreadKind::None || (tag.kind == ThreadKind::Bot && tag.owner != ai))
    {
        // Build rule 3: the owner's supervisor belongs to the owner's thread - count only, never dereference it here.
        Player* const bot = ai->GetBot();
        std::size_t const cohortIdx = CohortIndex(bot ? bot->GetGUIDLow() : 0);
        std::lock_guard<std::mutex> lock(g_mutex);
        target->NoteShadowGap();
        if (tag.kind == ThreadKind::None)
            ++g_min[cohortIdx].offthreadWrites;
        else
            ++g_min[cohortIdx].crossWrites;
        return;
    }

    Supervisor* const supervisor = ai->GetSupervisor();
    if (!supervisor)
        return;

    TravelDestination const* const dest = target->GetDestination();
    std::uint8_t const status = std::uint8_t(target->GetStatus());
    bool const nullDest = IsNullDest(dest);
    bool const forced = target->IsForced();
    WriterId const writer = CurrentWriter();
    std::uint32_t const nowMs = WorldTimer::getMSTime();
    DestKey now;
    now.id = reinterpret_cast<std::uintptr_t>(dest);
    now.temp = dest && dynamic_cast<TemporaryTravelDestination const*>(dest) != nullptr;
    ShadowState& state = supervisor->Shadow();

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!state.on)
        return;
    bool const gap = target->TakeShadowGap();
    if (!state.init)
    {
        // The first observed write takes the target as it is; an earlier unfollowed write is part of that picture (no gap).
        FillName(now, dest);
        state.init = true;
        state.last = now;
        state.lastStatus = status;
        state.lastNull = nullDest;
        state.destSinceMs = nowMs;
        return;
    }
    if (!state.open)
    {
        BundleStart(state.pending, state.lastStatus, state.lastNull, state.destSinceMs);
        state.from = state.last;
        state.open = true;
    }
    bool const newDest = !SameDest(state.last, now);
    BundleStep(state.pending, status, newDest, !SameDest(state.from, now), nullDest, writer, forced, gap, nowMs);
    if (newDest)
    {
        FillName(now, dest);
        state.last = now;
        state.destSinceMs = nowMs;
    }
    state.lastStatus = status;
    state.lastNull = nullDest;
}

void ShadowTick(PlayerbotAI* ai, ShadowState& state, bool shadowOn)
{
    if (!shadowOn)
    {
        // No shadow on anywhere: this state is empty too (only this bot's own tick turns it on), so no lock at the default.
        if (!g_armed.load(std::memory_order_relaxed))
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        DropLocked(state);
        return;
    }

    Player* const bot = ai ? ai->GetBot() : nullptr;
    if (!bot)
        return;
    std::uint32_t const guid = bot->GetGUIDLow();
    std::size_t const cohortIdx = CohortIndex(guid);
    std::uint32_t const nowSec = std::uint32_t(time(nullptr));

    MinuteOut minute;
    Closed closed;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!state.on)
        {
            state.on = true;
            if (g_on++ == 0)
                g_armed.store(true, std::memory_order_relaxed);
        }
        minute = RollLocked(nowSec);
        g_active[cohortIdx] = true;
        closed = CloseLocked(state, cohortIdx, nowSec);
    }
    EmitMinute(minute);
    Emit(closed, guid);
}

void ShadowRelease(PlayerbotAI* ai, ShadowState& state)
{
    Player* const bot = ai ? ai->GetBot() : nullptr;
    std::uint32_t const guid = bot ? bot->GetGUIDLow() : 0;
    Closed closed;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (state.on && bot)
            closed = CloseLocked(state, CohortIndex(guid), std::uint32_t(time(nullptr)));
        DropLocked(state);
    }
    Emit(closed, guid);
}

void ShadowDrop(ShadowState& state)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    DropLocked(state);
}
}
