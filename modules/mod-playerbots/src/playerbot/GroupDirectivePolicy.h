#pragma once

#include "GroupManagerPolicy.h"
#include "ManagerInboxPolicy.h"
#include "AdhocGroupPolicy.h"
#include "QuestGroupPolicy.h"
#include "SupervisorPolicy.h"
#include "GroupOpPolicy.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

// A3 step 6 T30-1 "Direktiven ADHOC/QUEST" (design ws-10/cli-grp/a3-schritt6/DESIGN-T30-MANAGER-DIREKTIVEN-20261109.md v1.1;
// owner decisions F1-F16 = (a), 09.11.2026; AiPlayerbot.BotGroups.Manager.Types, default 0). The pure part of the manager's
// lever: the registry of managed groups, the state machine per member and group, the directive bits for the supervisor's
// mailbox, the ack handling and the [GroupMgr] lines. No game type, no config read: GroupManager.cpp feeds the inbox tables,
// the op outcomes and the acks in, and does the I/O (ops, supervisor::Post, log) through the Io interface below. With Types 0
// nothing here is ever reached (the engine forms nothing and the call site only runs it while the registry holds a bot).
namespace ai::group_mgr
{
// ---- constants (design 2.3) -----------------------------------------------------------------------------------------------
constexpr std::uint32_t kJoinConfirmMs = 20000;    // from the join op's Execute to the confirming edge (no clock while pending, M7)
constexpr std::uint32_t kAckTimeoutMs = 10000;     // Enter/Exit without an ack (ST:548): fail closed
constexpr std::uint32_t kLeaveConfirmMs = 40000;   // a queued leave without an outcome
constexpr std::uint32_t kExitBlockSec = 600;       // the second failed Exit: no candidate for this long (exit_failed)
constexpr std::uint32_t kOpsPerPass = 1;           // new manager ops per pass, leaves first
constexpr std::uint32_t kMaxManagerOpsInFlight = 8;
constexpr std::uint32_t kLinesPerMinute = 300;     // event lines; the rest are counted as lines_dropped
constexpr std::uint32_t kExitTries = 2;            // F8 (a): Enter is never repeated, Exit once (idempotent, 9b R1)
constexpr std::uint32_t kLeaveTries = 2;
constexpr float kGatherYards = 30.0f;              // S7 "gathered" (QuestGroupWorld.cpp)
constexpr std::uint32_t kRealPlayerGraceMs = 10000;   // edges lag one bot pass: fewer reporters must last this long
constexpr std::uint32_t kFormingMaxSec = 120;          // a group not active by then ends (join_stale)
constexpr std::uint32_t kSkipLineMs = 60000;           // one foreign skip line per bot per minute (the rest is counted only)
constexpr std::uint32_t kGapShiftMaxMs = 120000;       // the stale-anchor credit stops at the resume limit (M15/M16)
constexpr std::uint32_t kSkipUnknownLongSec = 1800;    // T30-1b: minute2 skip_unknown_30m counts bots skip_unknown this long
constexpr std::uint32_t kOtherMapLongMs = 600000;      // T30-1d: minute2 follow_other_map_600 counts episodes this long

// a is later than b on the 32-bit ms clock (unsigned difference, wraps).
inline bool After(std::uint32_t a, std::uint32_t b)
{
    return std::uint32_t(a - b) - 1u < 0x7FFFFFFFu;
}

// ---- why a member or a group ends (design 2.6) -----------------------------------------------------------------------------
// Values < 128: the Exit directive carries it in b9-15.
enum class EndReason : std::uint8_t
{
    None = 0,
    // ADHOC: adhoc_group::Leave
    QuestTurnedIn,
    ObjectiveDone,
    GroupDone,
    Instance,
    LeaderOtherMap,
    OutOfRange,
    LevelWindow,
    Idle,
    // QUEST: quest_groups::Dissolve
    TurnedIn,
    RendezvousTimeout,
    NoProgress,
    MaxTime,
    TurnInWait,
    LeaderLost,
    LeaderStuck,
    RealPlayer,
    TooSmall,
    Abandoned,
    // the manager's own (both types)
    JoinFailed,
    JoinStale,
    OpRefused,
    AckTimeout,
    AckRefused,
    DirectiveDropped,
    NoSupervisor,
    SwitchOff,
    ForeignGroup,
    Orphan,
    ExtLeft,
    // T30-1b: the bot left the GROUP mode itself (ack state 4 "left", T02-GROUP handler). Appended: the values above stay.
    BotLeftDone,   // left reason 26 goal_done: a natural end
    BotLeftLost,   // left reason 27 goal_lost: the manager's own (retry cooldown only)
    BotLeftOff,    // left reason 22 arbiter_off (or any other): the manager's own (retry cooldown only)
    Count,
};
constexpr std::size_t kEndReasons = std::size_t(EndReason::Count);
static_assert(kEndReasons <= 128, "the Exit directive carries the end reason in 7 bits (b9-15)");

inline char const* EndReasonName(EndReason r)
{
    switch (r)
    {
        case EndReason::None: return "none";
        case EndReason::QuestTurnedIn: return "quest_turned_in";
        case EndReason::ObjectiveDone: return "objective_done";
        case EndReason::GroupDone: return "group_done";
        case EndReason::Instance: return "instance";
        case EndReason::LeaderOtherMap: return "leader_other_map";
        case EndReason::OutOfRange: return "out_of_range";
        case EndReason::LevelWindow: return "level_window";
        case EndReason::Idle: return "idle";
        case EndReason::TurnedIn: return "turned_in";
        case EndReason::RendezvousTimeout: return "rendezvous_timeout";
        case EndReason::NoProgress: return "no_progress";
        case EndReason::MaxTime: return "max_time";
        case EndReason::TurnInWait: return "turnin_wait";
        case EndReason::LeaderLost: return "leader_lost";
        case EndReason::LeaderStuck: return "leader_stuck";
        case EndReason::RealPlayer: return "real_player";
        case EndReason::TooSmall: return "too_small";
        case EndReason::Abandoned: return "abandoned";
        case EndReason::JoinFailed: return "join_failed";
        case EndReason::JoinStale: return "join_stale";
        case EndReason::OpRefused: return "op_refused";
        case EndReason::AckTimeout: return "ack_timeout";
        case EndReason::AckRefused: return "ack_refused";
        case EndReason::DirectiveDropped: return "directive_dropped";
        case EndReason::NoSupervisor: return "no_supervisor";
        case EndReason::SwitchOff: return "switch_off";
        case EndReason::ForeignGroup: return "foreign_group";
        case EndReason::Orphan: return "orphan";
        case EndReason::ExtLeft: return "ext_left";
        case EndReason::BotLeftDone: return "bot_left_done";
        case EndReason::BotLeftLost: return "bot_left_lost";
        case EndReason::BotLeftOff: return "bot_left_off";
        case EndReason::Count: break;
    }
    return "unknown";
}

inline EndReason FromLeave(adhoc_group::Leave l)
{
    switch (l)
    {
        case adhoc_group::Leave::None: return EndReason::None;
        case adhoc_group::Leave::QuestTurnedIn: return EndReason::QuestTurnedIn;
        case adhoc_group::Leave::ObjectiveDone: return EndReason::ObjectiveDone;
        case adhoc_group::Leave::Instance: return EndReason::Instance;
        case adhoc_group::Leave::OutOfRange: return EndReason::OutOfRange;
        case adhoc_group::Leave::LevelWindow: return EndReason::LevelWindow;
        case adhoc_group::Leave::Idle: return EndReason::Idle;
        case adhoc_group::Leave::GroupDone: return EndReason::GroupDone;
        case adhoc_group::Leave::LeaderOtherMap: return EndReason::LeaderOtherMap;
    }
    return EndReason::None;
}

inline EndReason FromDissolve(quest_groups::Dissolve d)
{
    switch (d)
    {
        case quest_groups::Dissolve::None: return EndReason::None;
        case quest_groups::Dissolve::TurnedIn: return EndReason::TurnedIn;
        case quest_groups::Dissolve::RendezvousTimeout: return EndReason::RendezvousTimeout;
        case quest_groups::Dissolve::NoProgress: return EndReason::NoProgress;
        case quest_groups::Dissolve::MaxTime: return EndReason::MaxTime;
        case quest_groups::Dissolve::TurnInWait: return EndReason::TurnInWait;
        case quest_groups::Dissolve::LeaderLost: return EndReason::LeaderLost;
        case quest_groups::Dissolve::LeaderStuck: return EndReason::LeaderStuck;
        case quest_groups::Dissolve::RealPlayer: return EndReason::RealPlayer;
        case quest_groups::Dissolve::TooSmall: return EndReason::TooSmall;
        case quest_groups::Dissolve::Abandoned: return EndReason::Abandoned;
    }
    return EndReason::None;
}

// F13 (a) (= S7 today): a QUEST attempt is only an S7 dissolve reason other than turned_in. The manager's own reasons do not
// count; they only set the retry cooldown (ManagerQuestCooldown).
inline bool IsS7Reason(EndReason r)
{
    return r >= EndReason::TurnedIn && r <= EndReason::Abandoned;
}

inline bool CountsAsQuestAttempt(EndReason r)
{
    return IsS7Reason(r) && r != EndReason::TurnedIn;
}

// T30-1b: bot_left_done is a natural end (the bot finished its goal, like a member that turned the quest in); bot_left_lost and
// bot_left_off end the membership for the manager's side of things (the goal fell away or the handler was disarmed): no S7
// attempt failure, the retry cooldown only - the same as the other manager reasons, so they count here.
inline bool IsManagerReason(EndReason r)
{
    return r >= EndReason::JoinFailed && r < EndReason::Count && r != EndReason::BotLeftDone;
}

// A natural end (G5, osc): the goal reached. Every other end within kOscSec of the Enter's ack 2 is an oscillation.
inline bool IsNaturalEnd(EndReason r)
{
    return r == EndReason::QuestTurnedIn || r == EndReason::ObjectiveDone || r == EndReason::GroupDone || r == EndReason::TurnedIn ||
        r == EndReason::BotLeftDone;
}

// G5 (T30-1b): a membership that ends without a natural reason less than kOscSec after its Enter was applied (ack 2).
// One count per cause: too_small (the follow-on end of a partner whose other member left; that one counted already or ended
// naturally) and switch_off (an operator action, not bot behaviour) are no osc (CLI-GRP 10.11.).
constexpr std::uint32_t kOscSec = 900;

inline bool CountsAsOsc(bool acked, std::uint32_t ackSec, std::uint32_t nowSec, EndReason why)
{
    return acked && nowSec - ackSec < kOscSec && !IsNaturalEnd(why) && why != EndReason::TooSmall && why != EndReason::SwitchOff;
}

// ---- the directive (design 3.3, agreed with CLI-Projekt 9b R3 / T33-R8) ----------------------------------------------------
// supervisor::Event: kind GroupDirective, a = seq (manager-wide, never 0), b = the bits below, c = the quest id, mapId/x/y = the
// forming map and the anchor (the leader's snapshot; no gather point, T33), z = 0, source = Requester::GroupMgr.
// b: b0-3 verb, b4-5 group type, b6-8 objective + 1 (0 = none), b9-15 end reason (Exit only; Enter/Update carry b13-15 below),
// b16-31 the group serial (low 16 bits).
// Enter/Update b9-15 (G4, agreed with CLI-Projekt 10.11.): b9-12 reserved for T34 (the leg), b13-14 gatherKind (the T33 gather
// kind), b15 gatherFlag (the T33 gather flag), read by T02-GROUP (Q8 (a)); the manager sends b9-15 as 0 until T33-0. Exit keeps
// b9-15 as the end reason.
enum class Verb : std::uint8_t
{
    None = 0,
    Enter = 1,
    Update = 2,   // reserved (T33)
    Exit = 3,
};

inline char const* VerbName(Verb v)
{
    switch (v)
    {
        case Verb::None: return "none";
        case Verb::Enter: return "enter";
        case Verb::Update: return "update";
        case Verb::Exit: return "exit";
    }
    return "unknown";
}

struct DirectiveBits
{
    Verb verb = Verb::None;
    GroupType type = GroupType::Adhoc;
    std::int8_t objective = -1;     // 0..6, -1 = none
    EndReason reason = EndReason::None;
    std::uint16_t serial = 0;
    std::uint8_t gatherKind = 0;   // b13-14, Enter/Update (T33)
    bool gatherFlag = false;   // b15, Enter/Update (T33)
};

inline std::uint32_t PackDirective(DirectiveBits const& d)
{
    std::uint32_t const objective = d.objective >= 0 && d.objective <= 6 ? std::uint32_t(d.objective + 1) : 0u;
    std::uint32_t const reason = d.verb == Verb::Exit ? (std::uint32_t(d.reason) & 0x7Fu) : 0u;
    // Q8 (a): b13-15 outside Exit only; b9-12 stay 0 (T34).
    std::uint32_t const gather = d.verb != Verb::Exit ? ((std::uint32_t(d.gatherKind) & 0x3u) << 13) | (d.gatherFlag ? (1u << 15) : 0u) : 0u;
    return (std::uint32_t(d.verb) & 0xFu) | ((std::uint32_t(d.type) & 0x3u) << 4) | (objective << 6) | (reason << 9) | gather |
        (std::uint32_t(d.serial) << 16);
}

inline DirectiveBits UnpackDirective(std::uint32_t b)
{
    DirectiveBits d;
    d.verb = Verb(b & 0xFu);
    d.type = GroupType((b >> 4) & 0x3u);
    std::uint32_t const objective = (b >> 6) & 0x7u;
    d.objective = objective ? std::int8_t(objective - 1) : std::int8_t(-1);
    d.reason = d.verb == Verb::Exit ? EndReason((b >> 9) & 0x7Fu) : EndReason::None;
    d.serial = std::uint16_t(b >> 16);
    if (d.verb != Verb::Exit)
    {
        d.gatherKind = std::uint8_t((b >> 13) & 0x3u);
        d.gatherFlag = ((b >> 15) & 0x1u) != 0;
    }
    return d;
}

inline supervisor::Event MakeDirective(std::uint32_t seq, DirectiveBits const& d, std::uint32_t questId, std::uint32_t mapId,
    float x, float y, std::uint32_t atMs)
{
    supervisor::Event e;
    e.kind = supervisor::EventKind::GroupDirective;
    e.a = seq;
    e.b = PackDirective(d);
    e.c = questId;
    e.mapId = mapId;
    e.x = x;
    e.y = y;
    e.z = 0.0f;
    e.atMs = atMs;
    e.source = std::uint8_t(supervisor::Requester::GroupMgr);
    return e;
}

// ---- the ack reason (F12 (a), 9b R5, T33-R1): state 3 carries uint8(supervisor::Deny) 1-7, own reasons from 16 -------------
constexpr std::uint8_t kAckReasonOwnBase = 16;
constexpr std::uint8_t kAckReasonStartZoneHold = 16;   // R8: an Enter (or Update) outside the start zone of a held bot
constexpr std::uint8_t kAckReasonReserved17 = 17;
constexpr std::uint8_t kAckReasonReserved18 = 18;
constexpr std::uint8_t kAckReasonGatherOtherMap = 19;  // T33 (no logic here)
constexpr std::uint8_t kAckReasonNoOpenGroup = 20;     // T33: an Update without an open group (no logic here)
// T30-1b (G6): 21-27 are owned by CLI-Projekt T02-GROUP, reserved 10.11.2026. 26, 27 and 22 also come as the reason of a left
// ack (state 4).
constexpr std::uint8_t kAckReasonLegacyGroupRole = 21;
constexpr std::uint8_t kAckReasonArbiterOff = 22;
constexpr std::uint8_t kAckReasonLaneBusy = 23;
constexpr std::uint8_t kAckReasonGoalNotInLog = 24;
constexpr std::uint8_t kAckReasonFocusConflict = 25;
constexpr std::uint8_t kAckReasonGoalDone = 26;
constexpr std::uint8_t kAckReasonGoalLost = 27;

inline char const* AckReasonName(std::uint8_t reason)
{
    if (reason == 0)
        return "none";
    if (reason <= std::uint8_t(supervisor::Deny::RescueFirst))
        return supervisor::DenyName(supervisor::Deny(reason));
    switch (reason)
    {
        case kAckReasonStartZoneHold: return "start_zone_hold";
        case kAckReasonReserved17: return "reserved_17";
        case kAckReasonReserved18: return "reserved_18";
        case kAckReasonGatherOtherMap: return "gather_other_map";
        case kAckReasonNoOpenGroup: return "no_open_group";
        case kAckReasonLegacyGroupRole: return "legacy_group_role";
        case kAckReasonArbiterOff: return "arbiter_off";
        case kAckReasonLaneBusy: return "lane_busy";
        case kAckReasonGoalNotInLog: return "goal_not_in_log";
        case kAckReasonFocusConflict: return "focus_conflict";
        case kAckReasonGoalDone: return "goal_done";
        case kAckReasonGoalLost: return "goal_lost";
        default: break;
    }
    return "unknown";
}

inline char const* AckStateName(std::uint8_t state)
{
    switch (state)
    {
        case manager_inbox::kAckReceived: return "received";
        case manager_inbox::kAckApplied: return "applied";
        case manager_inbox::kAckRefused: return "refused";
        case manager_inbox::kAckLeft: return "left";
        default: break;
    }
    return "unknown";
}

// T30-1b: the left ack's reason as the member's end reason: 26 goal_done, 27 goal_lost, anything else (22 arbiter_off
// expected) bot_left_off.
inline EndReason FromLeftReason(std::uint8_t reason)
{
    return reason == kAckReasonGoalDone ? EndReason::BotLeftDone :
        reason == kAckReasonGoalLost ? EndReason::BotLeftLost : EndReason::BotLeftOff;
}

// 2.3 Exit (M4): done on Ack 2, or Ack 3 with SameMode / PassiveHold (the bot is not in GROUP).
inline bool ExitDone(std::uint8_t state, std::uint8_t reason)
{
    return state == manager_inbox::kAckApplied ||
        (state == manager_inbox::kAckRefused &&
            (reason == std::uint8_t(supervisor::Deny::SameMode) || reason == std::uint8_t(supervisor::Deny::PassiveHold)));
}

// ---- the rules the engine reads (GroupManager.cpp fills them from the config each pass) -------------------------------------
struct Rules
{
    std::uint32_t mode = 0;               // BotGroups.Manager
    std::uint32_t types = 0;              // BotGroups.Manager.Types, already effective (F9)
    std::uint32_t statusSeconds = 0;      // BotGroups.Manager.StatusSeconds
    std::uint32_t intervalMs = 1000;      // randomBotUpdateInterval (the gap rule)
    // ADHOC (AdhocGroupWorld values)
    std::uint32_t maxBots = 3;
    std::uint32_t levelWindow = 3;
    float adhocRadius = 50.0f;
    std::uint32_t pairCooldownSeconds = 600;
    bool helpUntilDone = false;           // F5 (a): AdHoc.HelpUntilDone && AdHoc.WorldThread
    std::uint32_t leaderOtherMapMode = 0; // F11 (a): BotGroups.LeaderOtherMap, per bot by cohort
    std::uint32_t leaderOtherMapSeconds = 120;
    // QUEST (S7 values)
    std::uint32_t questMaxSize = 5;
    std::uint32_t questMaxSizeDrop = 3;
    std::uint32_t questLevelWindow = 3;
    float recruitRadius = 800.0f;
    std::uint32_t maxAttempts = 3;
    std::uint32_t retryCooldownMinutes = 30;
    quest_groups::Timers timers;
};

// ---- the I/O the engine needs (GroupManager.cpp: ops, the supervisor's postbox, the log; tests: a fake) ---------------------
class Io
{
public:
    virtual ~Io() = default;
    virtual bool SupervisorApplies(std::uint32_t low) = 0;
    virtual bool S7Owns(std::uint32_t low) = 0;
    // false = refused (a bot not found, or an op of the bot or the inviter still pending): op_refused.
    virtual bool QueueJoin(std::uint32_t invitee, std::uint32_t inviter, std::uint32_t groupId, GroupType type,
        std::uint32_t questId, std::uint8_t objective) = 0;
    virtual bool QueueLeave(std::uint32_t bot, std::uint32_t groupId, std::uint32_t leaderLow) = 0;
    // false = not delivered (supervisor off for the bot, R10, or its ring full): directive_dropped.
    virtual bool PostDirective(std::uint32_t low, supervisor::Event const& event) = 0;
    virtual void Line(char const* line) = 0;
};

using StatusTable = std::unordered_map<std::uint32_t, manager_inbox::StatusSnapshot>;
using EdgeTable = std::unordered_map<std::uint32_t, manager_inbox::EdgeSnapshot>;
using WishTable = std::unordered_map<std::uint32_t, manager_inbox::WishSnapshot>;

struct Ack
{
    std::uint32_t low = 0;
    std::uint32_t seq = 0;
    std::uint8_t state = 0;
    std::uint8_t reason = 0;
    std::uint16_t serial = 0;   // T30-1b: b16-31 of the state word, the group serial (low 16) the bot holds; 0 for states 1-3
};

// The state word (ManagerInboxPolicy.h): b0-7 the state, b16-31 the serial. States 1-3 carry 0 above b7, so they read as before.
inline Ack AckOf(manager_inbox::Event const& e)
{
    Ack a;
    a.low = e.botLow;
    a.seq = e.seq;
    a.state = std::uint8_t(e.mapOrDungeon & 0xFFu);
    a.serial = std::uint16_t(e.mapOrDungeon >> 16);
    a.reason = e.reason;
    return a;
}

struct PassInput
{
    std::uint32_t nowMs = 0;
    std::uint32_t nowSec = 0;
    bool resumed = false;   // ResumedAfterGap: the tables were cleared in this pass (M16)
    StatusTable const* status = nullptr;
    EdgeTable const* edges = nullptr;
    WishTable const* wishes = nullptr;
    std::vector<Ack> const* acks = nullptr;
    std::vector<group_ops::ManagerOutcome> const* outcomes = nullptr;
    std::uint64_t keyOverflow = 0;   // manager_inbox::Read().keyOverflow (a total; the minute line shows its increment)
};

// ---- the registry (design 3.2) --------------------------------------------------------------------------------------------
enum class MemberState : std::uint8_t
{
    Joining = 0,
    Joined,
    Directed,
    Acked,
    Leaving,
    Left,
};

inline char const* MemberStateName(MemberState s)
{
    switch (s)
    {
        case MemberState::Joining: return "joining";
        case MemberState::Joined: return "joined";
        case MemberState::Directed: return "directed";
        case MemberState::Acked: return "acked";
        case MemberState::Leaving: return "leaving";
        case MemberState::Left: return "left";
    }
    return "unknown";
}

struct Member
{
    std::uint32_t low = 0;
    MemberState state = MemberState::Joining;
    bool inviter = false;            // the inviter of a new group (no op of its own; confirmed by its own edge)
    bool opWanted = false;           // a join op waits for the budget
    bool opQueued = false;           // pushed, no outcome yet (no join clock, M7)
    std::uint32_t opExecMs = 0;      // the join executed at (0 = not yet); the join clock runs from here
    std::uint32_t enterSeq = 0;
    std::uint32_t sentMs = 0;
    bool enterDelivered = false;     // Post took the Enter: an Exit follows at the end
    bool leaveWanted = false;
    bool leaveQueued = false;
    std::uint32_t leaveMs = 0;
    std::uint8_t leaveTries = 0;
    EndReason reason = EndReason::None;
    bool silent = false;
    bool bgWait = false;
    bool turnedIn = false;           // its key showed the group's quest rewarded (S7 forgets such a member's attempts)
    std::uint32_t addedMs = 0;       // silent without any snapshot only kStaleMs after this (or after a resume, M16)
    std::uint32_t joinedSec = 0;
    std::uint8_t progress = 0;       // the key's progress counter mod 8 last seen
    bool progressKnown = false;
    std::uint32_t lastProgressSec = 0;
    std::uint32_t outOfRangeSince = 0;
    std::uint32_t levelWindowSince = 0;
    std::uint32_t leaderOtherMapSince = 0;
    bool ackKnown = false;           // T30-1b osc: its Enter was applied (ack 2) at ackSec
    std::uint32_t ackSec = 0;
    std::uint32_t otherMapSinceMs = 0;   // T30-1d: an open other-map episode (QUEST follower) since (PostStamp), 0 = none
    bool otherMapConfirmed = false;      // T30-1d: two maps again on snapshots of both received at or after the open
};

enum class GroupState : std::uint8_t
{
    Forming = 0,
    Active,
    Ending,
};

struct ManagedGroup
{
    std::uint32_t serial = 0;
    GroupType type = GroupType::Adhoc;
    std::uint32_t coreGroupId = 0;   // 0 until an edge confirms it
    std::uint32_t questId = 0;
    std::int8_t objective = -1;
    std::uint32_t leaderLow = 0;
    std::uint32_t mapId = 0;
    bool alliance = false;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    std::vector<Member> m;
    bool joinInFlight = false;       // one join per group in flight (M8)
    bool dropOnly = false;
    GroupState state = GroupState::Forming;
    std::uint32_t createdSec = 0;
    std::uint32_t formedSec = 0;
    quest_groups::Phase phase = quest_groups::Phase::Rendezvous;
    std::uint32_t phaseSinceSec = 0;
    std::uint32_t lastProgressSec = 0;
    std::uint32_t realPlayerSinceMs = 0;
    EndReason endReason = EndReason::None;
    std::uint32_t sizeMax = 0;
};

// T33-0 (the meeting shadow, measurement only): a group Sweep erased in this pass and why it ended. The shadow reads the list
// after the pass, so a meeting T30 ended and swept in the same pass still gets its verdict. Cleared at the start of every pass.
struct SweptGroup
{
    std::uint32_t serial = 0;
    EndReason why = EndReason::None;
};

// One entry per bot the manager holds: a member, an Exit still open, an exit block, or a group it could not confirm leaving.
// IsManaged = an entry exists; the legacy writers stay closed for it (Blocks, M1).
struct BotEntry
{
    std::uint32_t serial = 0;          // its group (0 = none)
    DirectiveBits exitBits;
    std::uint32_t exitQuest = 0;
    std::uint32_t exitMap = 0;
    float exitX = 0.0f;
    float exitY = 0.0f;
    std::uint32_t exitSeq = 0;
    std::uint32_t exitSentMs = 0;
    std::uint8_t exitTries = 0;
    bool exitOpen = false;             // an Exit waits for its ack
    bool exitDue = false;              // the second Exit goes out in the next pass
    std::uint32_t blockedUntilSec = 0;
    std::uint32_t staleGroupId = 0;    // leave_unconfirmed / silent at the end: kept until its edge shows another group
    std::uint64_t exitDuePass = 0;     // the pass that found the Exit failed: the second one goes out in a later pass
    // A leave that took the bot out of this core group: its edges naming the group predate the leave (the bot posts on its next
    // pass), so they start no orphan / foreign leave. Kept until an edge shows another group, at most kLeaveConfirmMs.
    std::uint32_t outGroupId = 0;
    std::uint32_t outMs = 0;
};

// The read view for the bots' map threads (gates, S7 mirror hooks, the status key): written only by the engine, read under a
// mutex; an atomic in front so a bot without any managed bot in the realm takes no lock (Types 0: never).
struct ViewEntry
{
    std::uint32_t coreGroupId = 0;
    std::uint32_t questId = 0;
    std::int8_t objective = -1;
    GroupType type = GroupType::Adhoc;
    std::uint8_t questRole = 0;   // QUEST member: 1 follower, 2 leader (the S7 role mirror, 3.6); 0 otherwise
    // The low 16 bits of the group serial (the directive's b16-31) while the bot is a live member (not ending, not left);
    // 0 otherwise. NextGroupSerial never hands out a serial whose low 16 bits are 0.
    std::uint16_t serial = 0;
};

class ManagedView
{
public:
    bool Any() const { return any_.load(std::memory_order_acquire); }

