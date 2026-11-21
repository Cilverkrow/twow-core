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

    // A quest seen in the log for the first time: tracked from 0 unless a credit already arrived for it (a kill or item packet
    // between the accept and the book's first snapshot of it keeps its count; OB-10 review of #910/#911). True when it is tracked.
    bool Track(std::uint32_t questId)
    {
        return FindOrAdd(questId) != nullptr;
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
// or a QB2 mark. QB6/QB7 pair (A3 4.5 (8), CLI-Projekt 20.11. Q1-Q3 (a)): the planner's close memory moves here 1:1 later
// (PlannerShadowPolicy.h MemorySlot / Close):
// - sourceKey is opaque: the caller's key (the planner's TargetKey(kind, questId, destId)); the book never builds one;
// - a full memory takes the same quest + key, else the first expired slot in field order, else the slot closed longest ago;
// - an untilMs not after nowMs (closeMs 0) or reason None is no close;
// - deadCycles stays with the key when it is closed again (also after it expired) and rises only on Dead (saturates at 255);
// - closes counts every close of the key the same way (the caller's "2nd close of the same taker -> Park", 06.11. answer 4).
enum class CloseReason : std::uint8_t
{
    None = 0,         // never stored
    Dead = 1,         // values as planner::CloseReason
    R9 = 2,
    Suppressed = 3,
};

class CloseBook
{
public:
    static constexpr std::size_t Slots = 16;

    void Close(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs,
        bool dropQuest = false)
    {
        // untilMs - nowMs must be 1 .. 2^31 - 1 ms, so Before() stays exact (fail-closed: no open-ended close)
        if (!questId || reason == std::uint8_t(CloseReason::None) || !Before(nowMs, untilMs) ||
            std::uint32_t(untilMs - nowMs) >= 0x80000000u)
            return;
        Entry* target = Find(questId, sourceKey);
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
                if (Before(e.closedAtMs, target->closedAtMs))
                    target = &e;
        }
        bool const same = target->questId == questId && target->sourceKey == sourceKey;
        std::uint8_t const dead = same ? target->deadCycles : 0;
        std::uint8_t const closes = same ? target->closes : 0;
        target->questId = questId;
        target->sourceKey = sourceKey;
        target->closedAtMs = nowMs;
        target->untilMs = untilMs;
        target->reason = reason;
        target->dropQuest = dropQuest;
        target->deadCycles = reason == std::uint8_t(CloseReason::Dead) && dead < 255 ? std::uint8_t(dead + 1) : dead;
        target->closes = closes < 255 ? std::uint8_t(closes + 1) : closes;
    }

    bool IsClosed(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t nowMs) const
    {
        Entry const* e = Find(questId, sourceKey);
        return e && Live(*e, nowMs);
    }

    // How many of the quest's sources are not closed; 0 means the quest goes to the park slots. The caller passes its candidate
    // keys (the book has no source list before QB1 / QB3; CLI-Projekt 20.11. Q1 (a)).
    std::size_t OpenSources(std::uint32_t questId, std::uint32_t const* sources, std::size_t n, std::uint32_t nowMs) const
    {
        std::size_t open = 0;
        for (std::size_t i = 0; sources && i < n; ++i)
            if (!IsClosed(questId, sources[i], nowMs))
                ++open;
        return open;
    }

    // The key's counters while its slot is held (also after the close expired); 0 for a key the memory does not hold.
    std::uint8_t DeadCycles(std::uint32_t questId, std::uint32_t sourceKey) const
    {
        Entry const* e = Find(questId, sourceKey);
        return e ? e->deadCycles : 0;
    }

    std::uint8_t Closes(std::uint32_t questId, std::uint32_t sourceKey) const
    {
        Entry const* e = Find(questId, sourceKey);
        return e ? e->closes : 0;
    }

    // A live close of the quest asked to drop it (QB8: the later left-log label; the label itself comes with the caller).
    bool DropRequested(std::uint32_t questId, std::uint32_t nowMs) const
    {
        for (Entry const& e : entries_)
            if (e.questId == questId && e.dropQuest && Live(e, nowMs))
                return true;
        return false;
    }

private:
    struct Entry
    {
        std::uint32_t questId = 0;
        std::uint32_t sourceKey = 0;
        std::uint32_t closedAtMs = 0;
        std::uint32_t untilMs = 0;
        std::uint8_t reason = 0;
        std::uint8_t deadCycles = 0;
        std::uint8_t closes = 0;
        bool dropQuest = false;
    };

    Entry* Find(std::uint32_t questId, std::uint32_t sourceKey)
    {
        for (Entry& e : entries_)
            if (e.questId && e.questId == questId && e.sourceKey == sourceKey)
                return &e;
        return nullptr;
    }

    Entry const* Find(std::uint32_t questId, std::uint32_t sourceKey) const
    {
        for (Entry const& e : entries_)
            if (e.questId && e.questId == questId && e.sourceKey == sourceKey)
                return &e;
        return nullptr;
    }

    static bool Live(Entry const& e, std::uint32_t nowMs)
    {
        return e.questId && Before(nowMs, e.untilMs);
    }

    Entry entries_[Slots];
};

