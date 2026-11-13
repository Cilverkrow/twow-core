#include "playerbot/playerbot.h"
#include "playerbot/GroupManager.h"
#include "playerbot/ManagerInbox.h"
#include "playerbot/SupervisorPolicy.h"   // T30-0: supervisor::Applies for the shadow candidates
#include "playerbot/Supervisor.h"         // T30-1: the one directive post (R10)
#include "playerbot/SupervisorShadow.h"   // T30-1c: supervisor::ArbiterValue (the live arbiter value)
#include "playerbot/GroupManagerView.h"
#include "playerbot/GroupOps.h"           // T30-1: QueueManagerJoin / QueueManagerLeave / TakeManagerOutcomes
#include "playerbot/QuestGroupWorld.h"    // T30-1: S7 still owns a bot (quest_groups::OwnsBot)

#include <chrono>

namespace ai::group_mgr
{
namespace
{
// R10 (A3-S3-T30-SCHNITTSTELLE): the supervisor applies to the bot. A directive goes only to such a bot; a ring nobody drains
// would make MailWaiting() true for every bot.
bool SupervisorOn(std::uint32_t low)
{
    return sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, low);
}

// T30-1c: the live owner switch - Types after the StatusSeconds rule, and only while the arbiter enforces (LiveTypes,
// GroupManagerPolicy.h). The arbiter value is global (one relaxed load plus the key, any thread; the console override of
// "rndbot arbiter" included), so it gates every candidate and the bot-thread view (OwnsBot) alike.
std::uint32_t LiveTypesNow()
{
    return LiveTypes(sPlayerbotAIConfig.botGroupsManagerTypes, sPlayerbotAIConfig.botGroupsManagerStatusSeconds,
        ai::supervisor::ArbiterValue(sPlayerbotAIConfig.supervisorArbiter), sPlayerbotAIConfig.supervisor);
}

// T33: the meeting verdict rules, one source for the shadow (T33-0) and the lever (T33-1). F7 (a): S7's 2D radius, no z (kZUnknown),
// the instance only on instanceable maps; F12 (a): S7 checks no alive; snapshots at most StatusSeconds plus one pass old.
GatherRules GatherRulesNow()
{
    GatherRules rules;
    rules.radius = kGatherYards;
    rules.zBand = kZUnknown;
    rules.deadCounts = true;
    rules.maxAgeMs = PlaceMaxAgeMs(sPlayerbotAIConfig.botGroupsManagerStatusSeconds, sPlayerbotAIConfig.randomBotUpdateInterval);
    return rules;
}

// The engine's I/O on the world thread: guids only (the op path looks the bots up itself, m2), the one Post, the log.
class WorldIo : public Io
{
public:
    bool SupervisorApplies(std::uint32_t low) override { return SupervisorOn(low); }
    bool S7Owns(std::uint32_t low) override { return quest_groups::OwnsBot(low); }
    bool QueueJoin(std::uint32_t invitee, std::uint32_t inviter, std::uint32_t groupId, GroupType type, std::uint32_t questId,
        std::uint8_t objective) override
    {
        return group_ops::QueueManagerJoin(invitee, inviter, groupId, type, questId, objective);
    }
    bool QueueLeave(std::uint32_t bot, std::uint32_t groupId, std::uint32_t leaderLow) override
    {
        return group_ops::QueueManagerLeave(bot, groupId, leaderLow);
    }
    bool PostDirective(std::uint32_t low, ai::supervisor::Event const& event) override
    {
        if (sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, low))
            return ai::supervisor::Post(low, event);
        return false;
    }
    void Line(char const* line) override { sLog.outBasic("%s", line); }
};

// A bot that logged out or left the roster posts no more: drop its entry (map keys only, no Player* read).
template <typename Table>
void DropGone(Table& table, std::map<std::uint32_t, Player*> const& bots)
{
    for (auto it = table.begin(); it != table.end();)
    {
        if (bots.find(it->first) == bots.end())
            it = table.erase(it);
        else
            ++it;
    }
}
}

// A3 step 6: the inbox drain (T30-0), then the directive pass (T30-1, design 2.2; a no-op while Types is 0 and nothing is held),
// then the meeting shadow (T33-0; a no-op while Gather is 0). The manager clock moves first (T33 2.5): every T33 time of this
// pass is on it, and a GatherPlace drained in this pass counts before any shadow deadline.
void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now)
{
    ++passes_;
    std::uint32_t const nowMs = WorldTimer::getMSTime();
    mgrMs_ = AdvanceClock(mgrMs_, clockLastMs_, nowMs, sPlayerbotAIConfig.randomBotUpdateInterval);
    DrainInbox(bots, nowMs);
    RunDirectives(bots, nowMs, now);
    RunGather(nowMs);
}

