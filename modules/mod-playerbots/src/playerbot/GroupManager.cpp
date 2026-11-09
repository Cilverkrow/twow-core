#include "playerbot/playerbot.h"
#include "playerbot/GroupManager.h"
#include "playerbot/ManagerInbox.h"
#include "playerbot/SupervisorPolicy.h"   // T30-0: supervisor::Applies for the shadow candidates (no postbox)

namespace ai::group_mgr
{
namespace
{
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

// Scaffold (A3 step 6): no decision, no group read or write. The next pairs fill this pass in.
void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t /*now*/)
{
    ++passes_;
    DrainInbox(bots, WorldTimer::getMSTime());
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
        case manager_inbox::Kind::GroupWish:
            wishes_[event.botLow] = manager_inbox::WishSnapshotOf(event, nowMs);
            break;
        default:
            break;   // counted only (DirectiveAck: no directive goes out yet; no sender for the others)
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

    // (1) drain, (2) tables.
    inboxBuf_.clear();
    manager_inbox::Drain(inboxBuf_, manager_inbox::kDrainMaxPerPass);
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
    std::uint32_t const supervisorMode = sPlayerbotAIConfig.supervisor;
    line.shadow = manager_inbox::CountShadow(status_, edges_, wishes_,
        [supervisorMode](std::uint32_t low) { return ai::supervisor::Applies(supervisorMode, low); }, nowMs,
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
}