    bool Find(std::uint32_t low, ViewEntry& out) const
    {
        if (!Any())
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = bots_.find(low);
        if (it == bots_.end())
            return false;
        out = it->second;
        return true;
    }

    // The bot is a live member of the group whose serial has these low 16 bits (the supervisor's orphan check). Same fast
    // path as Find; the lock covers the lookup only.
    bool IsMember(std::uint32_t low, std::uint16_t serial) const
    {
        if (!Any() || !serial)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = bots_.find(low);
        return it != bots_.end() && it->second.serial == serial;
    }

    bool KeyOf(std::uint32_t coreGroupId, ViewEntry& out) const
    {
        if (!Any() || !coreGroupId)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = cores_.find(coreGroupId);
        if (it == cores_.end())
            return false;
        out = it->second;
        return true;
    }

    // World thread: the new view replaces the old one (swap under the lock; the old maps are reused next time).
    void Publish(std::unordered_map<std::uint32_t, ViewEntry>& bots, std::unordered_map<std::uint32_t, ViewEntry>& cores)
    {
        bool const any = !bots.empty();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bots_.swap(bots);
            cores_.swap(cores);
        }
        any_.store(any, std::memory_order_release);
        bots.clear();
        cores.clear();
    }

private:
    mutable std::mutex mutex_;
    std::atomic<bool> any_{ false };
    std::unordered_map<std::uint32_t, ViewEntry> bots_;
    std::unordered_map<std::uint32_t, ViewEntry> cores_;
};

// ---- lookups and candidate facts (2.4) ------------------------------------------------------------------------------------
inline manager_inbox::StatusSnapshot const* FindStatus(StatusTable const* t, std::uint32_t low)
{
    if (!t)
        return nullptr;
    auto const it = t->find(low);
    return it == t->end() ? nullptr : &it->second;
}

inline manager_inbox::EdgeSnapshot const* FindEdge(EdgeTable const* t, std::uint32_t low)
{
    if (!t)
        return nullptr;
    auto const it = t->find(low);
    return it == t->end() ? nullptr : &it->second;
}

inline manager_inbox::WishSnapshot const* FindWish(WishTable const* t, std::uint32_t low)
{
    if (!t)
        return nullptr;
    auto const it = t->find(low);
    return it == t->end() ? nullptr : &it->second;
}

// OB-10 note on #984: a Status lost to a full inbox ring leaves the previous wish in the table until the next Status
// ("no wish" posts nothing). The manager therefore acts on a wish only while it is from the latest status period:
// 1.5 periods, not the 3 periods of kStaleMs. A dropped Status then costs the bot one period as a QUEST candidate (fail-closed).
inline std::uint32_t WishFreshMs(std::uint32_t statusSeconds)
{
    std::uint64_t const ms = std::uint64_t(statusSeconds) * 1500u;
    return ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : std::uint32_t(ms);
}

inline float Dist2d(float ax, float ay, float bx, float by)
{
    float const dx = ax - bx;
    float const dy = ay - by;
    return dx * dx + dy * dy;
}

// One bot as a candidate (2.4, from snapshots and the manager's own state only). The cand_* minute counts come from here.
struct CandidateFacts
{
    bool stale = false;
    bool busy = false;
    bool noSupervisor = false;
    bool adhoc = false;       // an ADHOC candidate (key valid, incomplete, own objective open)
    bool quest = false;       // a QUEST candidate (fresh wish, no start-zone hold, not blocked)
    bool zoneHold = false;    // would be a QUEST candidate but held in its start zone (F14 (a): never a QUEST candidate)
};