void WorldThreadManager::RunDirectives(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs, std::uint32_t nowSec)
{
    std::uint32_t const types = LiveTypesNow();
    if (!types && !engine_.HasGroups())
    {
        acks_.clear();
        return;
    }
    auto const start = std::chrono::steady_clock::now();
    // (2) the op outcomes of the manager's ops since the last pass.
    outcomes_.clear();
    group_ops::TakeManagerOutcomes(outcomes_);
    Rules rules;
    rules.mode = sPlayerbotAIConfig.botGroupsManager;
    rules.types = types;
    rules.statusSeconds = sPlayerbotAIConfig.botGroupsManagerStatusSeconds;
    rules.ackTimeoutSeconds = sPlayerbotAIConfig.botGroupsManagerAckTimeoutSeconds;   // T30-1f
    rules.intervalMs = sPlayerbotAIConfig.randomBotUpdateInterval;   // ms
    rules.maxBots = sPlayerbotAIConfig.botGroupsMaxBots;
    rules.levelWindow = sPlayerbotAIConfig.botGroupsLevelWindow;
    rules.adhocRadius = sPlayerbotAIConfig.botGroupsAdhocRadius;
    rules.pairCooldownSeconds = sPlayerbotAIConfig.botGroupsAdhocPairCooldownSeconds;
    rules.helpUntilDone = sPlayerbotAIConfig.botGroupsAdhocHelpUntilDone && sPlayerbotAIConfig.botGroupsAdhocWorldThread;   // F5 (a)
    rules.leaderOtherMapMode = sPlayerbotAIConfig.botGroupsLeaderOtherMap;   // F11 (a)
    rules.leaderOtherMapSeconds = sPlayerbotAIConfig.botGroupsLeaderOtherMapSeconds;
    rules.questMaxSize = sPlayerbotAIConfig.questGroupsMaxSize;
    rules.questMaxSizeDrop = sPlayerbotAIConfig.questGroupsMaxSizeDrop;
    rules.questLevelWindow = sPlayerbotAIConfig.questGroupsLevelWindow;
    rules.recruitRadius = sPlayerbotAIConfig.questGroupsRecruitRadius;
    rules.maxAttempts = sPlayerbotAIConfig.questGroupsMaxAttempts;
    rules.retryCooldownMinutes = sPlayerbotAIConfig.questGroupsRetryCooldownMinutes;
    rules.timers.rendezvousMinutes = sPlayerbotAIConfig.questGroupsRendezvousMinutes;
    rules.timers.noProgressMinutes = sPlayerbotAIConfig.questGroupsNoProgressMinutes;
    rules.timers.turnInWaitMinutes = sPlayerbotAIConfig.questGroupsTurnInWaitMinutes;
    rules.timers.maxQuestMinutes = sPlayerbotAIConfig.questGroupsMaxQuestMinutes;
    PassInput in;
    in.nowMs = nowMs;
    in.nowSec = nowSec;
    in.resumed = resumedPass_;
    in.status = &status_;
    in.edges = &edges_;
    in.wishes = &wishes_;
    in.acks = &acks_;
    in.outcomes = &outcomes_;
    in.keyOverflow = manager_inbox::Read().keyOverflow;   // 5.3 key_overflow (the engine prints its increment)
    // T33-1: the lever acts only with Gather.Lever 1 (the loader keeps it 0 while Gather is 0) on the Gather arm's QUEST groups.
    in.gather = sPlayerbotAIConfig.botGroupsManagerGather;
    in.gatherLever = sPlayerbotAIConfig.botGroupsManagerGatherLever;
    in.mgrMs = mgrMs_;
    in.places = &places_;
    in.gatherRules = GatherRulesNow();
    // A bot that left the bot map (logout, roster release) can take no op: forgotten (map keys only). Not after a resume (M16).
    if (!resumedPass_)
        engine_.ForgetGone([&bots](std::uint32_t low) { return bots.find(low) != bots.end(); });
    WorldIo io;
    engine_.Pass(in, rules, io);
    acks_.clear();
    engine_.NotePassUs(std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count()));
}

