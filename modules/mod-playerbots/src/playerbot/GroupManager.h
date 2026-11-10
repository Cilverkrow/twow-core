#pragma once

#include "playerbot/GroupManagerPolicy.h"
#include "playerbot/ManagerInboxPolicy.h"
#include "playerbot/GroupDirectivePolicy.h"   // T30-1: the registry and the state machine (pure)

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

class Player;

// A3 step 6 (ADR-0041, OB-00 06.11.2026; AiPlayerbot.BotGroups.Manager, default 0): the one owner of the groups of roster
// bots, on the world thread (RandomPlayerbotMgr::UpdateAIInternal, maps idle, next to S7 and #628). SCAFFOLD: the pass counts
// itself and drains the manager inbox (A3-GERUEST-L60 7.2.11) into the latest status, group edge and group wish per bot; T30-0
// counts shadow candidates from those tables once a minute (measurement only, no op, no directive). T30-1 (BotGroups.Manager.Types,
// default 0) forms, holds and ends ADHOC and QUEST groups of its cohort's bots (GroupDirectivePolicy.h) and tells their
// supervisors by directives. The meeting point and the dungeon episodes (G-1..G-3) come in their own pairs, each behind this
// switch and each with its own A/B (ADR-0041 rules 1 and 3). Every membership change goes through the world-thread op path of
// BOT-12 (group_ops::QueueManagerJoin / QueueManagerLeave); nothing in here may call a group handler or Group::* directly.
namespace ai::group_mgr
{
class WorldThreadManager
{
public:
    void Update(std::map<std::uint32_t, Player*> const& bots, std::uint32_t now);
    // T30-1 (M1, M13): the call site keeps the pass while the registry holds anything, so switching off ends every group.
    bool HasGroups() const { return engine_.HasGroups(); }
    // T30-1 read views for the map threads (GroupManagerView.h).
    Engine const& Directives() const { return engine_; }

    std::uint64_t Passes() const { return passes_; }

private:
    // 7.2.11: takes at most kDrainMaxPerPass events, keeps the last Status, GroupEdge and GroupWish per bot (T30-0), counts
    // every kind; once a minute drops the entries of bots no longer in the map, counts the shadow candidates and writes the
    // [ManagerInbox] line (none for a silent minute); after an off gap (kResumeGapMs) it starts a fresh window and fresh
    // tables and drains the whole ring (at most Capacity events), counted and discarded. Order of one pass: drain -> tables ->
    // minute line. Reads only the map keys.
    void DrainInbox(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs);
    // T30-0: one drained event into its table (Status, GroupEdge, GroupWish); the other kinds are only counted.
    void StoreEvent(manager_inbox::Event const& event, std::uint32_t nowMs);
    // T30-1 (design 2.2): after the drain - op outcomes, acks, gap rule, deadlines, end rules, forming, the op budget. Returns
    // at once while Types is 0 and the registry is empty (the default: no op, no directive, no line).
    void RunDirectives(std::map<std::uint32_t, Player*> const& bots, std::uint32_t nowMs, std::uint32_t nowSec);

    std::uint64_t passes_ = 0;
    std::vector<manager_inbox::Event> inboxBuf_;   // reused each pass
    std::unordered_map<std::uint32_t, manager_inbox::StatusSnapshot> status_;   // guid low -> latest Status
    std::unordered_map<std::uint32_t, manager_inbox::EdgeSnapshot> edges_;      // guid low -> latest GroupEdge (T30-0)
    std::unordered_map<std::uint32_t, manager_inbox::WishSnapshot> wishes_;     // guid low -> wish of the latest Status (T30-0)
    std::array<std::uint64_t, manager_inbox::kKindBuckets> kindTotals_{};   // drained events per kind (+ other) since start
    bool minuteStarted_ = false;
    std::uint32_t minuteStartMs_ = 0;
    std::uint32_t lastPassMs_ = 0;   // a longer gap than kResumeGapMs = the manager was off (reload 1 -> 0 -> 1)
    manager_inbox::Counters seen_;   // inbox totals at the last minute line
    std::uint64_t statusSeen_ = 0;   // Status total at the last minute line
    std::uint64_t ackSeen_ = 0;      // DirectiveAck total at the last minute line
    std::uint64_t edgeSeen_ = 0;     // GroupEdge total at the last minute line (T30-0)
    std::uint64_t wishSeen_ = 0;     // GroupWish total at the last minute line (T30-0)
    bool resumedPass_ = false;       // T30-1: this pass cleared the tables (M16: no deadline, no forget)
    std::vector<Ack> acks_;          // T30-1: DirectiveAck events of this pass
    std::vector<group_ops::ManagerOutcome> outcomes_;   // T30-1: manager op outcomes of this pass
    Engine engine_;
};

WorldThreadManager& WorldManager();
}
