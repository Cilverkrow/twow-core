#pragma once

#include "playerbot/GroupManagerPolicy.h"
#include "playerbot/ManagerInboxPolicy.h"

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class Player;

// A3 step 6 (ADR-0041, OB-00 06.11.2026; AiPlayerbot.BotGroups.Manager, default 0): the one owner of the groups of roster
// bots, on the world thread (RandomPlayerbotMgr::UpdateAIInternal, maps idle, next to S7 and #628). SCAFFOLD: the pass counts
// itself and drains the manager inbox (A3-GERUEST-L60 7.2.11) into the latest status per bot; nothing reads that table yet.
// Forming, changing and leaving groups, the meeting point and the dungeon episodes (G-1..G-3) come in their own
// pairs, each behind this switch and each with its own A/B (ADR-0041 rules 1 and 3). Every membership change will go through
// the world-thread op path of BOT-12 (GroupOps.h); nothing in here may call a group handler or Group::* directly.
namespace ai::group_mgr
{
class WorldThreadManager
{
public:
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);

    std::uint64_t Passes() const { return passes_; }

private:
    // 7.2.11: takes at most kDrainMaxPerPass events, keeps the last Status per bot, counts the other kinds; once a minute
    // drops snapshots of bots no longer in the map and writes the [ManagerInbox] line (none for a silent minute); after an off
    // gap (kResumeGapMs) it starts a fresh window and table. Reads only the map keys.
    void DrainInbox(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs);

    std::uint64_t passes_ = 0;
    std::vector<manager_inbox::Event> inboxBuf_;   // reused each pass
    std::unordered_map<std::uint32_t, manager_inbox::StatusSnapshot> status_;   // guid low -> latest Status
    std::array<std::uint64_t, manager_inbox::kKindBuckets> kindTotals_{};   // drained events per kind (+ other) since start
    bool minuteStarted_ = false;
    std::uint32_t minuteStartMs_ = 0;
    std::uint32_t lastPassMs_ = 0;   // a longer gap than kResumeGapMs = the manager was off (reload 1 -> 0 -> 1)
    manager_inbox::Counters seen_;   // inbox totals at the last minute line
    std::uint64_t statusSeen_ = 0;   // Status total at the last minute line
    std::uint64_t ackSeen_ = 0;      // DirectiveAck total at the last minute line
};

WorldThreadManager& WorldManager();
}
