#pragma once

#include "GroupManagerPolicy.h"
#include "ManagerInboxPolicy.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// A3 step 6 T33-0 "Fakten + Schatten" (design ws-10/cli-grp/a3-schritt6/DESIGN-T33-TREFFPUNKT-20261109.md v1.1 + Nachtrag 1;
// owner decisions F1-F17 = (a), O6 (a): no hidden hop, no teleport; AiPlayerbot.BotGroups.Manager.Gather, default 0).
// MEASUREMENT ONLY. The pure facts and rules of the meeting point: the bot's GatherPlace post (3.2), the place the manager
// judges (2.4), the meeting point at the leader's last place (F1 (a), F7 (a)), the arrival verdict of a group (leader not
// judged, like S7) and the manager's one clock (2.5). No game type, no config read, no lock; the game rule values (radius,
// z band, the dead rule) are parameters. GroupGatherShadow.h runs them next to the T30 rule; nothing here moves a bot,
// posts a directive or ends a group (T33-1).
namespace ai::group_mgr
{
// ---- the switch (5.1) ------------------------------------------------------------------------------------------------------
// AiPlayerbot.BotGroups.Manager.Gather: 0 off (default), 1 all, 2 cohort B, 3 cohort A by the LEADER's low guid (F8 (a)).
// T33-0: any value != 0 turns the GatherPlace posts and the shadow on for every manager QUEST group; the cohort only labels the
// group's arm (gather= in event=phase) for the A/B of T33-1. Any other value is 0 (one error line); it needs
// BotGroups.Manager.Types != 0 (else 0, one error line) - without manager groups there is nothing to measure.
inline bool GatherValid(std::uint32_t raw)
{
    return raw <= cohort::ModeOnlyA;
}

inline std::uint32_t EffectiveGather(std::uint32_t gather, std::uint32_t types)
{
    return types && GatherValid(gather) ? gather : 0u;
}

// The arm of a group (F8 (a)): the lever would apply by its leader's guid.
inline bool GatherArm(std::uint32_t gather, std::uint32_t leaderLow)
{
    return GatherValid(gather) && cohort::Applies(gather, leaderLow);
}

// ---- T33-1: the lever (5.1; OB-00 13.11.2026 (a): its own key) -------------------------------------------------------------
// AiPlayerbot.BotGroups.Manager.Gather.Lever: 0 off (default), 1 on. The lever acts ONLY with Lever = 1: then a manager QUEST
// group whose leader is in the Gather arm (GatherArm, F8 (a)) gets the meeting (GroupDirectivePolicy.h Engine, design 2.3).
// Gather stays the measurement switch and the arm cohort: Gather != 0 with Lever 0 is the T33-0 shadow alone, everything else
// as T30. Any other value is 0 (one error line); it needs Gather != 0 (else 0, one error line).
inline bool GatherLeverValid(std::uint32_t raw)
{
    return raw <= 1u;
}

inline std::uint32_t EffectiveGatherLever(std::uint32_t lever, std::uint32_t gather)
{
    return gather && lever == 1u ? 1u : 0u;
}

inline bool GatherApplies(std::uint32_t gather, std::uint32_t lever, GroupType type, std::uint32_t leaderLow)
{
    return EffectiveGatherLever(lever, gather) && type == GroupType::Quest && GatherArm(gather, leaderLow);
}

// F1 (a): the Enter of a meeting group carries b15 = 1 and kind a (follow the leader, like S7); the manager never packs
// another kind in v1 (kind b / c and kind 3 never, design v1.3 Nachtrag 2).
constexpr std::uint8_t kGatherKindFollow = 0;

// ---- the bot's place (3.2; Kind::GatherPlace) --------------------------------------------------------------------------------
// Event layout: level b0 = the map is instanceable (b1-7 = 0; b4/b5 reserved for the hearthstone appendix), mapOrDungeon = map
// id, seq = the raw instance id of the bot's map (continent regions included), hpPct10 = the group serial's low 16 bits of the
// bot's own last directive (0 = none; T33-0 posts 0: the bot keeps no directive record before the T02-GROUP handler),
// x / y = position, reserved = z as float bits. role, reason and manaPct10 stay 0. alive / busy come from the Status (2.4).
constexpr std::uint8_t kPlaceInstanceable = 0x01;

struct PlaceFacts
{
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;   // raw GetInstanceId()
    bool instanceable = false;      // GetMap()->Instanceable()
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint16_t serial = 0;
};

static_assert(sizeof(float) == sizeof(std::uint32_t), "z rides in the 32-bit reserved field");

inline manager_inbox::Event MakeGatherPlace(std::uint32_t botLow, PlaceFacts const& f)
{
    manager_inbox::Event e{};
    e.botLow = botLow;
    e.kind = manager_inbox::Kind::GatherPlace;
    e.level = f.instanceable ? kPlaceInstanceable : std::uint8_t(0);
    e.mapOrDungeon = f.mapId;
    e.seq = f.instanceId;
    e.hpPct10 = f.serial;
    e.x = f.x;
    e.y = f.y;
    std::memcpy(&e.reserved, &f.z, sizeof(e.reserved));
    return e;
}

inline PlaceFacts GatherPlaceOf(manager_inbox::Event const& e)
{
    PlaceFacts f;
    f.mapId = e.mapOrDungeon;
    f.instanceId = e.seq;
    f.instanceable = (e.level & kPlaceInstanceable) != 0;
    f.x = e.x;
    f.y = e.y;
    std::memcpy(&f.z, &e.reserved, sizeof(f.z));
    f.serial = e.hpPct10;
    return f;
}

// The manager's latest GatherPlace of one bot (places_, like edges_).
struct PlaceSnapshot
{
    PlaceFacts place;
    std::uint32_t receivedMs = 0;
    bool busy = false;   // the bot's Status said busy (taxi, teleport) when this place was stored (no jump count then)
};

inline PlaceSnapshot PlaceSnapshotOf(manager_inbox::Event const& e, std::uint32_t receivedMs)
{
    PlaceSnapshot s;
    s.place = GatherPlaceOf(e);
    s.receivedMs = receivedMs;
    return s;
}

// 2.8 (K0-7, K1-18): a place far from the bot's previous one - descriptive only (Walk holds the live jumps: hops, far moves,
// flights, the hearthstone node). Another map is its own kind of jump.
enum class PlaceJump : std::uint8_t
{
    None = 0,
    SameMap,
    OtherMap,
};

constexpr float kPlaceJumpYards = 200.0f;

inline PlaceJump JumpOf(PlaceFacts const& before, PlaceFacts const& now, float yards)
{
    if (before.mapId != now.mapId)
        return PlaceJump::OtherMap;
    float const dx = now.x - before.x;
    float const dy = now.y - before.y;
    return dx * dx + dy * dy > yards * yards ? PlaceJump::SameMap : PlaceJump::None;
}

// ---- the place the manager judges (2.4) -------------------------------------------------------------------------------------
// alive and teleporting from the Status flags (kFlagAlive, kFlagBusy: taxi, teleport, band trip, parked); in the world only
// while both the Status and the GatherPlace are at most maxAgeMs old (K1-23; a bot in a far teleport posts nothing and shows as
// missing freshness). F7 (a): on a map that is not instanceable the instance id counts as 0 (continent regions are reachable
// on foot, the BOT-12 OnOtherMap rule). F12 (a) (= S7): with deadCounts a dead bot at the point is there.
inline Place PlaceOf(manager_inbox::StatusSnapshot const* status, PlaceSnapshot const* place, std::uint32_t nowMs,
    std::uint32_t maxAgeMs, bool deadCounts)
{
    Place p;
    if (!status || !place)
        return p;
    manager_inbox::StatusFlags const f = manager_inbox::UnpackFlags(status->flags);
    p.alive = deadCounts || f.alive;
    p.inWorld = manager_inbox::Fresh(status->receivedMs, nowMs, maxAgeMs) && manager_inbox::Fresh(place->receivedMs, nowMs, maxAgeMs);
    p.teleporting = f.busy;
    p.mapId = place->place.mapId;
    p.instanceId = place->place.instanceable ? place->place.instanceId : 0u;
    p.x = place->place.x;
    p.y = place->place.y;
    p.z = place->place.z;
    return p;
}

// StatusSeconds plus one pass (2.4, K1-23): the oldest snapshot that still counts. "One pass" is read as one bot engine pass
// (the bot posts at its first pass after StatusSeconds) plus one manager pass (receivedMs is the drain time), each at most
// max(interval, 1000) ms; with only one interval every status cycle would show a fresh bot as "not there" for a moment.
inline std::uint32_t PlaceMaxAgeMs(std::uint32_t statusSeconds, std::uint32_t intervalMs)
{
    std::uint64_t const ms = std::uint64_t(statusSeconds) * 1000u + 2u * std::uint64_t(std::max<std::uint32_t>(intervalMs, 1000u));
    return ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : std::uint32_t(ms);
}

// F7 (a): no z test (S7 has none); z_out only counts arrivals beyond this band.
constexpr float kZUnknown = 1e6f;

// The meeting point at the leader's last place (F1 (a)): same instance rule as PlaceOf, the leg is SameMap (2.6), no deadline
// here (the caller sets it on the manager clock).
inline GatherPoint GatherPointFor(PlaceSnapshot const& leader, float radius, float zBand)
{
    GatherPoint g;
    g.mapId = leader.place.mapId;
    g.instanceId = leader.place.instanceable ? leader.place.instanceId : 0u;
    g.x = leader.place.x;
    g.y = leader.place.y;
    g.z = leader.place.z;
    g.radius = radius;
    g.zBand = zBand;
    g.deadlineMs = 0;
    g.leg = Leg::SameMap;
    return g;
}

// ---- the group verdict (2.2, 2.4) -------------------------------------------------------------------------------------------
struct GatherRules
{
    float radius = 0.0f;             // F7 (a): 2D 30 yd (kGatherYards, like S7)
    float zBand = kZUnknown;         // F7 (a): no z
    float zOutYards = 15.0f;         // z_out: arrived in 2D, |dz| above this (counted only)
    bool deadCounts = true;          // F12 (a)
    std::uint32_t maxAgeMs = 0;      // PlaceMaxAgeMs
};

struct MemberVerdict
{
    std::uint32_t low = 0;
    bool judged = false;             // every member but the leader (F1 (a): the leader is the point)
    bool arrived = false;
    bool zOut = false;               // arrived, |dz| > zOutYards
    bool regionMismatch = false;     // arrived on a map that is not instanceable, the raw instance ids differ
    bool dead = false;               // arrived while its Status says dead (F12 (a): counts as there; dead_at_point)
};

struct GroupVerdict
{
    bool pointKnown = false;         // the leader's last place is fresh; without it nobody is there (2.2)
    bool gathered = false;           // pointKnown and every judged member arrived
    std::uint32_t judged = 0;
    std::uint32_t arrived = 0;
};

// The members to judge come in with their low guid; the leader among them is skipped (S7: bot != leader). Judged against the
// leader's LAST GatherPlace, so a leader on the move never makes the verdict flip by itself (K0-1).
template <typename StatusMap, typename PlaceMap>
GroupVerdict ArrivedForGroup(std::uint32_t leaderLow, std::vector<MemberVerdict>& members, StatusMap const& status,
    PlaceMap const& places, std::uint32_t nowMs, GatherRules const& r)
{
    GroupVerdict v;
    PlaceSnapshot const* leader = nullptr;
    auto const lp = places.find(leaderLow);
    auto const ls = status.find(leaderLow);
    // K1-23: the leader's Status and GatherPlace must both be fresh, like a member's (a leader that stopped posting is no point).
    if (lp != places.end() && ls != status.end() && manager_inbox::Fresh(lp->second.receivedMs, nowMs, r.maxAgeMs) &&
        manager_inbox::Fresh(ls->second.receivedMs, nowMs, r.maxAgeMs))
        leader = &lp->second;
    v.pointKnown = leader != nullptr;
    GatherPoint const point = leader ? GatherPointFor(*leader, r.radius, r.zBand) : GatherPoint();
    for (MemberVerdict& m : members)
    {
        m.judged = m.low != leaderLow;
        m.arrived = false;
        m.zOut = false;
        m.regionMismatch = false;
        m.dead = false;
        if (!m.judged)
            continue;
        ++v.judged;
        if (!leader)
            continue;
        auto const s = status.find(m.low);
        auto const p = places.find(m.low);
        PlaceSnapshot const* own = p == places.end() ? nullptr : &p->second;
        Place const place = PlaceOf(s == status.end() ? nullptr : &s->second, own, nowMs, r.maxAgeMs, r.deadCounts);
        m.arrived = Arrived(place, point);
        if (!m.arrived)
            continue;
        ++v.arrived;
        float const dz = place.z - point.z;
        m.zOut = dz > r.zOutYards || -dz > r.zOutYards;
        m.regionMismatch = !own->place.instanceable && own->place.instanceId != leader->place.instanceId;
        m.dead = !manager_inbox::UnpackFlags(s->second.flags).alive;
    }
    v.gathered = v.pointKnown && v.arrived == v.judged;
    return v;
}

// T33-0b (F2 (a), design 2.1): a manager ADHOC group has no meeting point; the shadow only counts who is at its formation anchor
// (ManagedGroup mapId/anchorX/anchorY = the leader's Status at creation). Every member but the leader is judged (S7: bot !=
// leader), 2D within the radius, no z (the anchor has none). The anchor carries no instance id: on a map that is not
// instanceable the instance counts as 0 (PlaceOf); on an instanceable map the point takes the leader's instance from its
// fresh GatherPlace on the anchor map, so without that place nobody there counts as arrived. Never a directive or a deadline.
inline GatherPoint AnchorPointFor(std::uint32_t mapId, float x, float y, PlaceSnapshot const* leader, float radius)
{
    GatherPoint g;
    g.mapId = mapId;
    g.instanceId = leader && leader->place.mapId == mapId && leader->place.instanceable ? leader->place.instanceId : 0u;
    g.x = x;
    g.y = y;
    g.z = 0.0f;
    g.radius = radius;
    g.zBand = kZUnknown;
    g.deadlineMs = 0;
    g.leg = Leg::SameMap;
    return g;
}

template <typename StatusMap, typename PlaceMap>
GroupVerdict ArrivedAtAnchor(std::uint32_t mapId, float x, float y, std::uint32_t leaderLow, std::vector<MemberVerdict>& members,
    StatusMap const& status, PlaceMap const& places, std::uint32_t nowMs, GatherRules const& r)
{
    GroupVerdict v;
    v.pointKnown = true;   // the anchor is fixed at creation
    auto const lp = places.find(leaderLow);
    PlaceSnapshot const* leader =
        lp != places.end() && manager_inbox::Fresh(lp->second.receivedMs, nowMs, r.maxAgeMs) ? &lp->second : nullptr;
    GatherPoint const point = AnchorPointFor(mapId, x, y, leader, r.radius);
    for (MemberVerdict& m : members)
    {
        m.judged = m.low != leaderLow;
        m.arrived = false;
        m.zOut = false;
        m.regionMismatch = false;
        m.dead = false;
        if (!m.judged)
            continue;
        ++v.judged;
        auto const s = status.find(m.low);
        auto const p = places.find(m.low);
        Place const place = PlaceOf(s == status.end() ? nullptr : &s->second, p == places.end() ? nullptr : &p->second, nowMs,
            r.maxAgeMs, r.deadCounts);
        m.arrived = Arrived(place, point);
        if (!m.arrived)
            continue;
        ++v.arrived;
        m.dead = !manager_inbox::UnpackFlags(s->second.flags).alive;
    }
    v.gathered = v.judged > 0 && v.arrived == v.judged;
    return v;
}

// ---- the manager's one clock (2.5) ------------------------------------------------------------------------------------------
// mgrMs grows by the time between two passes (unsigned 32-bit difference); a gap longer than 2 x max(interval, 1000) ms (a
// world hitch, or the manager was off) adds only one interval, like the T30 gap rule. Its own anchor lastMs (0 = not started;
// DrainInbox moves lastPassMs_ first). The first pass starts the clock at 1, so 0 stays "unset" for every anchor on it.
inline std::uint64_t AdvanceClock(std::uint64_t mgrMs, std::uint32_t& lastMs, std::uint32_t nowMs, std::uint32_t intervalMs)
{
    if (!lastMs)
    {
        lastMs = manager_inbox::PostStamp(nowMs);
        return mgrMs ? mgrMs : 1u;
    }
    std::uint32_t const delta = nowMs - lastMs;
    lastMs = manager_inbox::PostStamp(nowMs);
    std::uint32_t const limit = 2u * std::max<std::uint32_t>(intervalMs, 1000u);
    return mgrMs + (delta > limit ? intervalMs : delta);
}
}
