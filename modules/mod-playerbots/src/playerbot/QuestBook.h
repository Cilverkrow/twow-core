#pragma once

#include "playerbot/QuestBookPolicy.h"
#include "playerbot/QuestTurnInParkPolicy.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class PlayerbotAI;
class Player;
class Quest;

// A3 step 2, quest book core (QB-K; ADR-0041, OB-00 07.11.2026; AiPlayerbot.QuestBook, default 0). One book per bot, made and
// used on the bot's thread (PlayerbotAI::UpdateAI, the engine pass) for a roster bot on its own (no real player master) while
// the switch applies to it, or in shadow (QuestBook.Shadow = 1) for the other roster bots. In shadow the book only watches the
// log and writes its KPI lines; no caller asks it (PlayerbotAI::ActiveQuestBook is null). Pure rules: QuestBookPolicy.h.
// Plan: ws-10/cli-quest-organic/a3/step2-questbook/QB-BUILD-PLAN-20261107.md.
namespace ai::questbook
{
// The minute counters of the [QuestBookMin] lines (increments, per cohort; reset by the world thread's report).
enum class Count : std::uint8_t
{
    AcceptYes = 0,
    AcceptNoWindow,
    AcceptNoGrey,
    AcceptNoFull,
    AcceptNoExcluded,
    TargetNoWindow,
    TargetNoExcluded,
    TurnInYield,
    CompleteStale,
    LeftLog,
    CreditDropped,
    SourceSingle,    // QB-S: the chosen source of an objective is named / single
    SourceCommon,    // QB-S: the chosen source is a common one
    SourceRegroup,   // QB-S: the choice differs from the old first-fit one
    OrderScored,     // QB-S: a fetch whose winners were put in XP-per-minute order
    BundleRanked,    // QB-B: a fetch whose giver sites (two or more) were put in quests-per-second order
    BundleRegroup,   // QB-B: the best site is not the first-fit one
    BundleCapped,    // QB-B: a fetch with more than MaxSites giver sites (the rest followed unranked)
    Max
};

inline std::atomic<std::uint64_t> counts[2][std::size_t(Count::Max)];   // static storage: zero at start

inline void Note(std::uint32_t guidLow, Count c)
{
    counts[cohort::InB(guidLow) ? 1 : 0][std::size_t(c)].fetch_add(1, std::memory_order_relaxed);
}

// The parameters from the config (one copy, filled at load).
Params ConfigParams();

// The quest's level for a bot of this level (a scaled quest, level <= 0, has the bot's level).
int QuestLevelFor(Quest const* quest, std::uint32_t botLevel);

// The level window of the book for one quest, for the async travel search too (no book, no Player: numbers and the template).
NoReason TemplateWindow(Quest const* quest, std::uint32_t botLevel, std::uint32_t grayLevel);

// QB5: the quest is on QuestBook.Exclude.
bool Excluded(std::uint32_t questId);

class QuestBook
{
public:
    QuestBook(PlayerbotAI* ai, bool shadow);
    ~QuestBook();

    QuestBook(QuestBook const&) = delete;
    QuestBook& operator=(QuestBook const&) = delete;

    // Once per engine pass: the log snapshot (at most every 5 s, or right after a quest packet), new quests (credit 0), quests
    // that left the log (QB8 detection line), and complete quests not turned in (QB4 KPI line).
    void Observe(std::uint32_t nowMs);
    void Release();

    bool Shadow() const { return shadow_; }
    void SetShadow(bool shadow) { shadow_ = shadow; }

    // K1: the accept verdict (counts the answer).
    Answer Accept(Quest const* quest, std::uint32_t freeSlots);
    // K4: one progress notion.
    bool HasProgress(Quest const* quest);
    // The quest actions: kill credit, item credit, quest complete.
    void OnCredit(std::uint32_t questId);
    // twow-repo#724: the quest was rewarded (bot thread: the bot's SMSG_QUESTGIVER_QUEST_COMPLETE from the packet inbox, or
    // PlayerbotAI::NoteQuestRewarded after the bot's own RewardQuest without announce); the next regular snapshot (not
    // forced: logging only, the snapshot timing stays as before) labels it turned_in when it left the log (repeatable quests too).
    void OnRewarded(std::uint32_t questId);