// ---- QB6 / QB7 episode (A3 4.5 (8); QB-S3-ANTWORT-20261106.md section 5) ---------------------------------------------------
// One episode per bot: QB6 (use a GO, then turn in to / accept from a summoned NPC while it lives) or QB7 (escort). While an
// episode is active the planner does not re-plan. Fail-closed: every episode has a hard end; there is no lock without one.
// - Start refuses quest 0, kind None, a duration of 0 and a second live episode (a restart needs End first); a duration above
//   MaxEpisodeMs is cut to it.
// - The end is checked as "elapsed since start < duration" (unsigned: exact across the getMSTime wrap for spans below ~49 days);
//   an episode found expired is cleared on that look.
// - Advance changes only the stage of the live episode of the same quest; it never moves the end.
// - End(quest, why) clears that quest's episode (live or expired) and keeps the reason for the caller's line.
enum class EpisodeKind : std::uint8_t
{
    None = 0,
    GoSummon = 1,   // QB6
    Escort = 2,     // QB7
};

enum class EpisodeEnd : std::uint8_t
{
    None = 0,
    QuestComplete = 1,
    QuestFailed = 2,
    NpcDead = 3,
    TooFar = 4,
    Death = 5,            // the bot died
    CombatLoop = 6,       // R8
    GroupDirective = 7,
    Expired = 8,
};

constexpr std::uint32_t MaxEpisodeMs = 7200000;   // 2 h: the hard cap of any episode (QB7 without a quest time limit included)

struct Episode
{
    std::uint32_t questId = 0;
    EpisodeKind kind = EpisodeKind::None;
    std::uint8_t stage = 0;
    std::uint32_t untilMs = 0;   // startMs + durationMs (for the lines; the check uses the elapsed time)
};

class EpisodeBook
{
public:
    bool Start(std::uint32_t questId, EpisodeKind kind, std::uint32_t durationMs, std::uint32_t nowMs)
    {
        if (!questId || kind == EpisodeKind::None || !durationMs || Active(nowMs))
            return false;
        std::uint32_t const d = durationMs < MaxEpisodeMs ? durationMs : MaxEpisodeMs;
        cur_ = Episode{questId, kind, 0, nowMs + d};
        startMs_ = nowMs;
        durationMs_ = d;
        lastEnd_ = EpisodeEnd::None;
        return true;
    }

    bool Advance(std::uint32_t questId, std::uint8_t stage, std::uint32_t nowMs)
    {
        Episode const* e = Active(nowMs);
        if (!e || e->questId != questId)
            return false;
        cur_.stage = stage;
        return true;
    }

    bool End(std::uint32_t questId, EpisodeEnd why)
    {
        if (!questId || cur_.questId != questId)
            return false;
        cur_ = Episode{};
        lastEnd_ = why;
        return true;
    }

    // nullptr when there is none or it reached its end.
    Episode const* Active(std::uint32_t nowMs) const
    {
        if (!cur_.questId)
            return nullptr;
        if (std::uint32_t(nowMs - startMs_) < durationMs_)
            return &cur_;
        cur_ = Episode{};
        lastEnd_ = EpisodeEnd::Expired;
        return nullptr;
    }

    EpisodeEnd LastEnd() const { return lastEnd_; }

private:
    mutable Episode cur_;
    mutable EpisodeEnd lastEnd_ = EpisodeEnd::None;
    std::uint32_t startMs_ = 0;
    std::uint32_t durationMs_ = 0;
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

// twow-repo#724 (logging only, no switch): Player::GetQuestRewardStatus is false for every repeatable quest (RewardQuest
// sets a repeatable quest to status NONE), so a repeatable turn-in was logged "dropped" (BOT-16 unit 1, 18.11.2026: Blood Shard
// turn-ins). The per-event proof is a reward note on the bot's thread before the snapshot, from two sources:
// - RewardQuest with announce = true sends SMSG_QUESTGIVER_QUEST_COMPLETE (Player::SendQuestReward); it is read from the bot's
//   packet inbox;
// - the bot's own RewardQuest calls with announce = false send no packet, so they note the quest directly:
//   TalkToQuestGiverAction::RewardNoItem (every quest without a reward choice, the Blood Shard quests) and RewardAction.
// Not covered: the core's QUEST_FLAGS_AUTO_REWARDED path in Player::CompleteQuest (announce = false), which stays "dropped" for a
// repeatable quest. The raw m_rewarded flag is no proof: the abandon handler sets status NONE and keeps it.
inline LeftReason ClassifyLeft(bool rewardStatus, bool rewardNote, bool failed)
{
    if (rewardStatus || rewardNote)
        return LeftReason::TurnedIn;
    return failed ? LeftReason::Failed : LeftReason::Dropped;
}

// The reward notes since the last snapshot: at most `cap` quest ids, a repeat is kept once, a full list drops its oldest.
// Returns the new count.
inline std::size_t NoteQuestId(std::uint32_t* ids, std::size_t n, std::size_t cap, std::uint32_t questId)
{
    if (!ids || !questId || !cap)
        return n;
    for (std::size_t i = 0; i < n; ++i)
        if (ids[i] == questId)
            return n;
    if (n >= cap)
    {
        for (std::size_t i = 1; i < cap; ++i)
            ids[i - 1] = ids[i];
        n = cap - 1;
    }
    ids[n++] = questId;
    return n;
}

inline bool HasQuestId(std::uint32_t const* ids, std::size_t n, std::uint32_t questId)
{
    for (std::size_t i = 0; ids && i < n; ++i)
        if (ids[i] == questId)
            return true;
    return false;
}

// ---- QB4 KPI: complete but not turned in ----------------------------------------------------------------------------------
// completeSinceMs 0 = not complete. Report once per quest when it has been complete for at least staleMs.
inline bool ShouldReportStale(std::uint32_t completeSinceMs, std::uint32_t nowMs, std::uint32_t staleMs, bool reported)
{
    return !reported && completeSinceMs != 0 && !Before(nowMs, completeSinceMs) && nowMs - completeSinceMs >= staleMs;
}
}
