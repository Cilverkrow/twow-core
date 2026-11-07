#pragma once

#include "CohortPolicy.h"

#include <cstddef>
#include <cstdint>

// A3 step 2, quest book core (QB-K; ADR-0041, OB-00 07.11.2026; AiPlayerbot.QuestBook, default 0). Plan:
// ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md (886d603a); interface to step 3: QB-S3-ANTWORT
// (5e0dda32), catalog A3-BOT-LOGIK 4.5 (8). One book per bot answers the questions that today five level windows, four progress
// notions and three turn-in checks answer differently (W3, W4, QB4):
//   - one level window for accept, giver travel and objective travel (LevelWindow);
//   - one rule "zone fits" (ZoneFits);
//   - one progress notion (CreditBook: credit_seq per quest, progress is "!=", plus the objective counts);
//   - CanAccept / CanTarget / CanTurnIn with the answers Yes, No(reason) or CheckAtRuntime;
//   - the containers the step-3 planner and R9 use later: park slots, close memory, the permanent exclusion list (QB5, config
//     list QuestBook.Exclude, OB-00 07.11. D1 (a)), the log diff for QB8.
// Pure rules: no game types, no config reads, no locks, no log calls. Every threshold is a parameter (Params), never a constant
// in the callers. Times are uint32 milliseconds of WorldTimer::getMSTime() and compare wrap-safe (Before).
namespace ai::questbook
{
// ---- the switch -----------------------------------------------------------------------------------------------------------
// AiPlayerbot.QuestBook: 0 off (default, rollback), 1 all bots, 2 cohort B (odd low guid), 3 cohort A (even low guid). Any
// other value is 0 (the loader writes one error line). Same cohort rule as the other A3 switches (CohortPolicy.h).
inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

inline std::uint32_t SanitizeMode(std::uint32_t mode)
{
    return ValidMode(mode) ? mode : cohort::ModeOff;
}

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return cohort::Applies(SanitizeMode(mode), guidLow);
}

// ---- answers --------------------------------------------------------------------------------------------------------------
enum class Verdict : std::uint8_t
{
    Yes = 0,
    No = 1,
    CheckAtRuntime = 2,   // a fact the snapshot cannot know (is the giver / taker spawned now?): the caller checks it live
};

enum class NoReason : std::uint8_t
{
    None = 0,
    AboveWindow = 1,    // quest level > bot level + Params::aboveLevels
    Grey = 2,           // quest level <= the bot's grey level
    BelowMinLevel = 3,  // bot level < the quest's MinLevel
    LogFull = 4,        // free log slots <= Params::logFullSlots
    Excluded = 5,       // QB5 permanent list
    Parked = 6,
    Closed = 7,
    ZoneTooHigh = 8,
    NotInLog = 9,
    NotComplete = 10,
    CannotReward = 11,
    NoMoney = 12,
    TakerAbsent = 13,
    GiverAbsent = 14,
};

inline char const* NoReasonName(NoReason reason)
{
    switch (reason)
    {
        case NoReason::None: return "none";
        case NoReason::AboveWindow: return "above_window";
        case NoReason::Grey: return "grey";
        case NoReason::BelowMinLevel: return "below_min_level";
        case NoReason::LogFull: return "log_full";
        case NoReason::Excluded: return "excluded";
        case NoReason::Parked: return "parked";
        case NoReason::Closed: return "closed";
        case NoReason::ZoneTooHigh: return "zone_too_high";
        case NoReason::NotInLog: return "not_in_log";
        case NoReason::NotComplete: return "not_complete";
        case NoReason::CannotReward: return "cannot_reward";
        case NoReason::NoMoney: return "no_money";
        case NoReason::TakerAbsent: return "taker_absent";
        case NoReason::GiverAbsent: return "giver_absent";
    }
    return "unknown";
}

struct Answer
{
    Verdict verdict = Verdict::Yes;
    NoReason reason = NoReason::None;

    static Answer Yes() { return Answer{}; }
    static Answer No(NoReason r) { return Answer{Verdict::No, r}; }
    static Answer Check(NoReason r) { return Answer{Verdict::CheckAtRuntime, r}; }
    bool IsYes() const { return verdict == Verdict::Yes; }
    bool IsNo() const { return verdict == Verdict::No; }
};

// A fact the snapshot may not know.
enum class Tri : std::uint8_t
{
    Unknown = 0,
    No = 1,
    Yes = 2,
};