    // QB6/QB7 pair (A3 4.5 (8), QB-S3-ANTWORT-20261106.md sections 4-5; CLI-Projekt 20.11. Q1-Q4 (a)). The book's Close / Park /
    // Episode memory for the planner, which moves its own close memory here later (its own pair). No caller yet, no key: nothing
    // in the game calls these, so there is no runtime effect. Bot thread only, like the rest of the book. Pure rules:
    // QuestBookPolicy.h CloseBook / ParkBook / EpisodeBook.
    // Close: time-bounded, never permanent (permanent only via the QB5 list or a QB2 mark). sourceKey is the caller's opaque key.
    void Close(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs,
        bool dropQuest = false)
    {
        closes_.Close(questId, sourceKey, untilMs, reason, nowMs, dropQuest);
    }
    bool IsClosed(std::uint32_t questId, std::uint32_t sourceKey, std::uint32_t nowMs) const
    {
        return closes_.IsClosed(questId, sourceKey, nowMs);
    }
    std::size_t OpenSources(std::uint32_t questId, std::uint32_t const* sourceKeys, std::size_t n, std::uint32_t nowMs) const
    {
        return closes_.OpenSources(questId, sourceKeys, n, nowMs);
    }
    std::uint8_t CloseDeadCycles(std::uint32_t questId, std::uint32_t sourceKey) const { return closes_.DeadCycles(questId, sourceKey); }
    std::uint8_t CloseCount(std::uint32_t questId, std::uint32_t sourceKey) const { return closes_.Closes(questId, sourceKey); }
    bool DropRequested(std::uint32_t questId, std::uint32_t nowMs) const { return closes_.DropRequested(questId, nowMs); }
    // Park: the book's own park slots. IsParked also reads the bot's turn-in park (TravelTarget::turnInParks, read-only: a const
    // pointer, nothing is written there) when the caller passes it, so the two lists never disagree.
    void Park(std::uint32_t questId, std::uint32_t untilMs, std::uint8_t reason, std::uint32_t nowMs) { parks_.Park(questId, untilMs, reason, nowMs); }
    void Unpark(std::uint32_t questId) { parks_.Unpark(questId); }
    bool IsParked(std::uint32_t questId, std::uint32_t nowMs, turnin_park::Book const* turnInParks = nullptr) const
    {
        return parks_.IsParked(questId, nowMs) || (turnInParks && turnin_park::IsParked(*turnInParks, questId, nowMs));
    }
    std::size_t Parked(std::uint32_t nowMs, std::uint32_t* out, std::size_t max) const { return parks_.Parked(nowMs, out, max); }
    // QB5: the permanent list (read only).
    bool IsExcluded(std::uint32_t questId) const { return Excluded(questId); }
    // QB6 / QB7: one episode with a hard end; while ActiveEpisode is not null the planner does not re-plan.
    bool StartEpisode(std::uint32_t questId, EpisodeKind kind, std::uint32_t durationMs, std::uint32_t nowMs)
    {
        return episode_.Start(questId, kind, durationMs, nowMs);
    }
    bool AdvanceEpisode(std::uint32_t questId, std::uint8_t stage, std::uint32_t nowMs) { return episode_.Advance(questId, stage, nowMs); }
    bool EndEpisode(std::uint32_t questId, EpisodeEnd why) { return episode_.End(questId, why); }
    Episode const* ActiveEpisode(std::uint32_t nowMs) const { return episode_.Active(nowMs); }
    EpisodeEnd LastEpisodeEnd() const { return episode_.LastEnd(); }

private:
    struct Watch
    {
        std::uint32_t questId = 0;
        std::uint32_t completeSinceMs = 0;
        bool staleReported = false;
    };
    static constexpr std::size_t kLog = 32;

    Watch* Find(std::uint32_t questId);

    PlayerbotAI* ai_;
    bool shadow_;
    bool dirty_ = true;
    std::uint32_t lastSnapMs_ = 0;
    CreditBook credits_;
    Watch watch_[kLog];
    std::uint32_t prev_[kLog] = {};
    std::size_t nPrev_ = 0;
    std::uint32_t rewarded_[kLog] = {};   // twow-repo#724: reward notes since the last snapshot
    std::size_t nRewarded_ = 0;
    CloseBook closes_;     // QB6/QB7 pair: no caller yet
    ParkBook parks_;
    EpisodeBook episode_;
};
}

// The world thread's minute report ([QuestBookMin], two lines: cohort A and B). Nothing is written with the switch and the
// shadow off.
void ReportQuestBook();