inline CandidateFacts Candidate(manager_inbox::StatusSnapshot const& s, manager_inbox::EdgeSnapshot const* edge,
    manager_inbox::WishSnapshot const* wish, bool supervisorApplies, bool questBlocked, std::uint32_t nowMs, std::uint32_t staleMs)
{
    CandidateFacts c;
    if (!manager_inbox::Fresh(s.receivedMs, nowMs, staleMs))
    {
        c.stale = true;
        return c;
    }
    manager_inbox::StatusFlags const f = manager_inbox::UnpackFlags(s.flags);
    if (!f.alive || !f.candidate || f.inBgQueue || f.dungeonMap)
        return c;
    if (!edge || !manager_inbox::Fresh(edge->receivedMs, nowMs, staleMs) || edge->edge.groupId != 0 || edge->edge.invite)
        return c;
    if (f.busy)
    {
        c.busy = true;
        return c;
    }
    if (!supervisorApplies)
    {
        c.noSupervisor = true;   // F2 (a): no candidate without the supervisor
        return c;
    }
    manager_inbox::KeyFacts const k = manager_inbox::UnpackKey(s.key);
    c.adhoc = k.questId != 0 && k.objective >= 0 && k.questStatus == manager_inbox::kQuestIncomplete && !k.objDone;
    if (wish && manager_inbox::Fresh(wish->receivedMs, nowMs, staleMs) && wish->wish.questId)
    {
        if (f.startZoneHold)
            c.zoneHold = true;
        else if (!questBlocked)
            c.quest = true;
    }
    return c;
}

// ---- the minute counters (5.3) --------------------------------------------------------------------------------------------
enum class Cnt : std::uint8_t
{
    Passes = 0,
    OpJoinQ,
    OpJoinRefused,
    OpLeaveQ,
    OpLeaveRefused,
    ExecJoin,
    FailJoin,
    StaleJoin,
    ExecLeave,
    FailLeave,
    StaleLeave,
    Joined,
    JoinFailed,
    JoinStale,
    Formed,
    DirSent,
    DirDropped,
    AckRcv,
    AckApp,
    AckRef,
    AckTimeout,
    AckStale,
    ExitPending,
    ExitFailed,
    Left,
    LeaveUnconfirmed,
    Orphan,
    ExtLeft,
    Dissolved,
    Foreign,
    SkipUnknown,
    SkipS7,
    GapSuspend,
    ExitSkipped,
    LeaveSilent,
    LeaveBgWait,
    LinesDropped,
    // T30-1b: appended. The minute line's loop stops at LinesDropped (kCountsMinuteLoop), so its fields stay as they were; these
    // two go to the end of the minute2 line (G5: osc at the end of minute2), after its leave reasons.
    Osc,
    SelfLeft,
    // T30-1c (OB-10 note on #992): appended after skip_unknown_30m, at the very end of minute2 (kCountsMinute2Loop).
    LeftStale,
    // T30-1d (OB-10 review of R9, B4): a QUEST follower on another map than its leader, appended after left_stale.
    FollowOtherMap,
    FollowOtherMap600,
    FollowOtherMapMs,
    FollowQuestMs,
    FollowOtherMapShort,
    Count,
};
constexpr std::size_t kCounts = std::size_t(Cnt::Count);
constexpr std::size_t kCountsMinuteLoop = std::size_t(Cnt::LinesDropped) + 1;   // the counters of the minute line's loop
constexpr std::size_t kCountsMinute2Loop = std::size_t(Cnt::SelfLeft) + 1;      // minute2's counters before skip_unknown_30m

inline char const* CntName(Cnt c)
{
    switch (c)
    {
        case Cnt::Passes: return "passes";
        case Cnt::OpJoinQ: return "op_join_q";
        case Cnt::OpJoinRefused: return "op_join_refused";
        case Cnt::OpLeaveQ: return "op_leave_q";
        case Cnt::OpLeaveRefused: return "op_leave_refused";
        case Cnt::ExecJoin: return "exec_join";
        case Cnt::FailJoin: return "fail_join";
        case Cnt::StaleJoin: return "stale_join";
        case Cnt::ExecLeave: return "exec_leave";
        case Cnt::FailLeave: return "fail_leave";
        case Cnt::StaleLeave: return "stale_leave";
        case Cnt::Joined: return "joined";
        case Cnt::JoinFailed: return "join_failed";
        case Cnt::JoinStale: return "join_stale";
        case Cnt::Formed: return "formed";
        case Cnt::DirSent: return "dir_sent";
        case Cnt::DirDropped: return "dir_dropped";
        case Cnt::AckRcv: return "ack_rcv";
        case Cnt::AckApp: return "ack_app";
        case Cnt::AckRef: return "ack_ref";
        case Cnt::AckTimeout: return "ack_timeout";
        case Cnt::AckStale: return "ack_stale";
        case Cnt::ExitPending: return "exit_pending";
        case Cnt::ExitFailed: return "exit_failed";
        case Cnt::Left: return "left";
        case Cnt::LeaveUnconfirmed: return "leave_unconfirmed";
        case Cnt::Orphan: return "orphan";
        case Cnt::ExtLeft: return "ext_left";
        case Cnt::Dissolved: return "dissolved";
        case Cnt::Foreign: return "foreign";
        case Cnt::SkipUnknown: return "skip_unknown";
        case Cnt::SkipS7: return "skip_s7";
        case Cnt::GapSuspend: return "gap_suspend";
        case Cnt::ExitSkipped: return "exit_skipped";      // an Exit to a bot the supervisor no longer applies to (not in GROUP)
        case Cnt::LeaveSilent: return "leave_silent";      // a silent member at the end: no op (M2)
        case Cnt::LeaveBgWait: return "leave_bg_wait";     // a member in a BG queue at the end: no op (M2)
        case Cnt::LinesDropped: return "lines_dropped";
        case Cnt::Osc: return "osc";                       // G5: a membership ended < kOscSec after its ack 2, no natural end
        case Cnt::SelfLeft: return "self_left";            // a left ack (state 4) that ended its member
        case Cnt::LeftStale: return "left_stale";          // T30-1c: a left ack that did not match (not counted as ack_stale)
        case Cnt::FollowOtherMap: return "follow_other_map";          // T30-1d: other-map episodes closed
        case Cnt::FollowOtherMap600: return "follow_other_map_600";   // T30-1d: of them >= kOtherMapLongMs
        case Cnt::FollowOtherMapMs: return "follow_other_map_ms";     // T30-1d: their ms
        case Cnt::FollowQuestMs: return "follow_quest_ms";            // T30-1d: ms of acked QUEST followers (the base)
        case Cnt::FollowOtherMapShort: return "follow_other_map_short";   // T30-1d: episodes closed before confirmed (no line)
        case Cnt::Count: break;
    }
    return "unknown";
}

// Stands at the minute line (5.3: the only fields that are not increments).
struct Stands
{
    std::uint32_t types = 0;
    std::uint64_t groups = 0;
    std::uint64_t members = 0;
    std::uint64_t silent = 0;
    std::uint64_t owned = 0;
};

struct MinuteCounts
{
    std::uint64_t c[kCounts] = {};
    std::uint64_t leave[kEndReasons] = {};
    // 5.3: summed over the passes of the minute (candidate-passes), like every field but the stands.
    std::uint64_t candAdhoc = 0;
    std::uint64_t candQuest = 0;
    std::uint64_t candBusy = 0;
    std::uint64_t candZoneHold = 0;
    std::uint64_t wishes = 0;
    std::uint64_t noSupervisor = 0;
    std::uint64_t staleSnap = 0;
    std::uint64_t keyOverflow = 0;
    std::uint32_t gapMsMax = 0;
    std::uint32_t passUsMax = 0;
    // T30-1b (OB-10 smoke note): a level, not an increment - the bots skip_unknown without a break for >= kSkipUnknownLongSec at
    // the minute line (minute2 skip_unknown_30m).
    std::uint64_t skipUnknown30m = 0;
    // T30-1d: the longest other-map episode closed in the minute (ms), the open episodes at the line and the age of the oldest
    // of them (levels: a run's last minute2 shows an episode still open at its end).
    std::uint64_t otherMapMaxMs = 0;
    std::uint64_t otherMapOpen = 0;
    std::uint64_t otherMapOpenMaxMs = 0;
};

// "[GroupMgr] minute types= passes= cand_adhoc= ... owned=": fixed fields, increments except the stands (types, groups,
// members, silent, owned). Stays far below 4096 characters.
inline int FormatMinute(char* buf, std::size_t size, MinuteCounts const& m, Stands const& s)
{
    int n = std::snprintf(buf, size, "[GroupMgr] minute types=%u passes=%llu cand_adhoc=%llu cand_quest=%llu cand_busy=%llu"
        " cand_zone_hold=%llu wishes=%llu no_supervisor=%llu stale_snap=%llu key_overflow=%llu", s.types,
        (unsigned long long)m.c[std::size_t(Cnt::Passes)], (unsigned long long)m.candAdhoc, (unsigned long long)m.candQuest,
        (unsigned long long)m.candBusy, (unsigned long long)m.candZoneHold, (unsigned long long)m.wishes,
        (unsigned long long)m.noSupervisor, (unsigned long long)m.staleSnap, (unsigned long long)m.keyOverflow);
    for (std::size_t i = std::size_t(Cnt::OpJoinQ); i < kCountsMinuteLoop && n > 0 && std::size_t(n) < size; ++i)
        n += std::snprintf(buf + n, size - std::size_t(n), " %s=%llu", CntName(Cnt(i)), (unsigned long long)m.c[i]);
    if (n > 0 && std::size_t(n) < size)
        n += std::snprintf(buf + n, size - std::size_t(n), " gap_ms_max=%u pass_us_max=%u groups=%llu members=%llu silent=%llu owned=%llu",
            m.gapMsMax, m.passUsMax, (unsigned long long)s.groups, (unsigned long long)s.members, (unsigned long long)s.silent,
            (unsigned long long)s.owned);
    return n;
}

// "[GroupMgr] minute2 leave_<reason>=.. osc= self_left= skip_unknown_30m= left_stale= follow_other_map= follow_other_map_600=
// follow_other_map_ms= follow_quest_ms= follow_other_map_short= follow_other_map_max_ms= follow_other_map_open=
// follow_other_map_open_max_ms=": every reason, 0 included (increments), then the T30-1b counters osc and self_left
// (increments), skip_unknown_30m (a level), the T30-1c left_stale (an increment) and the T30-1d counters (increments, then the
// minute's max, and the open episodes and the oldest one's age, levels), each appended at the end.
inline int FormatMinute2(char* buf, std::size_t size, MinuteCounts const& m)
{
    int n = std::snprintf(buf, size, "[GroupMgr] minute2");
    for (std::size_t i = 1; i < kEndReasons && n > 0 && std::size_t(n) < size; ++i)
        n += std::snprintf(buf + n, size - std::size_t(n), " leave_%s=%llu", EndReasonName(EndReason(i)), (unsigned long long)m.leave[i]);
    for (std::size_t i = kCountsMinuteLoop; i < kCountsMinute2Loop && n > 0 && std::size_t(n) < size; ++i)
        n += std::snprintf(buf + n, size - std::size_t(n), " %s=%llu", CntName(Cnt(i)), (unsigned long long)m.c[i]);
    if (n > 0 && std::size_t(n) < size)
        n += std::snprintf(buf + n, size - std::size_t(n), " skip_unknown_30m=%llu", (unsigned long long)m.skipUnknown30m);
    for (std::size_t i = kCountsMinute2Loop; i < kCounts && n > 0 && std::size_t(n) < size; ++i)
        n += std::snprintf(buf + n, size - std::size_t(n), " %s=%llu", CntName(Cnt(i)), (unsigned long long)m.c[i]);
    if (n > 0 && std::size_t(n) < size)
        n += std::snprintf(buf + n, size - std::size_t(n), " follow_other_map_max_ms=%llu follow_other_map_open=%llu"
            " follow_other_map_open_max_ms=%llu", (unsigned long long)m.otherMapMaxMs, (unsigned long long)m.otherMapOpen,
            (unsigned long long)m.otherMapOpenMaxMs);
    return n;
}

// The group serial after last (design 3.2, 9b R3). The directive carries only its low 16 bits (b16-31), so a value whose low
// 16 bits are 0 (the view's "no live membership") or still in use (inUse: a group in the registry, an open Exit) is skipped.
// Terminates: far fewer than 65535 groups and Exits are ever held at once.
template <typename InUse>
inline std::uint32_t NextGroupSerial(std::uint32_t last, InUse const& inUse)
{
    for (;;)
    {
        ++last;
        std::uint16_t const low16 = std::uint16_t(last);
        if (low16 && !inUse(low16))
            return last;
    }
}

// ---- the engine: one pass per manager update (world thread) ---------------------------------------------------------------
class Engine
{
public:
    // The pass order (design 2.2, pinned, M15): (1) the caller drained the inbox into the tables and the acks, (2) op outcomes,
    // (3) acks, (4) gap rule, (5) confirmations and deadlines, (6) end rules, (7) forming, (8) the op budget (leaves first).
    void Pass(PassInput const& in, Rules const& r, Io& io)
    {
        now_ = in.nowMs;
        nowSec_ = in.nowSec;
        passDeltaMs_ = passed_ ? now_ - lastPassMs_ : 0u;   // T30-1d: follow_quest_ms accrues by the time since the last pass
        for (GoneEpisode const& ge : goneEpisodes_)          // T30-1d: the episodes ForgetGone closed before this pass
            OtherMapLine(io, ge.serial, ge.low, ge.ms, "gone", "-");
        goneEpisodes_.clear();
        staleMs_ = manager_inbox::StaleMs(r.statusSeconds);
        wishMs_ = WishFreshMs(r.statusSeconds);
        heldAtPassStart_ = HasGroups();   // T30-1e: the registry held anything when this pass began (the final minute flush)
        ++passNo_;
        swept_.clear();
        retryCooldownSec_ = r.retryCooldownMinutes * 60;
        Count(Cnt::Passes);
        ApplyOutcomes(in, io);
        ApplyAcks(in, r, io);
        GapRule(in, r, io);
        ConfirmEdges(in, r, io);
        if (!in.resumed)
            Deadlines(r, io);
        if (!in.resumed)
            EndRules(in, r, io);
        Form(in, r, io);
        RunOps(in, io);
        Sweep(in);
        lastPassMs_ = in.nowMs;
        passed_ = true;
        MinuteLine(in, r, io);
    }

    // The call site keeps the pass while anything is held (groups, an open Exit, a block): the switch-off path (2.8, M1).
    bool HasGroups() const { return !groups_.empty() || !bots_.empty(); }

    // Each pass but a resumed one (world thread): bots no longer in the bot map are forgotten (logout, release; no op possible).
    template <typename Present>
    void ForgetGone(Present const& present)
    {
        for (auto& entry : groups_)
            for (Member& m : entry.second.m)
                if (m.state != MemberState::Left && !present(m.low))
                {
                    // Its join (queued, or executed and unconfirmed) held the group's one join slot (M8); its outcome will
                    // find no member, so the slot is freed here.
                    if (!m.inviter && (m.opQueued || (m.state == MemberState::Joining && m.opExecMs)))
                        entry.second.joinInFlight = false;
                    CloseOtherMap(entry.second, m, "gone", "-", now_, nullptr);   // T30-1d: now_ = the last pass, line next pass
                    m.state = MemberState::Left;
                    m.leaveWanted = false;
                    m.opWanted = false;
                    m.opQueued = false;
                    m.leaveQueued = false;
                }
        for (auto it = bots_.begin(); it != bots_.end();)
            it = present(it->first) ? std::next(it) : bots_.erase(it);
        for (auto it = skipUnknown_.begin(); it != skipUnknown_.end();)
            it = present(it->first) ? std::next(it) : skipUnknown_.erase(it);
    }

