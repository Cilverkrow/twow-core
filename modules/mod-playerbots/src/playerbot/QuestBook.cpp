#include "playerbot/playerbot.h"
#include "playerbot/QuestBook.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Formulas.h"

#include <algorithm>
#include <ctime>

namespace ai::questbook
{
Params ConfigParams()
{
    Params p;
    p.aboveLevels = int(sPlayerbotAIConfig.questBookAboveLevels);
    return p;
}

int QuestLevelFor(Quest const* quest, std::uint32_t botLevel)
{
    // the rule of Player::GetQuestLevelForPlayer, without a Player (the async travel search has none)
    return quest && quest->GetQuestLevel() > 0 ? int(quest->GetQuestLevel()) : int(botLevel);
}

NoReason TemplateWindow(Quest const* quest, std::uint32_t botLevel, std::uint32_t grayLevel)
{
    if (!quest)
        return NoReason::None;
    return WindowReason(int(botLevel), QuestLevelFor(quest, botLevel), int(quest->GetMinLevel()), int(grayLevel),
        quest->GetRequiredClasses() != 0, ConfigParams());
}

bool Excluded(std::uint32_t questId)
{
    return sPlayerbotAIConfig.questBookExclude.count(questId) != 0;   // filled at load, read only afterwards (any thread)
}

QuestBook::QuestBook(PlayerbotAI* ai, bool shadow) : ai_(ai), shadow_(shadow)
{
}

QuestBook::~QuestBook() = default;

void QuestBook::Release()
{
    // The book owns nothing of the bot: the log, the travel target and the park book of TravelTarget stay as they are.
}

QuestBook::Watch* QuestBook::Find(std::uint32_t questId)
{
    for (Watch& w : watch_)
        if (w.questId == questId)
            return &w;
    return nullptr;
}

void QuestBook::Observe(std::uint32_t nowMs)
{
    if (!dirty_ && lastSnapMs_ && Before(nowMs, lastSnapMs_ + 5000))
        return;
    dirty_ = false;
    lastSnapMs_ = nowMs ? nowMs : 1;

    Player* bot = ai_->GetBot();
    if (!bot)
        return;
    std::uint32_t const guid = bot->GetGUIDLow();
    char const* const modeText = shadow_ ? "shadow" : "active";

    std::uint32_t now[kLog];
    std::size_t n = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE && n < kLog; ++slot)
        if (std::uint32_t const questId = bot->GetQuestSlotQuestId(slot))
            now[n++] = questId;

    // A quest seen for the first time: tracked from 0 (every accept path, without a hook in each), keeping a credit that came in
    // before this snapshot (the book forgets a quest when it leaves the log, so a kept count belongs to this accept).
    for (std::size_t i = 0; i < n; ++i)
    {
        if (Find(now[i]))
            continue;
        credits_.Track(now[i]);
        if (Watch* w = Find(0))
        {
            *w = Watch{};
            w->questId = now[i];
        }
    }

    // QB8 detection: a quest that left the log (no cleanup in QB-K).
    std::uint32_t left[kLog];
    std::size_t const nLeft = LeftLog(prev_, nPrev_, now, n, left, kLog);
    for (std::size_t i = 0; i < nLeft; ++i)
    {
        LeftReason const reason = bot->GetQuestRewardStatus(left[i]) ? LeftReason::TurnedIn :
            bot->GetQuestStatus(left[i]) == QUEST_STATUS_FAILED ? LeftReason::Failed : LeftReason::Dropped;
        sLog.outBasic("[QuestBook] state=left_log bot=%s guid=%u cohort=%c quest=%u reason=%s mode=%s", bot->GetName(), guid,
            cohort::Letter(guid), left[i], LeftReasonName(reason), modeText);
        Note(guid, Count::LeftLog);
        credits_.Forget(left[i]);
        if (Watch* w = Find(left[i]))
            *w = Watch{};
    }

