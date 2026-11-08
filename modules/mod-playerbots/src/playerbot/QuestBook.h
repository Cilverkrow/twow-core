#pragma once

#include "playerbot/QuestBookPolicy.h"

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
};
}

// The world thread's minute report ([QuestBookMin], two lines: cohort A and B). Nothing is written with the switch and the
// shadow off.
void ReportQuestBook();