// ---- parameters (the PREREG sets the values; the .dist values are proposals) ------------------------------------------------
struct Params
{
    int aboveLevels = 1;              // QuestBook.AboveLevels: a quest more than this above the bot is outside the window
    int zoneAboveLevels = 5;          // QuestBook.ZoneAboveLevels: an area more than this above the bot does not fit
    std::uint32_t logFullSlots = 4;   // free slots <= this: no new quest (the value of quest_accept::CleanFreeSlots)
};

// ---- the snapshot of one quest for one bot (numbers only, filled by the adapter) --------------------------------------------
struct Facts
{
    std::uint32_t questId = 0;
    int botLevel = 0;
    int questLevel = 0;        // resolved for the bot (a scaled quest has the bot's level)
    int minLevel = 0;          // Quest::GetMinLevel, 0 = none
    int grayLevel = 0;         // the bot's grey level (XP::GetGrayLevel), 0 = nothing is grey
    std::uint32_t freeSlots = 25;
    bool classQuest = false;   // a class quest is exempt from the window and from the log-full rule, as today
    bool inLog = false;
    bool complete = false;
    bool canReward = false;    // Player::CanRewardQuest(quest, false)
    bool hasMoney = true;      // enough money for the quest's ReqMoney
    bool excluded = false;     // QB5 list
    bool parked = false;
    bool closed = false;
    int areaLevel = 0;         // the level of the quest's area, 0 = unknown (fits)
    Tri giverSpawned = Tri::Unknown;
    Tri takerSpawned = Tri::Unknown;
};

// ---- one level window -----------------------------------------------------------------------------------------------------
// The same window decides accept, giver travel and objective travel: a quest is inside when the bot reached its MinLevel, it is
// above the bot's grey level and at most aboveLevels above the bot. Class quests are always inside.
inline NoReason WindowReason(int botLevel, int questLevel, int minLevel, int grayLevel, bool classQuest, Params const& p)
{
    if (classQuest)
        return NoReason::None;
    if (minLevel > 0 && botLevel < minLevel)
        return NoReason::BelowMinLevel;
    if (grayLevel > 0 && questLevel <= grayLevel)
        return NoReason::Grey;
    if (questLevel > botLevel + p.aboveLevels)
        return NoReason::AboveWindow;
    return NoReason::None;
}

inline bool InWindow(int botLevel, int questLevel, int minLevel, int grayLevel, bool classQuest, Params const& p)
{
    return WindowReason(botLevel, questLevel, minLevel, grayLevel, classQuest, p) == NoReason::None;
}

// ---- one zone rule --------------------------------------------------------------------------------------------------------
// An unknown area level (<= 0) fits; otherwise the area may be at most zoneAboveLevels above the bot.
inline bool ZoneFits(int botLevel, int areaLevel, Params const& p)
{
    return areaLevel <= 0 || areaLevel <= botLevel + p.zoneAboveLevels;
}

// ---- the three predicates -------------------------------------------------------------------------------------------------
inline Answer CanAccept(Facts const& f, Params const& p)
{
    if (f.excluded)
        return Answer::No(NoReason::Excluded);
    NoReason const window = WindowReason(f.botLevel, f.questLevel, f.minLevel, f.grayLevel, f.classQuest, p);
    if (window != NoReason::None)
        return Answer::No(window);
    if (!f.classQuest && f.freeSlots <= p.logFullSlots)
        return Answer::No(NoReason::LogFull);
    return Answer::Yes();
}

enum class Purpose : std::uint8_t
{
    Giver = 0,       // travel to take the quest
    Objective = 1,   // travel to work on it
};

inline Answer CanTarget(Facts const& f, Purpose purpose, Params const& p)
{
    if (f.excluded)
        return Answer::No(NoReason::Excluded);
    if (f.parked)
        return Answer::No(NoReason::Parked);
    if (f.closed)
        return Answer::No(NoReason::Closed);
    NoReason const window = WindowReason(f.botLevel, f.questLevel, f.minLevel, f.grayLevel, f.classQuest, p);
    if (window != NoReason::None)
        return Answer::No(window);
    if (!ZoneFits(f.botLevel, f.areaLevel, p))
        return Answer::No(NoReason::ZoneTooHigh);
    if (purpose == Purpose::Giver)
    {
        if (f.giverSpawned == Tri::No)
            return Answer::No(NoReason::GiverAbsent);
        if (f.giverSpawned == Tri::Unknown)
            return Answer::Check(NoReason::GiverAbsent);
    }
    return Answer::Yes();
}