    // QB4 KPI: complete but not turned in for StaleCompleteSeconds.
    std::uint32_t const staleMs = sPlayerbotAIConfig.questBookStaleCompleteSeconds * 1000u;
    for (Watch& w : watch_)
    {
        if (!w.questId)
            continue;
        if (bot->GetQuestStatus(w.questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(w.questId))
        {
            w.completeSinceMs = 0;
            w.staleReported = false;
            continue;
        }
        if (!w.completeSinceMs)
            w.completeSinceMs = lastSnapMs_;
        if (ShouldReportStale(w.completeSinceMs, lastSnapMs_, staleMs, w.staleReported))
        {
            w.staleReported = true;
            sLog.outBasic("[QuestBook] state=complete_stale bot=%s guid=%u cohort=%c quest=%u age_s=%u mode=%s", bot->GetName(), guid,
                cohort::Letter(guid), w.questId, (lastSnapMs_ - w.completeSinceMs) / 1000u, modeText);
            Note(guid, Count::CompleteStale);
        }
    }

    std::copy(now, now + n, prev_);
    nPrev_ = n;
}

Answer QuestBook::Accept(Quest const* quest, std::uint32_t freeSlots)
{
    Player* bot = ai_->GetBot();
    Facts f;
    f.questId = quest->GetQuestId();
    f.botLevel = int(bot->GetLevel());
    f.questLevel = int(bot->GetQuestLevelForPlayer(quest));
    f.minLevel = int(quest->GetMinLevel());
    f.grayLevel = int(MaNGOS::XP::GetGrayLevel(bot->GetLevel()));
    f.freeSlots = freeSlots;
    f.classQuest = quest->GetRequiredClasses() != 0;
    f.excluded = Excluded(f.questId);
    Answer const a = CanAccept(f, ConfigParams());
    std::uint32_t const guid = bot->GetGUIDLow();
    switch (a.reason)
    {
        case NoReason::None: Note(guid, Count::AcceptYes); break;
        case NoReason::Grey: Note(guid, Count::AcceptNoGrey); break;
        case NoReason::LogFull: Note(guid, Count::AcceptNoFull); break;
        case NoReason::Excluded: Note(guid, Count::AcceptNoExcluded); break;
        default: Note(guid, Count::AcceptNoWindow); break;   // above the window or below MinLevel
    }
    return a;
}

bool QuestBook::HasProgress(Quest const* quest)
{
    Player* bot = ai_->GetBot();
    std::uint32_t const questId = quest->GetQuestId();
    if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        return true;
    auto& statusMap = bot->getQuestStatusMap();
    auto const it = statusMap.find(questId);
    std::uint32_t counts = 0;
    if (it != statusMap.end())
        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            counts += it->second.m_itemcount[i] + it->second.m_creatureOrGOcount[i];
    return ai::questbook::HasProgress(credits_.Seq(questId), counts);
}

void QuestBook::OnCredit(std::uint32_t questId)
{
    dirty_ = true;
    if (!credits_.Bump(questId))
        Note(ai_->GetBot() ? ai_->GetBot()->GetGUIDLow() : 0, Count::CreditDropped);
}
}

void ReportQuestBook()
{
    static uint32 lastReport = 0;
    uint32 const now = uint32(time(nullptr));
    if ((!sPlayerbotAIConfig.questBook && !sPlayerbotAIConfig.questBookShadow) || now < lastReport + 60)
        return;
    lastReport = now;
    using ai::questbook::Count;
    for (std::size_t c = 0; c < 2; ++c)
    {
        auto take = [c](Count k) { return (unsigned long long)ai::questbook::counts[c][std::size_t(k)].exchange(0, std::memory_order_relaxed); };
        sLog.outBasic("[QuestBookMin] cohort=%c mode=%u shadow=%u accept_yes=%llu accept_no_window=%llu accept_no_grey=%llu "
            "accept_no_full=%llu accept_no_excl=%llu target_no_window=%llu target_no_excl=%llu turnin_yield=%llu complete_stale=%llu "
            "left_log=%llu credit_dropped=%llu source_single=%llu source_common=%llu source_regroup=%llu order_scored=%llu "
            "bundle_ranked=%llu bundle_regroup=%llu bundle_capped=%llu",
            c == 0 ? 'A' : 'B', sPlayerbotAIConfig.questBook, sPlayerbotAIConfig.questBookShadow ? 1u : 0u,
            take(Count::AcceptYes), take(Count::AcceptNoWindow), take(Count::AcceptNoGrey), take(Count::AcceptNoFull),
            take(Count::AcceptNoExcluded), take(Count::TargetNoWindow), take(Count::TargetNoExcluded), take(Count::TurnInYield),
            take(Count::CompleteStale), take(Count::LeftLog), take(Count::CreditDropped), take(Count::SourceSingle),
            take(Count::SourceCommon), take(Count::SourceRegroup), take(Count::OrderScored), take(Count::BundleRanked),
            take(Count::BundleRegroup), take(Count::BundleCapped));
    }
}