    void NotePassUs(std::uint32_t us) { minute_.passUsMax = std::max(minute_.passUsMax, us); }

    // Read views (any thread).
    ManagedView const& View() const { return view_; }
    std::uint32_t QueuedAge(std::uint32_t low, std::uint32_t questId, std::uint32_t nowSec) const
    {
        return queueUsed_.load(std::memory_order_acquire) ? questQueue_.QueuedAge(low, questId, nowSec) : 0u;
    }

    // Tests and the minute line.
    std::unordered_map<std::uint32_t, ManagedGroup> const& Groups() const { return groups_; }
    // T33-0: the groups the last pass swept (the meeting shadow, read only).
    std::vector<SweptGroup> const& Swept() const { return swept_; }
    std::unordered_map<std::uint32_t, BotEntry> const& Bots() const { return bots_; }
    MinuteCounts const& Minute() const { return minute_; }
    std::uint64_t Total(Cnt c) const { return total_[std::size_t(c)]; }
    std::uint64_t TotalLeave(EndReason r) const { return totalLeave_[std::size_t(r)]; }
    std::uint32_t QuestAttempts(std::uint32_t low, std::uint32_t questId) const { return questQueue_.Attempts(low, questId); }
    bool QuestCooldown(std::uint32_t low, std::uint32_t questId, std::uint32_t nowSec) const { return QuestBlocked(low, questId, nowSec, 0); }

private:
    // ---- counting and lines ----
    void Count(Cnt c, std::uint64_t n = 1)
    {
        minute_.c[std::size_t(c)] += n;
        total_[std::size_t(c)] += n;
    }

    void Emit(Io& io, char const* text)
    {
        if (linesThisMinute_ >= kLinesPerMinute)
        {
            Count(Cnt::LinesDropped);
            return;
        }
        ++linesThisMinute_;
        io.Line(text);
    }

    // ---- lookups ----
    ManagedGroup* GroupOf(std::uint32_t low, Member** member)
    {
        *member = nullptr;
        auto const b = bots_.find(low);
        if (b == bots_.end() || !b->second.serial)
            return nullptr;
        auto const g = groups_.find(b->second.serial);
        if (g == groups_.end())
            return nullptr;
        for (Member& m : g->second.m)
            if (m.low == low && m.state != MemberState::Left)
            {
                *member = &m;
                return &g->second;
            }
        return nullptr;
    }

    // The directive seq (field a): process-wide monotonic and never 0. The engine lives as long as the process (the static
    // WorldThreadManager) and nothing resets the counter: a switch off/on, a resume or an empty registry keep it.
    std::uint32_t NextSeq()
    {
        if (!++nextSeq_)
            ++nextSeq_;   // the wrap skips 0
        return nextSeq_;
    }

    // The group serial: process-wide monotonic like the seq; NextGroupSerial skips what the wire's 16 bits would confuse.
    std::uint32_t NextSerial()
    {
        nextSerial_ = NextGroupSerial(nextSerial_, [this](std::uint16_t low16) { return Low16InUse(low16); });
        return nextSerial_;
    }

    // These low 16 bits still name something on the wire: a group in the registry, or an Exit still open or due.
    bool Low16InUse(std::uint16_t low16) const
    {
        for (auto const& entry : groups_)
            if (std::uint16_t(entry.first) == low16)
                return true;
        for (auto const& entry : bots_)
            if ((entry.second.exitOpen || entry.second.exitDue) && entry.second.exitBits.serial == low16)
                return true;
        return false;
    }

    // M15 "Stale-Anker": a snapshot received before the last world hitch ages only by the time between passes, so a hitch of
    // 30-120 s makes no member silent and ends no group.
    bool Fresh(std::uint32_t receivedMs) const
    {
        return AgeMs(receivedMs) <= staleMs_;
    }

    // A wish is usable only from the latest status period (WishFreshMs), with the same stale-anchor credit as Fresh.
    bool FreshWish(std::uint32_t receivedMs) const
    {
        return AgeMs(receivedMs) <= wishMs_;
    }

    std::uint32_t AgeMs(std::uint32_t receivedMs) const
    {
        std::uint32_t age = now_ - receivedMs;
        if (gapShiftMs_ && age > std::uint32_t(now_ - gapAtMs_))
            age = age > gapShiftMs_ ? age - gapShiftMs_ : 0u;
        return age;
    }

    bool QuestBlocked(std::uint32_t low, std::uint32_t questId, std::uint32_t nowSec, std::uint32_t maxAttempts) const
    {
        if (questQueue_.Blocked(low, questId, nowSec))
            return true;
        if (maxAttempts && questQueue_.Attempts(low, questId) >= maxAttempts)
            return true;
        auto const it = questCooldown_.find((std::uint64_t(low) << 32) | questId);
        return it != questCooldown_.end() && it->second > nowSec;
    }

    // ---- (2) op outcomes ----
    void ApplyOutcomes(PassInput const& in, Io& io)
    {
        if (!in.outcomes)
            return;
        for (group_ops::ManagerOutcome const& o : *in.outcomes)
        {
            bool const join = o.kind == group_ops::OpKind::Join;
            bool const leave = o.kind == group_ops::OpKind::Leave;
            if (!join && !leave)
                continue;
            if (join)
                Count(o.result == group_ops::ManagerOpResult::Executed ? Cnt::ExecJoin :
                    o.result == group_ops::ManagerOpResult::Failed ? Cnt::FailJoin : Cnt::StaleJoin);
            else
                Count(o.result == group_ops::ManagerOpResult::Executed ? Cnt::ExecLeave :
                    o.result == group_ops::ManagerOpResult::Failed ? Cnt::FailLeave : Cnt::StaleLeave);
            Member* m = nullptr;
            ManagedGroup* g = GroupOf(o.bot, &m);
            if (join)
            {
                if (!g || !m->opQueued)
                    continue;
                m->opQueued = false;
                if (o.result == group_ops::ManagerOpResult::Executed)
                {
                    // The join clock starts now (M7); the inviter of a new group runs on the same clock. A member that ended
                    // while its join was queued is in the core group now: RunOps waits for its edge, then leaves it.
                    m->opExecMs = manager_inbox::PostStamp(now_);
                    for (Member& other : g->m)
                        if (other.inviter && (other.state == MemberState::Joining || other.state == MemberState::Leaving) &&
                            !other.opExecMs)
                            other.opExecMs = m->opExecMs;
                    if (m->state == MemberState::Leaving)
                        g->joinInFlight = false;
                    OpLine(io, "join", *g, o.bot, o.other, "exec", m->reason);
                    continue;
                }
                g->joinInFlight = false;
                if (m->state == MemberState::Leaving)
                {
                    // Ended while its join was pending: the join never took, so its leave has nothing to do.
                    MarkLeft(*g, *m, io);
                    continue;
                }
                // Failed: the core refused, the invitee is not in the group (RunJoin uninvites). Stale/superseded: the op never
                // joined. Either way no leave is needed; the bot is a candidate again (2.3 Bildung).
                EndReason const why = o.result == group_ops::ManagerOpResult::Failed ? EndReason::JoinFailed : EndReason::JoinStale;
                Count(why == EndReason::JoinFailed ? Cnt::JoinFailed : Cnt::JoinStale);
                OpLine(io, "join", *g, o.bot, o.other, OpResultName(o.result), why);
                EndMember(*g, *m, why, io, false);
                continue;
            }
            // A leave: Execute (RunLeave checked the bot is out) or a drop that says it is out = left; else try again.
            BotEntry* entry = FindBot(o.bot);
            bool const out = o.result == group_ops::ManagerOpResult::Executed ||
                (o.result == group_ops::ManagerOpResult::Stale &&
                    (o.verdict == group_ops::Verdict::DropAlreadyLeft || o.verdict == group_ops::Verdict::DropGroupChanged));
            if (g && m->state == MemberState::Leaving && m->leaveQueued)
            {
                m->leaveQueued = false;
                OpLine(io, "leave", *g, o.bot, o.other, OpResultName(o.result), m->reason);
                if (out)
                {
                    NoteOut(o.bot, o.groupId ? o.groupId : g->coreGroupId);
                    MarkLeft(*g, *m, io);
                }
                else
                    LeaveRetry(*g, *m, io);
            }
            else if (entry && entry->staleGroupId && out && o.groupId == entry->staleGroupId)
                entry->staleGroupId = 0;
        }
    }

    BotEntry* FindBot(std::uint32_t low)
    {
        auto const it = bots_.find(low);
        return it == bots_.end() ? nullptr : &it->second;
    }

    // 5.2 op results; "failed" (the core refused the join or the leave) is kept apart from "stale" (the op never ran).
    static char const* OpResultName(group_ops::ManagerOpResult r)
    {
        return r == group_ops::ManagerOpResult::Executed ? "exec" : r == group_ops::ManagerOpResult::Failed ? "failed" : "stale";
    }

    // A leave took the bot out of groupId (2.3 Ende): until its edge shows that, its old edges start nothing (Strays).
    void NoteOut(std::uint32_t low, std::uint32_t groupId)
    {
        if (!groupId)
            return;
        BotEntry& b = bots_[low];
        b.outGroupId = groupId;
        b.outMs = manager_inbox::PostStamp(now_);
    }

    // ---- (3) acks (3.4) ----
    void ApplyAcks(PassInput const& in, Rules const& r, Io& io)
    {
        if (!in.acks)
            return;
        for (Ack const& a : *in.acks)
        {
            if (a.state == manager_inbox::kAckLeft)
            {
                ApplyLeftAck(a, io);
                continue;
            }
            // An ack names its directive by the seq alone (PostDirectiveAck has no serial field). A seq is unique and goes to
            // one bot with one serial: the open Exit's (exitBits.serial) or the Enter of the bot's current group (the group
            // GroupOf finds is the one SendEnter took the serial from). So seq + bot = seq + serial.
            BotEntry* entry = FindBot(a.low);
            bool const exitAck = entry && entry->exitOpen && a.seq == entry->exitSeq;
            Member* m = nullptr;
            ManagedGroup* g = exitAck ? nullptr : GroupOf(a.low, &m);
            bool const enterAck = g && m->state == MemberState::Directed && a.seq == m->enterSeq;
            if (!exitAck && !enterAck)
            {
                Count(Cnt::AckStale);   // superseded or unknown (R4), any state: counted, nothing else
                continue;
            }
            if (a.state == manager_inbox::kAckReceived)
            {
                Count(Cnt::AckRcv);
                continue;
            }
            if (exitAck)
            {
                entry->exitOpen = false;
                // The member may have left and its group dissolved by now: the line names the Exit's own serial.
                AckLine(io, entry->exitBits.serial, a, Verb::Exit, now_ - entry->exitSentMs);
                if (ExitDone(a.state, a.reason))
                {
                    entry->exitDue = false;
                    continue;
                }
                ExitFailure(a.low, *entry, io);
                continue;
            }
            AckLine(io, g->serial, a, Verb::Enter, now_ - m->sentMs);
            if (a.state == manager_inbox::kAckApplied)
            {
                Count(Cnt::AckApp);
                m->state = MemberState::Acked;
                m->ackKnown = true;
                m->ackSec = nowSec_;
                continue;
            }
            Count(Cnt::AckRef);
            EndMember(*g, *m, EndReason::AckRefused, io, true);
        }
        (void)r;
    }