// A complete quest is always worth handing in: no level window, no zone rule, and the QB5 list does not stop a turn-in.
inline Answer CanTurnIn(Facts const& f)
{
    if (!f.inLog)
        return Answer::No(NoReason::NotInLog);
    if (!f.complete)
        return Answer::No(NoReason::NotComplete);
    if (!f.canReward)
        return Answer::No(NoReason::CannotReward);
    if (!f.hasMoney)
        return Answer::No(NoReason::NoMoney);
    if (f.parked)
        return Answer::No(NoReason::Parked);
    if (f.closed)
        return Answer::No(NoReason::Closed);
    if (f.takerSpawned == Tri::No)
        return Answer::No(NoReason::TakerAbsent);
    if (f.takerSpawned == Tri::Unknown)
        return Answer::Check(NoReason::TakerAbsent);
    return Answer::Yes();
}

// ---- time -----------------------------------------------------------------------------------------------------------------
// Wrap-safe "a is before b" for getMSTime() values (valid for spans below ~24 days).
inline bool Before(std::uint32_t a, std::uint32_t b)
{
    return std::int32_t(a - b) < 0;
}

// ---- one progress notion (credit_seq) -------------------------------------------------------------------------------------
// The quest actions (kill credit, item credit, quest complete) bump the quest's counter; accepting the quest sets it to 0.
// Progress between two looks is "seq differs" (never ">", the counter may wrap). Objective text is no progress.
inline bool Progressed(std::uint32_t before, std::uint32_t now)
{
    return before != now;
}

inline bool HasProgress(std::uint32_t seq, std::uint32_t objectiveCounts)
{
    return seq != 0 || objectiveCounts > 0;
}

class CreditBook
{
public:
    static constexpr std::size_t Slots = 32;   // above the 20-quest log; a full book drops the bump (counted by the caller)

    // Accept (or a quest seen in the log for the first time): the counter starts at 0.
    bool Reset(std::uint32_t questId)
    {
        Entry* e = FindOrAdd(questId);
        if (!e)
            return false;
        e->seq = 0;
        return true;
    }

    bool Bump(std::uint32_t questId)
    {
        Entry* e = FindOrAdd(questId);
        if (!e)
            return false;
        ++e->seq;
        return true;
    }

    std::uint32_t Seq(std::uint32_t questId) const
    {
        for (Entry const& e : entries_)
            if (e.questId == questId)
                return e.seq;
        return 0;
    }

    void Forget(std::uint32_t questId)
    {
        for (Entry& e : entries_)
            if (e.questId == questId)
                e = Entry{};
    }

    std::size_t Used() const
    {
        std::size_t n = 0;
        for (Entry const& e : entries_)
            if (e.questId)
                ++n;
        return n;
    }

private:
    struct Entry
    {
        std::uint32_t questId = 0;
        std::uint32_t seq = 0;
    };

    Entry* FindOrAdd(std::uint32_t questId)
    {
        if (!questId)
            return nullptr;
        Entry* freeSlot = nullptr;
        for (Entry& e : entries_)
        {
            if (e.questId == questId)
                return &e;
            if (!e.questId && !freeSlot)
                freeSlot = &e;
        }
        if (freeSlot)
        {
            freeSlot->questId = questId;
            freeSlot->seq = 0;
        }
        return freeSlot;
    }

    Entry entries_[Slots];
};

// ---- park slots (16, as BookSize of QuestTurnInParkPolicy.h) -----------------------------------------------------------
// A parked quest stays in the log but is no travel target. untilMs 0 = until Unpark. A full book replaces the entry that ends
// first (an open-ended one last).
class ParkBook
{
public:
    static constexpr std::size_t Slots = 16;

    void Park(std::uint32_t questId, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs)
    {
        if (!questId)
            return;
        Entry* target = nullptr;
        for (Entry& e : entries_)
            if (e.questId == questId)
                target = &e;
        if (!target)
            for (Entry& e : entries_)
                if (!Live(e, nowMs))
                {
                    target = &e;
                    break;
                }
        if (!target)
        {
            target = &entries_[0];
            for (Entry& e : entries_)
                if (EndsBefore(e, *target))
                    target = &e;
        }
        target->questId = questId;
        target->untilMs = untilMs;
        target->reason = reason;
    }

    void Unpark(std::uint32_t questId)
    {
        for (Entry& e : entries_)
            if (e.questId == questId)
                e = Entry{};
    }

    bool IsParked(std::uint32_t questId, std::uint32_t nowMs) const
    {
        for (Entry const& e : entries_)
            if (e.questId == questId && Live(e, nowMs))
                return true;
        return false;
    }

