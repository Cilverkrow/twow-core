#include "playerbot/playerbot.h"
#include "playerbot/GroupManager.h"
#include "playerbot/ManagerInbox.h"

namespace ai::group_mgr
{
// Scaffold (A3 step 6): no decision, no group read or write. The next pairs fill this pass in.
void WorldThreadManager::Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t /*now*/)
{
    ++passes_;
    DrainInbox(bots, WorldTimer::getMSTime());
}

void WorldThreadManager::DrainInbox(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs)
{
    // The manager was off (BotGroups.Manager reloaded 1 -> 0 -> 1): the table, the window and what is left in the ring date
    // from before the off time. Start over; this pass counts its events but stores no snapshot (receivedMs would make them
    // look fresh).
    bool const resumed = minuteStarted_ && manager_inbox::ResumedAfterGap(lastPassMs_, nowMs);
    lastPassMs_ = nowMs;
    if (resumed)
    {
        minuteStarted_ = false;
        status_.clear();
    }

    inboxBuf_.clear();
    manager_inbox::Drain(inboxBuf_, manager_inbox::kDrainMaxPerPass);
    for (manager_inbox::Event const& event : inboxBuf_)
    {
        ++kindTotals_[manager_inbox::KindIndex(event.kind)];
        // FIFO and one sender thread per bot: the last Status drained is the bot's latest.
        if (!resumed && event.kind == manager_inbox::Kind::Status)
            status_[event.botLow] = manager_inbox::SnapshotOf(event, nowMs);
    }

    // The minute window opens on the first pass (no line for a partial minute), like the GroupOps minute line. The baseline is
    // taken here, so the first line counts only its own window (no backlog from before a resume).
    if (!minuteStarted_)
    {
        minuteStarted_ = true;
        minuteStartMs_ = nowMs;
        seen_ = manager_inbox::Read();
        statusSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::Status)];
        ackSeen_ = kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::DirectiveAck)];
        return;
    }
    if (std::uint32_t(nowMs - minuteStartMs_) < 60000)
        return;
    minuteStartMs_ = nowMs;

    // A bot that logged out or left the roster posts no more: drop its snapshot (map keys only, no Player* read).
    for (auto it = status_.begin(); it != status_.end();)
    {
        if (bots.find(it->first) == bots.end())
            it = status_.erase(it);
        else
            ++it;
    }

    manager_inbox::Counters const totals = manager_inbox::Read();
    manager_inbox::MinuteLine line;
    line.posted = manager_inbox::TakeDelta(totals.posted, seen_.posted);
    line.dropped = manager_inbox::TakeDelta(totals.dropped, seen_.dropped);
    line.drained = manager_inbox::TakeDelta(totals.drained, seen_.drained);
    line.status = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::Status)], statusSeen_);
    line.ack = manager_inbox::TakeDelta(kindTotals_[manager_inbox::KindIndex(manager_inbox::Kind::DirectiveAck)], ackSeen_);
    line.snapshots = status_.size();
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