    // T30-1b (G2, agreed with CLI-Projekt 10.11.): the bot left the GROUP mode itself (ack state 4, seq 0, the serial it held in
    // b16-31). It counts only for the bot's current group under that serial, while its Enter is applied (Acked) and no Exit
    // of the bot is open; then the member ends with bot_left_* and the usual path sends the tracked Exit and the leave op.
    // Anything else (a seq, another serial, no group, not Acked, an Exit open) is left_stale (T30-1c, OB-10 note on #992; its
    // own counter, not ack_stale) and nothing else.
    void ApplyLeftAck(Ack const& a, Io& io)
    {
        BotEntry const* entry = FindBot(a.low);
        Member* m = nullptr;
        ManagedGroup* g = a.seq == 0 ? GroupOf(a.low, &m) : nullptr;
        if (!g || std::uint16_t(g->serial) != a.serial || m->state != MemberState::Acked || (entry && entry->exitOpen))
        {
            Count(Cnt::LeftStale);
            return;
        }
        Count(Cnt::SelfLeft);
        char line[192];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=self_left group=%u bot=%u reason=%s", g->serial, a.low,
            AckReasonName(a.reason));
        Emit(io, line);
        EndMember(*g, *m, FromLeftReason(a.reason), io, true);
    }

    // ---- (4) gap rule and resume (M15, M16) ----
    void GapRule(PassInput const& in, Rules const& r, Io& io)
    {
        if (in.resumed)
        {
            // Every ms anchor restarts now, the second clocks of the end rules start over; no deadline ends in this pass and a
            // member without a snapshot is silent only kStaleMs after now.
            for (auto& entry : groups_)
            {
                ManagedGroup& g = entry.second;
                g.lastProgressSec = nowSec_;
                g.phaseSinceSec = nowSec_;
                if (g.state == GroupState::Forming)
                    g.createdSec = nowSec_;   // kFormingMaxSec counts from the resume
                g.realPlayerSinceMs = 0;
                for (Member& m : g.m)
                {
                    // T30-1d: an open episode ends at the last pass before the gap (the base skips the gap as well).
                    CloseOtherMap(g, m, "resume", "-", lastPassMs_, &io);
                    if (m.opExecMs)
                        m.opExecMs = manager_inbox::PostStamp(now_);
                    if (m.sentMs)
                        m.sentMs = now_;
                    if (m.leaveMs)
                        m.leaveMs = now_;
                    m.addedMs = now_;
                    m.outOfRangeSince = 0;
                    m.levelWindowSince = 0;
                    m.leaderOtherMapSince = 0;
                    m.lastProgressSec = nowSec_;
                }
            }
            for (auto& entry : bots_)
            {
                if (entry.second.exitOpen)
                    entry.second.exitSentMs = now_;
                if (entry.second.outGroupId)
                    entry.second.outMs = manager_inbox::PostStamp(now_);
            }
            gapShiftMs_ = 0;   // the tables were cleared: no snapshot from before the resume is left
            GapLine(io, passed_ ? now_ - lastPassMs_ : 0u, "resume");
            return;
        }
        if (!passed_)
            return;
        // The stale-anchor credit of an earlier hitch ends once every snapshot from before it is stale anyway.
        if (gapShiftMs_ && std::uint64_t(std::uint32_t(now_ - gapAtMs_)) > std::uint64_t(staleMs_) + gapShiftMs_)
            gapShiftMs_ = 0;
        std::uint32_t const gap = now_ - lastPassMs_;
        std::uint32_t const interval = std::max<std::uint32_t>(r.intervalMs, 1000u);
        if (gap <= 2 * interval)
            return;
        // A world hitch: the deadlines measure only the time between passes. Steps (2) and (3) ran before this one: a clock
        // they set in this pass (opExecMs, exitSentMs, outMs) is current and does not move, or it would lie in the future.
        std::uint32_t const shift = gap - std::min(gap, r.intervalMs);
        Count(Cnt::GapSuspend);
        minute_.gapMsMax = std::max(minute_.gapMsMax, gap);
        GapLine(io, gap, "suspend");
        std::uint32_t const stamp = manager_inbox::PostStamp(now_);
        auto const shiftClock = [this, shift, stamp](std::uint32_t& clock)
        {
            if (clock && clock != now_ && clock != stamp)
                clock += shift;
        };
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            shiftClock(g.realPlayerSinceMs);
            for (Member& m : g.m)
            {
                shiftClock(m.opExecMs);
                shiftClock(m.sentMs);
                shiftClock(m.leaveMs);
                if (m.addedMs != now_)
                    m.addedMs += shift;
            }
        }
        for (auto& entry : bots_)
        {
            if (entry.second.exitOpen)
                shiftClock(entry.second.exitSentMs);
            shiftClock(entry.second.outMs);
        }
        // The stale anchor (M15): snapshots received before this pass age by the time between passes only (Fresh).
        gapShiftMs_ = std::min<std::uint32_t>(kGapShiftMaxMs, gapShiftMs_ + shift);
        gapAtMs_ = now_;
    }

    void GapLine(Io& io, std::uint32_t ms, char const* action)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=gap ms=%u action=%s", ms, action);
        Emit(io, line);
    }

    // ---- (5a) confirmations from the edges ----
    void ConfirmEdges(PassInput const& in, Rules const& r, Io& io)
    {
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            // A new group: the invitee's edge names the inviter as leader -> the core group id. A member that ended while its
            // join was queued counts too: its join ran, and its leave needs the id.
            if (!g.coreGroupId)
                for (Member const& m : g.m)
                {
                    if (m.inviter || (m.state != MemberState::Joining && m.state != MemberState::Leaving) || !m.opExecMs)
                        continue;
                    manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
                    if (e && e->edge.groupId && e->edge.leaderLow == g.leaderLow)
                    {
                        g.coreGroupId = e->edge.groupId;
                        break;
                    }
                }
            for (Member& m : g.m)
            {
                manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
                if (!e)
                    continue;
                if (m.state == MemberState::Joining)
                {
                    if (!g.coreGroupId || e->edge.groupId != g.coreGroupId || (!m.inviter && !m.opExecMs))
                        continue;
                    Count(Cnt::Joined);
                    char line[256];
                    std::snprintf(line, sizeof(line), "[GroupMgr] event=joined group=%u core=%u bot=%u leader=%u members=%u ms=%u",
                        g.serial, g.coreGroupId, m.low, g.leaderLow, e->edge.members, now_ - (m.opExecMs ? m.opExecMs : now_));
                    Emit(io, line);
                    m.state = MemberState::Joined;
                    m.joinedSec = nowSec_;
                    m.lastProgressSec = nowSec_;
                    if (!m.inviter)
                        g.joinInFlight = false;
                    SendEnter(g, m, in, io);
                    continue;
                }
                if (m.state == MemberState::Leaving && m.joinedSec && g.coreGroupId && Fresh(e->receivedMs) && e->edge.groupId != g.coreGroupId &&
                    !m.opQueued)
                {
                    m.leaveQueued = false;
                    MarkLeft(g, m, io);
                    continue;
                }
                // F15 (a): left on an allowed other path (repop): ext_left; the group goes on with >= 2.
                if ((m.state == MemberState::Joined || m.state == MemberState::Directed || m.state == MemberState::Acked) &&
                    g.coreGroupId && Fresh(e->receivedMs) && e->edge.groupId != g.coreGroupId)
                {
                    Count(Cnt::ExtLeft);
                    EndMember(g, m, EndReason::ExtLeft, io, true);
                    m.leaveWanted = false;
                    MarkLeft(g, m, io);
                }
            }
            UpdateActive(g, in, io);
        }
        (void)r;
    }

    void UpdateActive(ManagedGroup& g, PassInput const& in, Io& io)
    {
        if (g.state != GroupState::Forming)
            return;
        std::uint32_t acked = 0;
        std::uint32_t lo = 0xFFFFFFFFu;
        std::uint32_t hi = 0;
        for (Member const& m : g.m)
            if (m.state == MemberState::Acked)
            {
                ++acked;
                if (manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low))
                {
                    lo = std::min<std::uint32_t>(lo, s->level);
                    hi = std::max<std::uint32_t>(hi, s->level);
                }
            }
        if (acked < 2)
            return;
        g.state = GroupState::Active;
        g.formedSec = nowSec_;
        g.phaseSinceSec = nowSec_;
        g.lastProgressSec = nowSec_;
        Count(Cnt::Formed);
        char line[256];
        // 5.2: levels from the acked members' snapshots (0-0 without any); the cohort of the leader (CohortPolicy.h InB).
        std::snprintf(line, sizeof(line), "[GroupMgr] event=formed group=%u core=%u type=%s quest=%u objective=%d leader=%u members=%u"
            " levels=%u-%u map=%u cohort=%c", g.serial, g.coreGroupId, TypeName(g.type), g.questId, int(g.objective), g.leaderLow, acked,
            hi ? lo : 0u, hi, g.mapId, cohort::InB(g.leaderLow) ? 'B' : 'A');
        Emit(io, line);
    }

    // ---- directives ----
    void SendEnter(ManagedGroup& g, Member& m, PassInput const& in, Io& io)
    {
        DirectiveBits d;
        d.verb = Verb::Enter;
        d.type = g.type;
        d.objective = g.objective;
        d.serial = std::uint16_t(g.serial);
        std::uint32_t const seq = NextSeq();
        bool const ok = io.PostDirective(m.low, MakeDirective(seq, d, g.questId, g.mapId, g.anchorX, g.anchorY, now_));
        DirectiveLine(io, g, m.low, seq, d, ok);
        if (!ok)
        {
            // R10: not delivered (supervisor off for the bot, or its ring full) - no ack will come; fail closed at once.
            Count(Cnt::DirDropped);
            EndMember(g, m, EndReason::DirectiveDropped, io, true);
            return;
        }
        Count(Cnt::DirSent);
        m.state = MemberState::Directed;
        m.enterSeq = seq;
        m.sentMs = now_;
        m.enterDelivered = true;
        (void)in;
    }

    // tracked = false (a silent member, M2): one best-effort Exit - the bot posts nothing, so no ack wait, no retry, no alarm.
    void PostExit(std::uint32_t low, BotEntry& entry, Io& io, bool tracked = true)
    {
        entry.exitDue = false;
        char line[256];
        if (!io.SupervisorApplies(low))
        {
            // R10: the supervisor does not apply to the bot (no_supervisor, switched off): it cannot be in GROUP, so there is
            // nothing to exit. Done, not dropped (no dir_dropped / exit_failed alarm, 5.4).
            entry.exitOpen = false;
            Count(Cnt::ExitSkipped);
            std::snprintf(line, sizeof(line), "[GroupMgr] event=directive group=%u bot=%u seq=0 verb=exit type=%s quest=%u objective=%d result=skipped",
                std::uint32_t(entry.exitBits.serial), low, TypeName(entry.exitBits.type), entry.exitQuest, int(entry.exitBits.objective));
            Emit(io, line);
            return;
        }
        std::uint32_t const seq = NextSeq();
        bool const ok = io.PostDirective(low,
            MakeDirective(seq, entry.exitBits, entry.exitQuest, entry.exitMap, entry.exitX, entry.exitY, now_));
        ++entry.exitTries;
        std::snprintf(line, sizeof(line), "[GroupMgr] event=directive group=%u bot=%u seq=%u verb=exit type=%s quest=%u objective=%d result=%s",
            std::uint32_t(entry.exitBits.serial), low, seq, TypeName(entry.exitBits.type), entry.exitQuest, int(entry.exitBits.objective),
            ok ? "posted" : "dropped");
        Emit(io, line);
        if (!tracked)
        {
            entry.exitOpen = false;
            if (ok)
                Count(Cnt::DirSent);
            return;
        }
        if (!ok)
        {
            Count(Cnt::DirDropped);
            ExitFailure(low, entry, io);
            return;
        }
        Count(Cnt::DirSent);
        entry.exitOpen = true;
        entry.exitSeq = seq;
        entry.exitSentMs = now_;
    }

    // An Exit dropped, refused (another deny) or unanswered: once more in the next pass, then the bot is blocked (2.3 Exit).
    void ExitFailure(std::uint32_t low, BotEntry& entry, Io& io)
    {
        entry.exitOpen = false;
        if (entry.exitTries < kExitTries)
        {
            entry.exitDue = true;
            entry.exitDuePass = passNo_;
            Count(Cnt::ExitPending);
            return;
        }
        entry.exitDue = false;
        entry.blockedUntilSec = nowSec_ + kExitBlockSec;
        Count(Cnt::ExitFailed);
        char line[128];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=exit_failed bot=%u", low);
        Emit(io, line);
    }

    // ---- ending a member / a group ----
    // T30-1d: a QUEST follower on another map than its leader (OB-10 review of R9, B4). An episode opens on fresh snapshots
    // that show two maps and counts only once confirmed: a later pass still shows two maps on snapshots of both received at or
    // after the open. Without that a joint map change (portal, boat, a dissolving leader_stuck group) would make a phantom
    // episode of up to one status period, so an unconfirmed close only counts follow_other_map_short. The episode closes when
    // both are on one map again (end=back), when the member ends (end=ended with its reason: every end, switch_off and disarm
    // included), when it leaves the bot map (end=gone, ForgetGone) or at a resume (end=resume, at the last pass before the gap:
    // the base skips the gap too). One line per counted episode (the line budget) and the minute2 counters, which count even
    // when lines drop.
    void OtherMapEpisode(ManagedGroup& g, Member& m, bool otherMap, std::uint32_t leaderRcvMs, std::uint32_t memberRcvMs, Io& io)
    {
        if (otherMap && !m.otherMapSinceMs)
        {
            m.otherMapSinceMs = manager_inbox::PostStamp(now_);
            m.otherMapConfirmed = false;
        }
        else if (otherMap && !m.otherMapConfirmed && !After(m.otherMapSinceMs, leaderRcvMs) && !After(m.otherMapSinceMs, memberRcvMs))
            m.otherMapConfirmed = true;
        else if (!otherMap && m.otherMapSinceMs)
            CloseOtherMap(g, m, "back", "-", now_, &io);
    }

    // Closes an open episode at atMs; io null (ForgetGone runs before the pass) queues the line for the next pass.
    void CloseOtherMap(ManagedGroup& g, Member& m, char const* end, char const* reason, std::uint32_t atMs, Io* io)
    {
        if (!m.otherMapSinceMs)
            return;
        std::uint32_t const ms = After(atMs, m.otherMapSinceMs) ? atMs - m.otherMapSinceMs : 0u;
        bool const confirmed = m.otherMapConfirmed;
        m.otherMapSinceMs = 0;
        m.otherMapConfirmed = false;
        if (!confirmed)
        {
            Count(Cnt::FollowOtherMapShort);
            return;
        }
        Count(Cnt::FollowOtherMap);
        Count(Cnt::FollowOtherMapMs, ms);
        if (ms >= kOtherMapLongMs)
            Count(Cnt::FollowOtherMap600);
        minute_.otherMapMaxMs = std::max<std::uint64_t>(minute_.otherMapMaxMs, ms);
        if (io)
            OtherMapLine(*io, g.serial, m.low, ms, end, reason);
        else
            goneEpisodes_.push_back(GoneEpisode{ g.serial, m.low, ms });
    }

    void OtherMapLine(Io& io, std::uint32_t serial, std::uint32_t low, std::uint32_t ms, char const* end, char const* reason)
    {
        char line[192];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=other_map group=%u bot=%u ms=%u end=%s reason=%s", serial, low, ms, end,
            reason);
        Emit(io, line);
    }

    void EndMember(ManagedGroup& g, Member& m, EndReason why, Io& io, bool mayBeGrouped)
    {
        if (m.state == MemberState::Leaving || m.state == MemberState::Left)
            return;
        CloseOtherMap(g, m, "ended", EndReasonName(why), now_, &io);
        m.reason = why;
        ++minute_.leave[std::size_t(why)];
        ++totalLeave_[std::size_t(why)];
        // G5 (T30-1b): an end without a natural reason less than kOscSec after the Enter was applied is an oscillation.
        if (CountsAsOsc(m.ackKnown, m.ackSec, nowSec_, why))
            Count(Cnt::Osc);
        // A member that cannot be in the core group (its join never ran, none is pending) needs no leave op.
        bool const neverJoined = m.state == MemberState::Joining && !m.inviter && !m.opExecMs && !m.opQueued;
        // The one join in flight of the group was this member's (executed, not confirmed): the next join may go. A queued one
        // keeps the flag until its outcome.
        if (m.state == MemberState::Joining && !m.inviter && m.opExecMs)
            g.joinInFlight = false;
        m.opWanted = false;
        // M2: a silent member (no posts since kStaleMs) or one in a BG queue gets no leave op at the end (2.3); it stays held
        // (staleGroupId) until its edge shows another group, and posting again from the group gets it the orphan leave.
        // A joining member (its join queued or unconfirmed) is not quiet: its leave waits for the join (RunOps).
        bool const quiet = mayBeGrouped && (m.silent || m.bgWait) &&
            (m.state == MemberState::Joined || m.state == MemberState::Directed || m.state == MemberState::Acked);
        // F13 (a): the QUEST attempt count of S7 for S7 reasons (turned_in, or a member that turned the quest in, forgets the
        // entry like S7 DissolveGroup); the manager's own reasons only set the retry cooldown. T30-1b: bot_left_done (the bot
        // reached its goal) forgets the entry like a member that turned the quest in; bot_left_lost / bot_left_off are no S7
        // dissolve reason (CountsAsQuestAttempt is false), so they only set the retry cooldown like the manager's own reasons.
        if (g.type == GroupType::Quest && g.questId)
        {
            if (why == EndReason::TurnedIn || why == EndReason::BotLeftDone || m.turnedIn)
                questQueue_.Forget(m.low, g.questId);
            else if (CountsAsQuestAttempt(why))
            {
                // Seen first: a fresh entry, so the minute sweep (ForgetStale) does not drop the attempt at once.
                questQueue_.Seen(m.low, g.questId, nowSec_);
                questQueue_.Failed(m.low, g.questId, nowSec_, retryCooldownSec_);
            }
            else
                questCooldown_[(std::uint64_t(m.low) << 32) | g.questId] = nowSec_ + retryCooldownSec_;
        }
        if (m.enterDelivered)
        {
            BotEntry& entry = bots_[m.low];
            entry.exitBits.verb = Verb::Exit;
            entry.exitBits.type = g.type;
            entry.exitBits.objective = g.objective;
            entry.exitBits.reason = why;
            entry.exitBits.serial = std::uint16_t(g.serial);
            entry.exitQuest = g.questId;
            entry.exitMap = g.mapId;
            entry.exitX = g.anchorX;
            entry.exitY = g.anchorY;
            entry.exitTries = 0;
            PostExit(m.low, entry, io, !m.silent);
        }
        char line[256];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=leave group=%u bot=%u reason=%s members=%u minutes=%u",
            g.serial, m.low, EndReasonName(why), std::uint32_t(g.m.size()), g.formedSec ? (nowSec_ - g.formedSec) / 60 : 0u);
        Emit(io, line);
        m.state = MemberState::Leaving;
        m.leaveTries = 0;
        m.leaveQueued = false;
        m.leaveWanted = mayBeGrouped && !neverJoined && !quiet;
        if (quiet)
        {
            Count(m.silent ? Cnt::LeaveSilent : Cnt::LeaveBgWait);
            if (g.coreGroupId)
                bots_[m.low].staleGroupId = g.coreGroupId;
            MarkLeft(g, m, io);
            return;
        }
        if (!mayBeGrouped || neverJoined)
            MarkLeft(g, m, io);
    }

    void EndGroup(ManagedGroup& g, EndReason why, Io& io)
    {
        if (g.state == GroupState::Ending)
            return;
        g.state = GroupState::Ending;
        g.endReason = why;
        // Non-leaders first: the last leave dissolves the core group (GroupOps.cpp RunLeave).
        for (Member& m : g.m)
            if (m.low != g.leaderLow)
                EndMember(g, m, why, io, true);
        for (Member& m : g.m)
            if (m.low == g.leaderLow)
                EndMember(g, m, why, io, true);
    }

    void MarkLeft(ManagedGroup& g, Member& m, Io& io)
    {
        if (m.state == MemberState::Left)
            return;
        CloseOtherMap(g, m, "ended", EndReasonName(m.reason), now_, &io);
        m.state = MemberState::Left;
        m.leaveWanted = false;
        m.leaveQueued = false;
        m.opWanted = false;
        Count(Cnt::Left);
        char line[128];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=left group=%u bot=%u ms=%u", g.serial, m.low, m.leaveMs ? now_ - m.leaveMs : 0u);
        Emit(io, line);
        auto const b = bots_.find(m.low);
        if (b != bots_.end() && b->second.serial == g.serial)
            b->second.serial = 0;
    }

    void LeaveRetry(ManagedGroup& g, Member& m, Io& io)
    {
        if (m.leaveTries < kLeaveTries)
        {
            m.leaveWanted = true;
            return;
        }
        // leave_unconfirmed (alarm, not for switch_off): the bot stays held until its edge shows another group.
        if (m.reason != EndReason::SwitchOff)
        {
            Count(Cnt::LeaveUnconfirmed);
            char line[128];
            std::snprintf(line, sizeof(line), "[GroupMgr] event=leave_unconfirmed group=%u bot=%u", g.serial, m.low);
            Emit(io, line);
        }
        bots_[m.low].staleGroupId = g.coreGroupId;
        MarkLeft(g, m, io);
    }

    // ---- (5b) deadlines ----
    void Deadlines(Rules const& r, Io& io)
    {
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            for (Member& m : g.m)
            {
                if (m.state == MemberState::Joining && !m.inviter && m.opExecMs && now_ - m.opExecMs > kJoinConfirmMs)
                {
                    Count(Cnt::JoinFailed);
                    EndMember(g, m, EndReason::JoinFailed, io, true);
                }
                else if (m.state == MemberState::Directed && now_ - m.sentMs > kAckTimeoutMs)
                {
                    Count(Cnt::AckTimeout);
                    EndMember(g, m, EndReason::AckTimeout, io, true);
                }
                else if (m.state == MemberState::Leaving && m.leaveQueued && now_ - m.leaveMs > kLeaveConfirmMs)
                {
                    m.leaveQueued = false;
                    LeaveRetry(g, m, io);
                }
            }
        }
        for (auto& entry : bots_)
        {
            // F8 (a): the second Exit goes out one pass after the first failed (a full ring may have drained by then).
            if (entry.second.exitDue && !entry.second.exitOpen && passNo_ > entry.second.exitDuePass)
                PostExit(entry.first, entry.second, io);
            else if (entry.second.exitOpen && now_ - entry.second.exitSentMs > kAckTimeoutMs)
                ExitFailure(entry.first, entry.second, io);
        }
        // A group still forming after kFormingMaxSec (a join that never got its turn): it ends, nothing stays half built.
        for (auto& entry : groups_)
            if (entry.second.state == GroupState::Forming && nowSec_ - entry.second.createdSec > kFormingMaxSec)
                EndGroup(entry.second, EndReason::JoinStale, io);
        (void)r;
    }

    // ---- (6) end rules (2.6) ----
    void EndRules(PassInput const& in, Rules const& r, Io& io)
    {
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            if (g.state == GroupState::Ending)
                continue;
            // Member flags: silent (no posts since kStaleMs) and bg_wait - no op for them, they do not count for the size.
            for (Member& m : g.m)
            {
                if (m.state == MemberState::Leaving || m.state == MemberState::Left)
                    continue;
                manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low);
                m.silent = s ? !Fresh(s->receivedMs) : now_ - m.addedMs > staleMs_;
                m.bgWait = s && !m.silent && manager_inbox::UnpackFlags(s->flags).inBgQueue;
            }
            // 2.8: Types or Manager back to 0 (or the leader left the cohort): every group ends, one op per pass.
            if (!TypeOn(r.types, g.type) || !Owns(r.mode, r.types, g.leaderLow))
            {
                EndGroup(g, EndReason::SwitchOff, io);
                continue;
            }
            bool noSupervisor = false;
            std::uint32_t live = 0;
            std::uint32_t open = 0;
            for (Member const& m : g.m)
            {
                if (m.state == MemberState::Leaving || m.state == MemberState::Left)
                    continue;
                ++open;
                bool const inGroup = m.state == MemberState::Joined || m.state == MemberState::Directed || m.state == MemberState::Acked;
                if (inGroup && !m.silent && !m.bgWait)
                    ++live;
                if (inGroup && !m.silent && !io.SupervisorApplies(m.low))
                    noSupervisor = true;
            }
            if (noSupervisor)
            {
                EndGroup(g, EndReason::NoSupervisor, io);   // m7
                continue;
            }
            if (g.state == GroupState::Forming)
            {
                // F13 (a): a group that never got going lost its members to the manager's own reasons (op_refused,
                // join_*, ack_*, directive_dropped): it ends as join_stale, which charges no QUEST attempt. too_small (an
                // S7 reason) is for formed groups only.
                if (open < 2)
                    EndGroup(g, EndReason::JoinStale, io);
                continue;
            }
            if (live < 2)
            {
                EndGroup(g, EndReason::TooSmall, io);
                continue;
            }
            if (g.type == GroupType::Adhoc)
                AdhocRules(g, in, r, io);
            else
                QuestRules(g, in, r, io);
        }
        Strays(in, r, io);
    }

    Member* Leader(ManagedGroup& g)
    {
        for (Member& m : g.m)
            if (m.low == g.leaderLow)
                return &m;
        return nullptr;
    }

    bool LeaderLost(ManagedGroup& g, PassInput const& in)
    {
        Member* leader = Leader(g);
        if (!leader || leader->silent || leader->state == MemberState::Leaving || leader->state == MemberState::Left)
            return true;
        for (Member const& m : g.m)
        {
            if (m.state != MemberState::Directed && m.state != MemberState::Acked)
                continue;
            manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
            if (e && Fresh(e->receivedMs) && e->edge.groupId == g.coreGroupId && e->edge.leaderLow != g.leaderLow)
                return true;   // the core changed the leader (no leader change in T30)
        }
        return false;
    }

    void TrackProgress(ManagedGroup& g, Member& m, manager_inbox::KeyFacts const& k)
    {
        if (k.questId != g.questId)
            return;
        std::uint8_t const p = std::uint8_t(k.progress & 0x7u);
        if (m.progressKnown && p != m.progress)
        {
            m.lastProgressSec = nowSec_;
            g.lastProgressSec = nowSec_;
        }
        m.progress = p;
        m.progressKnown = true;
    }

    void AdhocRules(ManagedGroup& g, PassInput const& in, Rules const& r, Io& io)
    {
        if (LeaderLost(g, in))
        {
            EndGroup(g, EndReason::LeaderLost, io);   // m5
            return;
        }
        manager_inbox::StatusSnapshot const* ls = FindStatus(in.status, g.leaderLow);
        std::uint32_t lo = 0xFFFFFFFFu;
        std::uint32_t hi = 0;
        for (Member const& m : g.m)
        {
            manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low);
            if (!s || m.silent || m.state == MemberState::Leaving || m.state == MemberState::Left)
                continue;
            lo = std::min<std::uint32_t>(lo, s->level);
            hi = std::max<std::uint32_t>(hi, s->level);
        }
        bool const outsideWindow = hi >= lo && !adhoc_group::LevelWindowOk(lo, hi, r.levelWindow);
        for (Member& m : g.m)
        {
            if (m.state != MemberState::Acked || m.silent || m.bgWait)
                continue;
            manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low);
            if (!s)
                continue;
            manager_inbox::KeyFacts const k = manager_inbox::UnpackKey(s->key);
            manager_inbox::StatusFlags const f = manager_inbox::UnpackFlags(s->flags);
            manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
            TrackProgress(g, m, k);
            bool const otherMapRule = cohort::Applies(r.leaderOtherMapMode, m.low);
            bool const isLeader = m.low == g.leaderLow;
            bool const otherMap = !isLeader && ls && ls->mapId != s->mapId;
            bool const beyond = !isLeader && ls && !otherMap &&
                Dist2d(ls->x, ls->y, s->x, s->y) > 4.0f * r.adhocRadius * r.adhocRadius;
            bool const outOfRange = !isLeader && ls && adhoc_group::OutOfRangeFact(otherMap, beyond, otherMapRule);
            m.outOfRangeSince = outOfRange ? (m.outOfRangeSince ? m.outOfRangeSince : nowSec_) : 0;
            m.leaderOtherMapSince = adhoc_group::LeaderOtherMapClock(otherMapRule, ls != nullptr && !isLeader, otherMap,
                m.leaderOtherMapSince, nowSec_);
            m.levelWindowSince = outsideWindow ? (m.levelWindowSince ? m.levelWindowSince : nowSec_) : 0;

            adhoc_group::LeaveFacts facts;
            bool const sameKey = k.questId == g.questId;
            facts.questInProgress = !sameKey ||
                k.questStatus == manager_inbox::kQuestIncomplete || k.questStatus == manager_inbox::kQuestComplete;
            facts.objectiveDone = sameKey && k.objDone;
            facts.inInstance = f.dungeonMap || (e && e->edge.raid);
            facts.now = nowSec_;
            facts.outOfRangeSince = m.outOfRangeSince;
            facts.levelWindowSince = m.levelWindowSince;
            facts.lastProgress = m.lastProgressSec;
            facts.leaderOtherMapSince = m.leaderOtherMapSince;
            facts.leaderOtherMapSeconds = otherMapRule ? r.leaderOtherMapSeconds : 0;
            facts.helpUntilDone = r.helpUntilDone;
            if (facts.helpUntilDone && facts.objectiveDone)
                for (Member const& other : g.m)
                {
                    if (other.low == m.low || other.state != MemberState::Acked || other.silent)
                        continue;
                    manager_inbox::StatusSnapshot const* os = FindStatus(in.status, other.low);
                    if (!os)
                        continue;
                    manager_inbox::KeyFacts const ok = manager_inbox::UnpackKey(os->key);
                    if (ok.questId == g.questId && !ok.objDone)
                        facts.groupOpen = true;
                    facts.lastProgress = std::max(facts.lastProgress, other.lastProgressSec);
                }
            EndReason const why = FromLeave(adhoc_group::DecideLeave(facts));
            if (why == EndReason::None)
                continue;
            if (isLeader)
            {
                EndGroup(g, why, io);   // no leader change in T30: the group ends with its leader
                return;
            }
            EndMember(g, m, why, io, true);
        }
    }

    void QuestRules(ManagedGroup& g, PassInput const& in, Rules const& r, Io& io)
    {
        quest_groups::GroupFacts f;
        f.phase = g.phase;
        f.now = nowSec_;
        f.formedAt = g.formedSec;
        f.phaseSince = g.phaseSinceSec;
        manager_inbox::StatusSnapshot const* ls = FindStatus(in.status, g.leaderLow);
        manager_inbox::EdgeSnapshot const* le = FindEdge(in.edges, g.leaderLow);
        f.leaderLost = LeaderLost(g, in);
        f.leaderStuck = (ls && ls->mapId != g.mapId) || (le && Fresh(le->receivedMs) && le->edge.posStuck);
        bool allComplete = true;
        bool allTurnedIn = true;
        bool gathered = true;
        std::uint32_t members = 0;
        std::uint32_t reporters = 0;
        for (Member& m : g.m)
        {
            if (m.state == MemberState::Left)
                continue;
            // m6: every member still in the core group reports it, a Leaving one too (its leave may wait in the op queue).
            manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
            if (e && Fresh(e->receivedMs) && e->edge.groupId == g.coreGroupId)
                ++reporters;
            // T30-1d: the base of follow_other_map - every acked follower (silent or bg_wait too) for the time since the last pass.
            if (m.state == MemberState::Acked && m.low != g.leaderLow)
                Count(Cnt::FollowQuestMs, passDeltaMs_);
            if (m.state != MemberState::Acked || m.silent || m.bgWait)
                continue;
            ++members;
            manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low);
            if (!s)
                continue;
            manager_inbox::KeyFacts const k = manager_inbox::UnpackKey(s->key);
            TrackProgress(g, m, k);
            // T30-1d: an episode opens or closes only on fresh snapshots of the follower and its leader (no old map after a
            // teleport); without them it stays as it is.
            if (m.low != g.leaderLow && ls && Fresh(ls->receivedMs) && Fresh(s->receivedMs))
                OtherMapEpisode(g, m, ls->mapId != s->mapId, ls->receivedMs, s->receivedMs, io);
            bool const sameKey = k.questId == g.questId;
            if (sameKey && k.questStatus == manager_inbox::kQuestRewarded)
                m.turnedIn = true;
            if (!sameKey || k.questStatus != manager_inbox::kQuestRewarded)
                allTurnedIn = false;
            if (!sameKey || (k.questStatus != manager_inbox::kQuestComplete && k.questStatus != manager_inbox::kQuestRewarded))
                allComplete = false;
            if (m.low == g.leaderLow)
                f.abandoned = sameKey && k.questStatus == manager_inbox::kQuestNone;
            else if (!ls || ls->mapId != s->mapId || Dist2d(ls->x, ls->y, s->x, s->y) > kGatherYards * kGatherYards)
                gathered = false;
        }
        f.members = members;
        f.allComplete = members > 0 && allComplete;
        f.allTurnedIn = members > 0 && allTurnedIn;
        f.gathered = gathered;
        f.lastProgress = g.lastProgressSec;
        // m6: fewer bots report the core group than its size = a real player (or a bot outside the manager) is in it.
        bool const fewer = le && Fresh(le->receivedMs) && le->edge.groupId == g.coreGroupId && reporters < le->edge.members;
        g.realPlayerSinceMs = fewer ? (g.realPlayerSinceMs ? g.realPlayerSinceMs : manager_inbox::PostStamp(now_)) : 0;
        f.realPlayer = g.realPlayerSinceMs && now_ - g.realPlayerSinceMs >= kRealPlayerGraceMs;
        quest_groups::Step const s = quest_groups::Decide(f, r.timers);
        if (s.dissolve != quest_groups::Dissolve::None)
        {
            EndGroup(g, FromDissolve(s.dissolve), io);
            return;
        }
        if (s.next != g.phase)
        {
            g.phase = s.next;
            g.phaseSinceSec = nowSec_;
        }
    }

    // Orphans (M2) and foreign groups of owned bots (F4 (a)), from the fresh edges.
    void Strays(PassInput const& in, Rules const& r, Io& io)
    {
        if (!in.edges || (!r.types && groups_.empty() && bots_.empty()))
        {
            skipUnknown_.clear();   // nobody is judged, so nobody stays skip_unknown
            return;
        }
        cores_.clear();
        for (auto const& entry : groups_)
            if (entry.second.coreGroupId)
                cores_[entry.second.coreGroupId] = entry.first;
        reporters_.clear();
        for (auto const& entry : *in.edges)
            if (entry.second.edge.groupId && Fresh(entry.second.receivedMs))
                ++reporters_[entry.second.edge.groupId];
        for (auto const& entry : *in.edges)
        {
            std::uint32_t const low = entry.first;
            manager_inbox::EdgeFacts const& e = entry.second.edge;
            if (!e.groupId || !Fresh(entry.second.receivedMs) || e.bgGroup)
                continue;
            BotEntry* b = FindBot(low);
            // A leave just took the bot out of this group: the edge predates it (2.3 Ende, confirmed by the next edge).
            if (b && b->outGroupId == e.groupId)
                continue;
            // bg_wait (M2): a bot held in its old group while it waits in a BG queue gets no op; the BG path leaves the group.
            if (b && b->staleGroupId == e.groupId && InBgQueue(in, low))
                continue;
            auto const core = cores_.find(e.groupId);
            if (core != cores_.end())
            {
                Member* m = nullptr;
                ManagedGroup* g = GroupOf(low, &m);
                if (g && g->serial == core->second)
                    continue;   // a member of that group
                if (QueueStrayLeave(low, e.groupId, e.leaderLow, EndReason::Orphan))
                    Count(Cnt::Orphan);
                continue;
            }
            if (b)
            {
                if (b->staleGroupId == e.groupId && QueueStrayLeave(low, e.groupId, e.leaderLow, EndReason::Orphan))
                    Count(Cnt::Orphan);
                continue;
            }
            if (!Owns(r.mode, r.types, low))
                continue;
            auto const rep = reporters_.find(e.groupId);
            std::uint32_t const n = rep == reporters_.end() ? 0u : rep->second;
            if (io.S7Owns(low))
            {
                Count(Cnt::SkipS7);
                ForeignLine(io, e, low, n, "skip_s7", true);
                continue;
            }
            if (n < e.members)
            {
                Count(Cnt::SkipUnknown);   // a real player, a bot of the other cohort or with a real master: not ours
                ForeignLine(io, e, low, n, "skip_unknown", true);
                // OB-10 smoke note (T30-1b): the first second of an unbroken skip_unknown run (minute2 skip_unknown_30m).
                // try_emplace: no node is built when the bot is already in its run (allocation-light per pass).
                auto const run = skipUnknown_.try_emplace(low);
                if (run.second)
                    run.first->second.sinceSec = nowSec_;
                run.first->second.pass = passNo_;
                continue;
            }
            if (QueueStrayLeave(low, e.groupId, e.leaderLow, EndReason::ForeignGroup))
            {
                Count(Cnt::Foreign);
                ForeignLine(io, e, low, n, "dissolve", false);
            }
        }
        // A bot not skip_unknown in this pass is out of its run (another group, ungrouped, managed, stale, gone).
        for (auto it = skipUnknown_.begin(); it != skipUnknown_.end();)
            it = it->second.pass == passNo_ ? std::next(it) : skipUnknown_.erase(it);
    }

    bool InBgQueue(PassInput const& in, std::uint32_t low) const
    {
        manager_inbox::StatusSnapshot const* s = FindStatus(in.status, low);
        return s && Fresh(s->receivedMs) && manager_inbox::UnpackFlags(s->flags).inBgQueue;
    }

    // 5.2 "event=foreign ... action=": a skip repeats every pass while the bot stays in that group, so its line goes out once
    // per bot and kSkipLineMs (the counters keep every pass).
    void ForeignLine(Io& io, manager_inbox::EdgeFacts const& e, std::uint32_t low, std::uint32_t reporters, char const* action, bool skip)
    {
        if (skip)
        {
            auto const last = skipLineMs_.find(low);
            if (last != skipLineMs_.end() && now_ - last->second < kSkipLineMs)
                return;
            skipLineMs_[low] = now_;
        }
        char line[192];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=foreign core=%u bot=%u members=%u reporters=%u action=%s",
            e.groupId, low, e.members, reporters, action);
        Emit(io, line);
    }

    // One stray leave per bot per kLeaveConfirmMs; it waits for the op budget.
    bool QueueStrayLeave(std::uint32_t low, std::uint32_t groupId, std::uint32_t leaderLow, EndReason why)
    {
        auto const last = strayMs_.find(low);
        if (last != strayMs_.end() && now_ - last->second < kLeaveConfirmMs)
            return false;
        for (StrayLeave const& s : strays_)
            if (s.low == low)
                return false;
        strayMs_[low] = manager_inbox::PostStamp(now_);
        StrayLeave s;
        s.low = low;
        s.groupId = groupId;
        s.leaderLow = leaderLow;
        s.reason = why;
        strays_.push_back(s);
        ++minute_.leave[std::size_t(why)];
        ++totalLeave_[std::size_t(why)];
        return true;
    }

    // ---- (7) forming (2.5) ----
    struct Cand
    {
        std::uint32_t low = 0;
        manager_inbox::StatusSnapshot const* s = nullptr;
        manager_inbox::WishSnapshot const* w = nullptr;
        bool alliance = false;
        bool zoneHold = false;   // held in its start zone (R8): its own position is the only safe ADHOC anchor
    };

    void Form(PassInput const& in, Rules const& r, Io& io)
    {
        stands_ = Stands();
        stands_.types = r.types;
        if (!r.types || !in.status)
            return;
        adhocCands_.clear();
        questCands_.clear();
        for (auto const& entry : *in.status)
        {
            std::uint32_t const low = entry.first;
            if (!Owns(r.mode, r.types, low))
                continue;
            ++stands_.owned;
            if (bots_.count(low) || io.S7Owns(low))
                continue;
            manager_inbox::WishSnapshot const* w = FindWish(in.wishes, low);
            if (w && !FreshWish(w->receivedMs))
                w = nullptr;   // a leftover wish of an older period (a Status lost to a full ring) is no wish
            bool blocked = false;
            if (w && TypeOn(r.types, GroupType::Quest) && w->wish.questId && Fresh(w->receivedMs))
            {
                ++minute_.wishes;
                questQueue_.Seen(low, w->wish.questId, nowSec_);
                queueUsed_.store(true, std::memory_order_release);
                blocked = QuestBlocked(low, w->wish.questId, nowSec_, r.maxAttempts);
            }
            CandidateFacts const c = Candidate(entry.second, FindEdge(in.edges, low), w, io.SupervisorApplies(low), blocked, now_, staleMs_);
            minute_.staleSnap += c.stale ? 1 : 0;
            minute_.candBusy += c.busy ? 1 : 0;
            minute_.noSupervisor += c.noSupervisor ? 1 : 0;
            minute_.candZoneHold += c.zoneHold ? 1 : 0;
            Cand cand;
            cand.low = low;
            cand.s = &entry.second;
            cand.w = w;
            manager_inbox::StatusFlags const flags = manager_inbox::UnpackFlags(entry.second.flags);
            cand.alliance = flags.alliance;
            cand.zoneHold = flags.startZoneHold;
            if (c.adhoc && TypeOn(r.types, GroupType::Adhoc))
            {
                ++minute_.candAdhoc;
                adhocCands_.push_back(cand);
            }
            if (c.quest)
            {
                ++minute_.candQuest;
                questCands_.push_back(cand);
            }
        }
        if (now_ - lastQueueSweepMs_ >= 60000)
        {
            lastQueueSweepMs_ = now_;
            questQueue_.ForgetStale(nowSec_, 600);
            for (auto it = questCooldown_.begin(); it != questCooldown_.end();)
                it = it->second <= nowSec_ ? questCooldown_.erase(it) : std::next(it);
        }
        if (Inflight() >= kMaxManagerOpsInFlight)
            return;
        // One new group or one growth per pass: the op budget is one op per pass anyway.
        if (FormQuest(r, io) || GrowAdhoc(in, r, io))
            return;
        FormAdhoc(r, io);
    }

    std::uint32_t Inflight() const
    {
        std::uint32_t n = 0;
        for (auto const& entry : groups_)
            for (Member const& m : entry.second.m)
                n += (m.opQueued ? 1u : 0u) + (m.leaveQueued ? 1u : 0u);
        return n;
    }

    // A non-leader of the group still has a leave to go (wanted, queued, or behind its queued join).
    static bool OthersLeaving(ManagedGroup const& g)
    {
        for (Member const& o : g.m)
            if (o.low != g.leaderLow && o.state == MemberState::Leaving && (o.leaveWanted || o.leaveQueued || o.opQueued))
                return true;
        return false;
    }

    static bool JoinQueued(ManagedGroup const& g)
    {
        for (Member const& o : g.m)
            if (!o.inviter && o.state != MemberState::Left && o.opQueued)
                return true;
        return false;
    }

    Member NewMember(std::uint32_t low, bool inviter)
    {
        Member m;
        m.low = low;
        m.inviter = inviter;
        m.opWanted = !inviter;
        m.addedMs = now_;
        m.lastProgressSec = nowSec_;
        return m;
    }

    ManagedGroup& NewGroup(GroupType type, std::uint32_t questId, std::int8_t objective, Cand const& leader)
    {
        std::uint32_t const serial = NextSerial();
        ManagedGroup& g = groups_[serial];
        g.serial = serial;
        g.type = type;
        g.questId = questId;
        g.objective = objective;
        g.leaderLow = leader.low;
        g.mapId = leader.s->mapId;
        g.alliance = leader.alliance;
        g.anchorX = leader.s->x;
        g.anchorY = leader.s->y;
        g.createdSec = nowSec_;
        g.lastProgressSec = nowSec_;
        g.m.push_back(NewMember(leader.low, true));
        bots_[leader.low].serial = serial;
        return g;
    }

    void AddMember(ManagedGroup& g, std::uint32_t low)
    {
        g.m.push_back(NewMember(low, false));
        bots_[low].serial = g.serial;
        g.sizeMax = std::max<std::uint32_t>(g.sizeMax, std::uint32_t(g.m.size()));
    }

    bool FormQuest(Rules const& r, Io& io)
    {
        if (questCands_.empty())
            return false;
        // Buckets: map, team, quest (S7).
        std::unordered_map<std::uint64_t, std::vector<quest_groups::Candidate>> buckets;
        std::unordered_map<std::uint64_t, bool> dropOnly;
        std::unordered_map<std::uint32_t, Cand const*> byLow;
        for (Cand const& c : questCands_)
        {
            std::uint64_t const key = (std::uint64_t(c.s->mapId) << 40) ^ (std::uint64_t(c.alliance ? 1 : 0) << 39) ^ c.w->wish.questId;
            quest_groups::Candidate qc;
            qc.guid = c.low;
            qc.level = c.s->level;
            qc.guild = c.w->wish.guildId;
            qc.openObjectives = c.w->wish.openObjectives;
            qc.x = c.s->x;
            qc.y = c.s->y;
            buckets[key].push_back(qc);
            if (c.w->wish.dropOnly)
                dropOnly[key] = true;
            byLow[c.low] = &c;
        }
        for (auto& bucket : buckets)
        {
            if (bucket.second.size() < 2)
                continue;
            quest_groups::FormLimits lim;
            bool const drop = dropOnly.count(bucket.first) != 0;
            lim.maxSize = drop ? r.questMaxSizeDrop : r.questMaxSize;
            lim.levelWindow = r.questLevelWindow;
            lim.recruitRadius = r.recruitRadius;
            std::vector<std::vector<quest_groups::Candidate>> const formed = quest_groups::Form(bucket.second, lim);
            if (formed.empty())
                continue;
            std::vector<quest_groups::Candidate> const& members = formed.front();
            Cand const& leader = *byLow[members.front().guid];
            manager_inbox::KeyFacts const lk = manager_inbox::UnpackKey(leader.s->key);
            std::int8_t const objective = lk.questId == leader.w->wish.questId ? lk.objective : std::int8_t(-1);
            ManagedGroup& g = NewGroup(GroupType::Quest, leader.w->wish.questId, objective, leader);
            g.dropOnly = drop;
            for (std::size_t i = 1; i < members.size(); ++i)
                AddMember(g, members[i].guid);
            (void)io;
            return true;
        }
        return false;
    }

    bool GrowAdhoc(PassInput const& in, Rules const& r, Io& io)
    {
        if (adhocCands_.empty())
            return false;
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            if (g.type != GroupType::Adhoc || g.state != GroupState::Active || g.joinInFlight || !g.coreGroupId)
                continue;
            std::uint32_t size = 0;
            std::uint32_t lo = 0xFFFFFFFFu;
            std::uint32_t hi = 0;
            for (Member const& m : g.m)
            {
                if (m.state == MemberState::Leaving || m.state == MemberState::Left)
                    continue;
                ++size;
                if (manager_inbox::StatusSnapshot const* s = FindStatus(in.status, m.low))
                {
                    lo = std::min<std::uint32_t>(lo, s->level);
                    hi = std::max<std::uint32_t>(hi, s->level);
                }
            }
            if (size >= std::max<std::uint32_t>(2, std::min<std::uint32_t>(5, r.maxBots)))
                continue;
            manager_inbox::StatusSnapshot const* ls = FindStatus(in.status, g.leaderLow);
            if (!ls || !Fresh(ls->receivedMs))
                continue;
            for (Cand const& c : adhocCands_)
            {
                // R8: the group's anchor is the leader's position, maybe outside a held bot's start zone (ack reason 16).
                if (c.zoneHold)
                    continue;
                manager_inbox::KeyFacts const k = manager_inbox::UnpackKey(c.s->key);
                if (k.questId != g.questId || k.objective != g.objective || c.s->mapId != ls->mapId || c.alliance != g.alliance)
                    continue;
                if (Dist2d(c.s->x, c.s->y, ls->x, ls->y) > r.adhocRadius * r.adhocRadius)
                    continue;
                if (!adhoc_group::LevelWindowOk(std::min<std::uint32_t>(lo, c.s->level), std::max<std::uint32_t>(hi, c.s->level), r.levelWindow))
                    continue;
                if (pairCooldown_.IsBlocked(g.leaderLow, c.low, nowSec_))
                    continue;
                pairCooldown_.Block(g.leaderLow, c.low, nowSec_ + r.pairCooldownSeconds, nowSec_);
                AddMember(g, c.low);
                (void)io;
                return true;
            }
        }
        return false;
    }

    bool FormAdhoc(Rules const& r, Io& io)
    {
        std::sort(adhocCands_.begin(), adhocCands_.end(), [](Cand const& a, Cand const& b) { return a.low < b.low; });
        for (std::size_t i = 0; i < adhocCands_.size(); ++i)
        {
            Cand const& a = adhocCands_[i];
            manager_inbox::KeyFacts const ka = manager_inbox::UnpackKey(a.s->key);
            for (std::size_t j = i + 1; j < adhocCands_.size(); ++j)
            {
                Cand const& b = adhocCands_[j];
                manager_inbox::KeyFacts const kb = manager_inbox::UnpackKey(b.s->key);
                // The bucket of AdhocGroupWorld: map, team, quest, objective.
                if (ka.questId != kb.questId || ka.objective != kb.objective || a.s->mapId != b.s->mapId || a.alliance != b.alliance)
                    continue;
                if (Dist2d(a.s->x, a.s->y, b.s->x, b.s->y) > r.adhocRadius * r.adhocRadius)
                    continue;
                if (!adhoc_group::LevelWindowOk(std::min<std::uint32_t>(a.s->level, b.s->level),
                        std::max<std::uint32_t>(a.s->level, b.s->level), r.levelWindow))
                    continue;
                if (pairCooldown_.IsBlocked(a.low, b.low, nowSec_))
                    continue;
                // R8: a bot held in its start zone invites, so the anchor (the inviter's position) lies in its zone; the
                // other bot is not held. Both held: the supervisor's ack reason 16 decides.
                bool const aInvites = a.zoneHold != b.zoneHold ? a.zoneHold :
                    adhoc_group::Decide(a.low, false, false, b.low, false, false) == adhoc_group::Invite::AInvitesB;
                Cand const& inviter = aInvites ? a : b;
                Cand const& invitee = aInvites ? b : a;
                pairCooldown_.Block(a.low, b.low, nowSec_ + r.pairCooldownSeconds, nowSec_);
                ManagedGroup& g = NewGroup(GroupType::Adhoc, ka.questId, ka.objective, inviter);
                AddMember(g, invitee.low);
                (void)io;
                return true;
            }
        }
        return false;
    }

    // ---- (8) the op budget: leaves first, then joins; at most kOpsPerPass new ops ----
    void RunOps(PassInput const& in, Io& io)
    {
        std::uint32_t budget = kOpsPerPass;
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            for (Member& m : g.m)
            {
                if (!budget)
                    return;
                if (m.state != MemberState::Leaving || !m.leaveWanted || m.leaveQueued || m.opQueued)
                    continue;
                // 2.3 Ende: non-leaders first, so the leader's leave comes last and the core group never changes its leader.
                if (m.low == g.leaderLow && OthersLeaving(g))
                    continue;
                // A queued join of an invitee may still put the inviter into a new core group: wait for its outcome.
                if (m.inviter && !g.coreGroupId && JoinQueued(g))
                    continue;
                std::uint32_t groupId = g.coreGroupId;
                manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, m.low);
                if (!groupId && e && e->edge.groupId && e->edge.leaderLow == g.leaderLow)
                    groupId = e->edge.groupId;
                if (!groupId && m.opExecMs)
                {
                    // The join ran (it may have made the group) but no edge shows it yet: wait for it, so no bot is left in a
                    // core group nobody owns. After kJoinConfirmMs the leader's edge names the group, if it has one.
                    if (now_ - m.opExecMs <= kJoinConfirmMs)
                        continue;
                    manager_inbox::EdgeSnapshot const* le = FindEdge(in.edges, g.leaderLow);
                    if (le && Fresh(le->receivedMs) && le->edge.groupId && le->edge.leaderLow == g.leaderLow)
                        groupId = le->edge.groupId;
                }
                // An edge from before the join's Execute still shows the old state: only a later one says "not in the group".
                bool const edgeCurrent = e && Fresh(e->receivedMs) && (!m.opExecMs || After(e->receivedMs, m.opExecMs));
                if (!groupId || (edgeCurrent && e->edge.groupId != groupId))
                {
                    // Not in the group (never was, or out already): nothing to leave.
                    MarkLeft(g, m, io);
                    continue;
                }
                if (io.QueueLeave(m.low, groupId, g.leaderLow))
                {
                    Count(Cnt::OpLeaveQ);
                    m.leaveWanted = false;
                    m.leaveQueued = true;
                    m.leaveMs = manager_inbox::PostStamp(now_);
                    ++m.leaveTries;
                    --budget;
                    OpLine(io, "leave", g, m.low, g.leaderLow, "queued", m.reason);
                }
                else
                {
                    Count(Cnt::OpLeaveRefused);   // m4: again in the next pass, within the budget
                    OpLine(io, "leave", g, m.low, g.leaderLow, "refused", m.reason);
                }
            }
        }
        for (auto it = strays_.begin(); it != strays_.end() && budget;)
        {
            if (io.QueueLeave(it->low, it->groupId, it->leaderLow))
            {
                Count(Cnt::OpLeaveQ);
                --budget;
            }
            else
                Count(Cnt::OpLeaveRefused);
            it = strays_.erase(it);
        }
        if (!budget || Inflight() >= kMaxManagerOpsInFlight)
            return;
        for (auto& entry : groups_)
        {
            ManagedGroup& g = entry.second;
            if (g.state == GroupState::Ending || g.joinInFlight)
                continue;
            for (Member& m : g.m)
            {
                if (!m.opWanted || m.state != MemberState::Joining)
                    continue;
                // QUEST join n+1 only with the confirmed core id (M8); the first join of a new group carries 0.
                bool const first = !g.coreGroupId;
                // A join still queued, or executed for a member that is still in (not ending): the core id comes from it. A
                // member that ended after its join ran (join_failed, ack_*) does not hold the next invitee back.
                bool anyExec = false;
                for (Member const& o : g.m)
                    anyExec = anyExec || (!o.inviter && o.state != MemberState::Left &&
                        (o.opQueued || (o.opExecMs && o.state != MemberState::Leaving)));
                if (first && anyExec)
                    break;
                std::uint8_t const objective = g.objective >= 0 ? std::uint8_t(g.objective) : 0;
                if (io.QueueJoin(m.low, g.leaderLow, g.coreGroupId, g.type, g.questId, objective))
                {
                    Count(Cnt::OpJoinQ);
                    m.opWanted = false;
                    m.opQueued = true;
                    g.joinInFlight = true;
                    OpLine(io, "join", g, m.low, g.leaderLow, "queued", EndReason::None);
                    return;
                }
                Count(Cnt::OpJoinRefused);
                OpLine(io, "join", g, m.low, g.leaderLow, "refused", EndReason::OpRefused);
                EndMember(g, m, EndReason::OpRefused, io, false);
                return;
            }
        }
    }

    // ---- sweep: closed groups out, finished entries out, the view published ----
    void Sweep(PassInput const& in)
    {
        for (auto it = groups_.begin(); it != groups_.end();)
        {
            ManagedGroup& g = it->second;
            bool open = false;
            for (Member const& m : g.m)
                open = open || m.state != MemberState::Left;
            if (open)
            {
                ++it;
                continue;
            }
            for (Member const& m : g.m)
            {
                auto const b = bots_.find(m.low);
                if (b != bots_.end() && b->second.serial == g.serial)
                    b->second.serial = 0;
            }
            Count(Cnt::Dissolved);
            pendingDissolved_.push_back(g);
            swept_.push_back(SweptGroup{g.serial, g.endReason});
            it = groups_.erase(it);
        }
        for (auto it = bots_.begin(); it != bots_.end();)
        {
            BotEntry& b = it->second;
            if (b.staleGroupId)
            {
                manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, it->first);
                if (e && Fresh(e->receivedMs) && e->edge.groupId != b.staleGroupId)
                    b.staleGroupId = 0;
            }
            if (b.outGroupId)
            {
                manager_inbox::EdgeSnapshot const* e = FindEdge(in.edges, it->first);
                if ((e && Fresh(e->receivedMs) && e->edge.groupId != b.outGroupId) || now_ - b.outMs > kLeaveConfirmMs)
                    b.outGroupId = 0;
            }
            bool const held = b.serial || b.exitOpen || b.exitDue || b.blockedUntilSec > nowSec_ || b.staleGroupId || b.outGroupId;
            it = held ? std::next(it) : bots_.erase(it);
        }
        Publish();
    }

    void Publish()
    {
        viewBots_.clear();
        viewCores_.clear();
        for (auto const& entry : bots_)
            viewBots_[entry.first] = ViewEntry();
        for (auto const& entry : groups_)
        {
            ManagedGroup const& g = entry.second;
            ViewEntry v;
            v.coreGroupId = g.coreGroupId;
            v.questId = g.questId;
            // 3.1: a QUEST member reports the quest's progress over every objective (ReadKey sums them only for -1); the
            // directive keeps g.objective in b6-8.
            v.objective = g.type == GroupType::Quest ? std::int8_t(-1) : g.objective;
            v.type = g.type;
            if (g.coreGroupId)
                viewCores_[g.coreGroupId] = v;
            for (Member const& m : g.m)
            {
                if (m.state == MemberState::Left)
                    continue;
                ViewEntry mv = v;
                bool const inGroup = m.state == MemberState::Joined || m.state == MemberState::Directed || m.state == MemberState::Acked;
                if (g.type == GroupType::Quest && inGroup)
                    mv.questRole = m.low == g.leaderLow ? 2 : 1;
                // Live = still wanted in the group; an ending member (Exit sent) answers IsManaged(guid, serial) false.
                if (m.state != MemberState::Leaving)
                    mv.serial = std::uint16_t(g.serial);
                viewBots_[m.low] = mv;
            }
        }
        view_.Publish(viewBots_, viewCores_);
    }

    // ---- lines ----
    void OpLine(Io& io, char const* kind, ManagedGroup const& g, std::uint32_t bot, std::uint32_t other, char const* result, EndReason why)
    {
        char line[256];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=op kind=%s group=%u core=%u bot=%u other=%u type=%s result=%s reason=%s",
            kind, g.serial, g.coreGroupId, bot, other, TypeName(g.type), result, EndReasonName(why));
        Emit(io, line);
    }

    void DirectiveLine(Io& io, ManagedGroup const& g, std::uint32_t bot, std::uint32_t seq, DirectiveBits const& d, bool ok)
    {
        char line[256];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=directive group=%u bot=%u seq=%u verb=%s type=%s quest=%u objective=%d result=%s",
            g.serial, bot, seq, VerbName(d.verb), TypeName(g.type), g.questId, int(d.objective), ok ? "posted" : "dropped");
        Emit(io, line);
    }

    void AckLine(Io& io, std::uint32_t serial, Ack const& a, Verb verb, std::uint32_t ms)
    {
        char line[256];
        std::snprintf(line, sizeof(line), "[GroupMgr] event=ack group=%u bot=%u seq=%u verb=%s state=%s deny=%u reason=%s ms=%u",
            serial, a.low, a.seq, VerbName(verb), AckStateName(a.state), std::uint32_t(a.reason), AckReasonName(a.reason), ms);
        Emit(io, line);
    }

    void MinuteLine(PassInput const& in, Rules const& r, Io& io)
    {
        // Dissolved lines of this pass (after Sweep).
        for (ManagedGroup const& g : pendingDissolved_)
        {
            char line[256];
            std::snprintf(line, sizeof(line), "[GroupMgr] event=dissolved group=%u core=%u type=%s reason=%s minutes=%u size_max=%u phase=%s",
                g.serial, g.coreGroupId, TypeName(g.type), EndReasonName(g.endReason), g.formedSec ? (nowSec_ - g.formedSec) / 60 : 0u,
                g.sizeMax, g.type == GroupType::Quest ? quest_groups::PhaseName(g.phase) : "-");
            Emit(io, line);
        }
        pendingDissolved_.clear();
        if (!minuteStarted_)
        {
            minuteStarted_ = true;
            minuteStartMs_ = in.nowMs;
            keyOverflowSeen_ = in.keyOverflow;
            return;
        }
        // T30-1e (CLI-Projekt, R9 P5): the pass in which the registry empties with Types 0 (switch_off, the disarm) prints the
        // minute lines once with the increments so far. From the next pass on RunDirectives returns early (no Types, nothing
        // held), so that minute's switch_off leaves, acks, left acks and exits would never be printed.
        bool const finalFlush = !r.types && heldAtPassStart_ && !HasGroups();
        if (!finalFlush && in.nowMs - minuteStartMs_ < 60000)
            return;
        minuteStartMs_ = in.nowMs;
        linesThisMinute_ = 0;
        minute_.keyOverflow = manager_inbox::TakeDelta(in.keyOverflow, keyOverflowSeen_);
        // The per-bot throttles keep only what can still throttle.
        for (auto it = strayMs_.begin(); it != strayMs_.end();)
            it = now_ - it->second >= kLeaveConfirmMs ? strayMs_.erase(it) : std::next(it);
        for (auto it = skipLineMs_.begin(); it != skipLineMs_.end();)
            it = now_ - it->second >= kSkipLineMs ? skipLineMs_.erase(it) : std::next(it);
        // 5.3: only while Types != 0 or the registry holds anything, and once more in the pass that empties it (T30-1e).
        if (r.types || HasGroups() || finalFlush)
        {
            stands_.groups = groups_.size();
            for (auto const& entry : groups_)
                for (Member const& m : entry.second.m)
                {
                    if (m.state == MemberState::Left)
                        continue;
                    ++stands_.members;
                    stands_.silent += m.silent ? 1 : 0;
                    if (m.otherMapSinceMs)
                    {
                        ++minute_.otherMapOpen;
                        minute_.otherMapOpenMaxMs = std::max<std::uint64_t>(minute_.otherMapOpenMaxMs,
                            After(now_, m.otherMapSinceMs) ? now_ - m.otherMapSinceMs : 0u);
                    }
                }
            for (auto const& entry : skipUnknown_)
                minute_.skipUnknown30m += nowSec_ - entry.second.sinceSec >= kSkipUnknownLongSec ? 1 : 0;
            char buf[2048];
            FormatMinute(buf, sizeof(buf), minute_, stands_);
            io.Line(buf);
            FormatMinute2(buf, sizeof(buf), minute_);
            io.Line(buf);
        }
        minute_ = MinuteCounts();
        if (finalFlush)
            minuteStarted_ = false;   // T30-1e: a later start counts its first minute from its own first pass
    }

    struct StrayLeave
    {
        std::uint32_t low = 0;
        std::uint32_t groupId = 0;
        std::uint32_t leaderLow = 0;
        EndReason reason = EndReason::None;
    };

    std::unordered_map<std::uint32_t, ManagedGroup> groups_;
    std::unordered_map<std::uint32_t, BotEntry> bots_;
    std::vector<StrayLeave> strays_;
    std::unordered_map<std::uint32_t, std::uint32_t> strayMs_;
    std::unordered_map<std::uint32_t, std::uint32_t> skipLineMs_;
    // T30-1b: the bots skip_unknown in the last Strays pass and since when (an entry only while it lasts; a skip_unknown bot is
    // not in bots_, so there is no BotEntry to reuse).
    struct SkipSince
    {
        std::uint32_t sinceSec = 0;
        std::uint64_t pass = 0;
    };
    std::unordered_map<std::uint32_t, SkipSince> skipUnknown_;
    std::vector<ManagedGroup> pendingDissolved_;
    std::vector<SweptGroup> swept_;   // T33-0: this pass's swept groups (Swept)
    adhoc_group::PairCooldownStore pairCooldown_;
    quest_groups::QueueStore questQueue_;
    std::atomic<bool> queueUsed_{ false };
    std::unordered_map<std::uint64_t, std::uint32_t> questCooldown_;   // F13: the manager's own reasons, cooldown only
    std::uint32_t retryCooldownSec_ = 30 * 60;
    std::vector<Cand> adhocCands_;
    std::vector<Cand> questCands_;
    std::unordered_map<std::uint32_t, std::uint32_t> cores_;
    std::unordered_map<std::uint32_t, std::uint32_t> reporters_;
    ManagedView view_;
    std::unordered_map<std::uint32_t, ViewEntry> viewBots_;
    std::unordered_map<std::uint32_t, ViewEntry> viewCores_;
    std::uint32_t nextSeq_ = 0;      // only NextSeq moves it (never reset, never 0 once used)
    std::uint32_t nextSerial_ = 0;   // only NextSerial moves it (never reset)
    std::uint32_t now_ = 0;
    std::uint32_t nowSec_ = 0;
    std::uint32_t staleMs_ = manager_inbox::kStaleMinMs;
    std::uint32_t wishMs_ = 0;       // WishFreshMs: a wish counts only from the latest status period (OB-10 note on #984)
    std::uint32_t lastPassMs_ = 0;
    std::uint32_t passDeltaMs_ = 0;   // T30-1d: now_ - lastPassMs_ of this pass (0 on the first)
    struct GoneEpisode
    {
        std::uint32_t serial = 0;
        std::uint32_t low = 0;
        std::uint32_t ms = 0;
    };
    std::vector<GoneEpisode> goneEpisodes_;   // T30-1d: closed by ForgetGone (no Io there), their lines at the next pass
    bool passed_ = false;
    std::uint64_t passNo_ = 0;
    std::uint32_t lastQueueSweepMs_ = 0;
    bool minuteStarted_ = false;
    bool heldAtPassStart_ = false;   // T30-1e: HasGroups() at the start of this pass
    std::uint32_t minuteStartMs_ = 0;
    std::uint32_t linesThisMinute_ = 0;
    std::uint64_t keyOverflowSeen_ = 0;
    std::uint32_t gapShiftMs_ = 0;   // M15 stale anchor: the credit for snapshots received before gapAtMs_ (Fresh)
    std::uint32_t gapAtMs_ = 0;
    MinuteCounts minute_;
    std::uint64_t total_[kCounts] = {};
    std::uint64_t totalLeave_[kEndReasons] = {};
    Stands stands_;
};
}