    // The parked quest ids (live entries), at most Slots; returns the count.
    std::size_t Parked(std::uint32_t nowMs, std::uint32_t* out, std::size_t max) const
    {
        std::size_t n = 0;
        for (Entry const& e : entries_)
            if (Live(e, nowMs) && out && n < max)
                out[n++] = e.questId;
        return n;
    }

private:
    struct Entry
    {
        std::uint32_t questId = 0;
        std::uint32_t untilMs = 0;
        std::uint8_t reason = 0;
    };

    static bool Live(Entry const& e, std::uint32_t nowMs)
    {
        return e.questId && (e.untilMs == 0 || Before(nowMs, e.untilMs));
    }

    static bool EndsBefore(Entry const& a, Entry const& b)
    {
        if (a.untilMs == 0)
            return false;
        if (b.untilMs == 0)
            return true;
        return Before(a.untilMs, b.untilMs);
    }

    Entry entries_[Slots];
};

// ---- close memory (per quest and source) ----------------------------------------------------------------------------------
// Close(quest, source, until) is a time-bounded memory (R9 / R4 / R5: 7,200 s), never permanent: permanent only via the QB5 list
// or a QB2 mark. A full memory replaces the entry that ends first.
class CloseBook
{
public:
    static constexpr std::size_t Slots = 16;

    void Close(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs)
    {
        if (!questId || !Before(nowMs, untilMs))
            return;
        Entry* target = nullptr;
        for (Entry& e : entries_)
            if (e.questId == questId && e.sourceKey == sourceKey)
                target = &e;
        if (!target)
            for (Entry& e : entries_)
                if (!Live(e, nowMs))
                {
                    target = &e;
                    break;
                }
        if (!target)
        {
            target = &entries_[0];
            for (Entry& e : entries_)
                if (Before(e.untilMs, target->untilMs))
                    target = &e;
        }
        target->questId = questId;
        target->sourceKey = sourceKey;
        target->untilMs = untilMs;
        target->reason = reason;
    }

    bool IsClosed(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t nowMs) const
    {
        for (Entry const& e : entries_)
            if (e.questId == questId && e.sourceKey == sourceKey && Live(e, nowMs))
                return true;
        return false;
    }

    // How many of the quest's sources are not closed; 0 means the quest goes to the park slots.
    std::size_t OpenSources(std::uint32_t questId, std::uint32_t const* sources, std::size_t n, std::uint32_t nowMs) const
    {
        std::size_t open = 0;
        for (std::size_t i = 0; sources && i < n; ++i)
            if (!IsClosed(questId, sources[i], nowMs))
                ++open;
        return open;
    }

private:
    struct Entry
    {
        std::uint32_t questId = 0;
        std::uint32_t sourceKey = 0;
        std::uint32_t untilMs = 0;
        std::uint8_t reason = 0;
    };

    static bool Live(Entry const& e, std::uint32_t nowMs)
    {
        return e.questId && Before(nowMs, e.untilMs);
    }

    Entry entries_[Slots];
};

// ---- QB8 detection: which quests left the log between two snapshots ---------------------------------------------------------
// Writes the ids that are in prev but not in now into out (at most max); returns the count. Order follows prev.
inline std::size_t LeftLog(std::uint32_t const* prev, std::size_t nPrev, std::uint32_t const* now, std::size_t nNow,
    std::uint32_t* out, std::size_t max)
{
    std::size_t n = 0;
    for (std::size_t i = 0; prev && i < nPrev; ++i)
    {
        if (!prev[i])
            continue;
        bool still = false;
        for (std::size_t j = 0; now && j < nNow && !still; ++j)
            still = now[j] == prev[i];
        if (!still && out && n < max)
            out[n++] = prev[i];
    }
    return n;
}

enum class LeftReason : std::uint8_t
{
    Unknown = 0,
    TurnedIn = 1,   // rewarded
    Dropped = 2,    // status none, not rewarded
    Failed = 3,
};

inline char const* LeftReasonName(LeftReason reason)
{
    switch (reason)
    {
        case LeftReason::Unknown: return "unknown";
        case LeftReason::TurnedIn: return "turned_in";
        case LeftReason::Dropped: return "dropped";
        case LeftReason::Failed: return "failed";
    }
    return "unknown";
}

// ---- QB4 KPI: complete but not turned in ----------------------------------------------------------------------------------
// completeSinceMs 0 = not complete. Report once per quest when it has been complete for at least staleMs.
inline bool ShouldReportStale(std::uint32_t completeSinceMs, std::uint32_t nowMs, std::uint32_t staleMs, bool reported)
{
    return !reported && completeSinceMs != 0 && !Before(nowMs, completeSinceMs) && nowMs - completeSinceMs >= staleMs;
}
}