void WorldThreadManager::RunGather(std::uint32_t nowMs)
{
    // The loader keeps Gather 0 while Types is 0 (EffectiveGather).
    std::uint32_t const gather = sPlayerbotAIConfig.botGroupsManagerGather;
    if (!gather)
    {
        if (!places_.empty())
            places_.clear();
        gather_.Off();
        return;
    }
    // After an off gap the table dates from before the off time, like status_ and edges_ (this pass stored nothing).
    if (resumedPass_)
        places_.clear();
    ShadowInput in;
    in.nowMs = nowMs;
    in.mgrMs = mgrMs_;
    in.resumed = resumedPass_;
    in.gather = gather;
    in.rendezvousMinutes = sPlayerbotAIConfig.questGroupsRendezvousMinutes;
    in.rules = GatherRulesNow();
    in.status = &status_;
    in.places = &places_;
    in.swept = &engine_.Swept();   // groups this directive pass ended and swept: their meetings still get a verdict
    in.gatherLever = sPlayerbotAIConfig.botGroupsManagerGatherLever;   // T33-1: the lever's part of minute3 (read only)
    in.lever = &engine_.Lever();
    in.leverMeetings = engine_.MeetingsOpen();
    WorldIo io;
    gather_.Pass(engine_.Groups(), in, [&io](char const* line) { io.Line(line); });
}

void WorldThreadManager::StoreEvent(manager_inbox::Event const& event, std::uint32_t nowMs)
{
    switch (event.kind)
    {
        case manager_inbox::Kind::Status:
            status_[event.botLow] = manager_inbox::SnapshotOf(event, nowMs);
            // The bot posts its wish right after its status (same thread, FIFO): no wish behind this status = no wish now.
            wishes_.erase(event.botLow);
            break;
        case manager_inbox::Kind::GroupEdge:
            edges_[event.botLow] = manager_inbox::EdgeSnapshotOf(event, nowMs);
            break;
        case manager_inbox::Kind::DirectiveAck:
            // T30-1: the supervisor's answer to a directive (3.4); the engine applies it in this pass (step 3).
            acks_.push_back(AckOf(event));
            break;
        case manager_inbox::Kind::GatherPlace:
            // T33-0: the bot's own place for the meeting shadow; kept only while Gather is on (no table when 0).
            if (sPlayerbotAIConfig.botGroupsManagerGather)
                gather_.StorePlace(places_, status_, event, nowMs,
                    PlaceMaxAgeMs(sPlayerbotAIConfig.botGroupsManagerStatusSeconds, sPlayerbotAIConfig.randomBotUpdateInterval));
            break;
        case manager_inbox::Kind::GroupWish:
            wishes_[event.botLow] = manager_inbox::WishSnapshotOf(event, nowMs);
            break;
        default:
            break;   // counted only (no sender for the others)
    }
}

void WorldThreadManager::DrainInbox(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs)
{
    // The manager was off (BotGroups.Manager reloaded 1 -> 0 -> 1): the tables, the window and what is left in the ring date
    // from before the off time. Start over; this pass counts its events but stores nothing (receivedMs would make them look
    // fresh).
    bool const resumed = minuteStarted_ && manager_inbox::ResumedAfterGap(lastPassMs_, nowMs);
    lastPassMs_ = nowMs;
    if (resumed)
    {
        minuteStarted_ = false;
        status_.clear();
        edges_.clear();
        wishes_.clear();
    }
    // T30-1 (M16): the registry stays; the directive pass of this pass restarts its anchors and ends nothing.
    resumedPass_ = resumed;

    // After a resume the whole ring dates from before the off time: this pass takes all of it and stores none (a leftover
    // drained in a later pass would get receivedMs = now and look fresh). Bounded by the ring's capacity, so bots posting
    // meanwhile cannot keep the loop going.
    // (1) drain, (2) tables.
    inboxBuf_.clear();
    for (;;)
    {
        std::size_t const taken = manager_inbox::Drain(inboxBuf_, manager_inbox::kDrainMaxPerPass);
        if (!resumed || !taken || inboxBuf_.size() >= manager_inbox::Capacity)
            break;
    }
    for (manager_inbox::Event const& event : inboxBuf_)
    {
        ++kindTotals_[manager_inbox::KindIndex(event.kind)];
        // FIFO and one sender thread per bot: the last event of a kind drained is the bot's latest.
        if (!resumed)
            StoreEvent(event, nowMs);
    }

    // (3) the minute line. The window opens on the first pass (no line for a partial minute), like the GroupOps minute line.
    // The baseline is taken here, so the first line counts only its own window (no backlog from before a resume).
    if (!minuteStarted_)
    {
        minuteStarted_ = true;
        minuteStartMs_ = nowMs;
        seen_ = manager_inbox::Read();
        statusSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::Status)];
        ackSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::DirectiveAck)];
        edgeSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::GroupEdge)];
        wishSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::GroupWish)];
        return;
    }
    if (std::uint32_t(nowMs - minuteStartMs_) < 60000)
        return;
    minuteStartMs_ = nowMs;

    DropGone(status_, bots);
    DropGone(edges_, bots);
    DropGone(wishes_, bots);
    DropGone(places_, bots);

    manager_inbox::Counters const totals = manager_inbox::Read();
    manager_inbox::MinuteLine line;
    line.posted = manager_inbox::TakeDelta(totals.posted, seen_.posted);
    line.dropped = manager_inbox::TakeDelta(totals.dropped, seen_.dropped);
    line.drained = manager_inbox::TakeDelta(totals.drained, seen_.drained);
    line.status = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::Status)], statusSeen_);
    line.ack = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::DirectiveAck)], ackSeen_);
    line.edge = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::GroupEdge)], edgeSeen_);
    line.wish = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::GroupWish)], wishSeen_);
    line.keyOverflow = manager_inbox::TakeDelta(totals.keyOverflow, seen_.keyOverflow);
    line.snapshots = status_.size();
    line.edges = edges_.size();
    line.wishes = wishes_.size();
    // T30-0 shadow candidates (design 2.4, measurement only): who the manager could group now. Counted from the tables, no op.
    line.shadow = manager_inbox::CountShadow(status_, edges_, wishes_,
        [](std::uint32_t low) { return sPlayerbotAIConfig.supervisor && ai::supervisor::Applies(sPlayerbotAIConfig.supervisor, low); }, nowMs,
        manager_inbox::StaleMs(sPlayerbotAIConfig.botGroupsManagerStatusSeconds));
    // StatusSeconds 0 and no ack yet: a silent minute, no line (the deltas above are still taken).
    if (manager_inbox::MinuteLineDue(line))
        manager_inbox::LogMinute(line);
}

WorldThreadManager& WorldManager()
{
    static WorldThreadManager manager;
    return manager;
}

// ---- T30-1 read views (GroupManagerView.h): any thread; the registry view answers from one atomic while it is empty ----------
bool OwnsBot(std::uint32_t guidLow)
{
    // T30-1c: below arbiter 2 the manager owns no bot (Types acts like 0), so the legacy paths stay open for the cohort.
    // SupervisorOn trap: a bot without the supervisor gets no directive, so the manager does not own it either.
    return OwnsLive(sPlayerbotAIConfig.botGroupsManager, LiveTypesNow(), sPlayerbotAIConfig.supervisor, guidLow);
}

bool IsManaged(std::uint32_t guidLow)
{
    ViewEntry v;
    return WorldManager().Directives().View().Find(guidLow, v);
}

bool IsManaged(std::uint32_t guidLow, std::uint16_t serial)
{
    return WorldManager().Directives().View().IsMember(guidLow, serial);
}

bool Blocks(std::uint32_t guidLow)
{
    return OwnsBot(guidLow) || IsManaged(guidLow);
}

std::uint8_t QuestRoleOf(std::uint32_t guidLow)
{
    ViewEntry v;
    return WorldManager().Directives().View().Find(guidLow, v) ? v.questRole : std::uint8_t(0);
}

bool IsQuestMember(std::uint32_t guidLow)
{
    return QuestRoleOf(guidLow) != 0;
}

bool IsQuestFollower(std::uint32_t guidLow)
{
    return QuestRoleOf(guidLow) == 1;
}

std::uint32_t QueuedAge(std::uint32_t guidLow, std::uint32_t questId)
{
    return WorldManager().Directives().QueuedAge(guidLow, questId, std::uint32_t(time(nullptr)));
}

bool ManagedKeyOf(std::uint32_t coreGroupId, std::uint32_t& questId, std::int8_t& objective)
{
    ViewEntry v;
    if (!WorldManager().Directives().View().KeyOf(coreGroupId, v))
        return false;
    questId = v.questId;
    objective = v.objective;
    return true;
}
}
