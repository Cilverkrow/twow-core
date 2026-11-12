#pragma once

#include "CohortPolicy.h"

#include <cstddef>
#include <cstdint>

// A3 step 6 (ADR-0041, OB-00 06.11.2026; AiPlayerbot.BotGroups.Manager, default 0): the group manager owns the groups of roster
// bots on the world thread. Design: ws-10/cli-grp/a3-schritt6/ENTWURF-A3-SCHRITT6-GRUPPENMANAGER-20261106.md (21b26a95).
// This header is the scaffold's pure part: no game types, no config reads, no locks, no decisions taken anywhere yet. The
// game rules that are still open (role authority E1, which classes tank E2, the level margin E3, a teleport fallback E4, the
// rejoin thresholds E6) are parameters of these functions, never constants in here.
namespace ai::group_mgr
{
// ---- the switch -----------------------------------------------------------------------------------------------------------
// AiPlayerbot.BotGroups.Manager: 0 off (default), 1 all roster bots, 2 cohort B (odd low guid), 3 cohort A (even low guid).
// Any other value is 0 (the loader writes one error line). Same cohort rule as BotGroups.OpQueue (CohortPolicy.h).
inline bool ValidMode(std::uint32_t mode)
{
    return mode <= cohort::ModeOnlyA;
}

inline std::uint32_t SanitizeMode(std::uint32_t mode)
{
    return ValidMode(mode) ? mode : cohort::ModeOff;
}

inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return cohort::Applies(SanitizeMode(mode), guidLow);
}

// ---- group types ----------------------------------------------------------------------------------------------------------
// ADHOC: same quest objective nearby (today AdhocGroupAction/AdhocGroupWorld). QUEST: elite and group quests (today S7
// QuestGroupWorld). DUNGEON: a 5-man run (A3 step 7). One registry holds all three; a bot is in at most one managed group.
enum class GroupType : std::uint8_t
{
    Adhoc = 0,
    Quest = 1,
    Dungeon = 2,
};
constexpr std::size_t kGroupTypes = 3;

inline char const* TypeName(GroupType type)
{
    switch (type)
    {
        case GroupType::Adhoc: return "adhoc";
        case GroupType::Quest: return "quest";
        case GroupType::Dungeon: return "dungeon";
    }
    return "unknown";
}

// ---- T30-1: AiPlayerbot.BotGroups.Manager.Types (design T30 v1.1 5.1, default 0) -------------------------------------------
// A bit mask, bit = 1 << GroupType: 1 ADHOC, 2 QUEST. 4 DUNGEON (T34) and 8 FIXED (T31) are reserved and not active yet, so
// they are masked like any unknown bit (the loader writes one error line). 0 = the manager forms nothing and no gate blocks.
constexpr std::uint32_t kTypeAdhoc = 1u << std::uint32_t(GroupType::Adhoc);
constexpr std::uint32_t kTypeQuest = 1u << std::uint32_t(GroupType::Quest);
constexpr std::uint32_t kTypesActive = kTypeAdhoc | kTypeQuest;

inline bool TypesValid(std::uint32_t raw)
{
    return (raw & ~kTypesActive) == 0;
}

inline std::uint32_t SanitizeTypes(std::uint32_t raw)
{
    return raw & kTypesActive;
}

// F9 (a): Types without status posts cannot work (the manager would see no bot): Types acts as 0 (one error line at load).
inline std::uint32_t EffectiveTypes(std::uint32_t types, std::uint32_t statusSeconds)
{
    return statusSeconds ? SanitizeTypes(types) : 0u;
}

// T30-1c (agreed with CLI-Projekt and OB-00 10.11.): the manager acts only while the arbiter enforces (arbiter = the value of
// supervisor::ArbiterValue: 0 off, 1 shadow, 2 enforce; the key or the console override). The T02-GROUP handler refuses Enter
// and Update below 2 (ack reason 22 arbiter_off), so a group would only form and end: below 2 Types acts like 0 - the manager
// owns no bot (the legacy paths are open again) and its open groups end with switch_off.
constexpr std::uint32_t kManagerArbiter = 2;   // = supervisor::kArbiterEnforce

// SupervisorOn trap (OB-10 review 5d0bdcbb, point 2): a directive reaches only a bot that has the supervisor (Supervisor mode = the
// value of AiPlayerbot.Supervisor: 0 off, 1 all, 2 cohort B, 3 cohort A). With the supervisor off the manager would own the bots
// (and close every legacy group path) but could post no directive, so no group would form. SupervisorAny is the global half
// (LiveTypes: no supervisor at all = Types acts like 0), SupervisorApplies the per-bot half (OwnsLive).
inline bool SupervisorAny(std::uint32_t supervisorMode)
{
    return supervisorMode != cohort::ModeOff && supervisorMode <= cohort::ModeOnlyA;
}

inline bool SupervisorApplies(std::uint32_t supervisorMode, std::uint32_t guidLow)
{
    return SupervisorAny(supervisorMode) && cohort::Applies(supervisorMode, guidLow);
}

inline std::uint32_t LiveTypes(std::uint32_t types, std::uint32_t statusSeconds, std::uint32_t arbiter, std::uint32_t supervisorMode)
{
    return (arbiter == kManagerArbiter && SupervisorAny(supervisorMode)) ? EffectiveTypes(types, statusSeconds) : 0u;
}

inline bool TypeOn(std::uint32_t types, GroupType type)
{
    return (SanitizeTypes(types) & (1u << std::uint32_t(type))) != 0;
}

// F1 (a): the manager owns a bot of its cohort once any type is on - whatever the bits, every legacy group writer is closed
// for it (group_mgr::Blocks = Owns || the bot still stands in the manager's registry).
inline bool Owns(std::uint32_t mode, std::uint32_t types, std::uint32_t guidLow)
{
    return SanitizeTypes(types) != 0 && Applies(mode, guidLow);
}

// The bot-thread view (OwnsBot): Owns, and the bot has the supervisor (the SupervisorOn trap above).
inline bool OwnsLive(std::uint32_t mode, std::uint32_t types, std::uint32_t supervisorMode, std::uint32_t guidLow)
{
    return Owns(mode, types, guidLow) && SupervisorApplies(supervisorMode, guidLow);
}

// T30-1 (agreed with CLI-Projekt 10.11.): the orphan grace of a bot in GROUP. The supervisor's GROUP handler (T02-GROUP) asks
// group_mgr::IsManaged(guid, serial) at most once per 10 s; when it stays false for this long (the manager dropped the bot or
// its group and no Exit reached it), the bot leaves GROUP on its own. Longer than an Exit, its ack timeout and the one retry,
// so a late Exit still comes first.
constexpr std::uint32_t kOrphanGraceMs = 60000;

// ---- roles ----------------------------------------------------------------------------------------------------------------
enum class Role : std::uint8_t
{
    Unknown = 0,
    Tank = 1,
    Healer = 2,
    Dps = 3,
};

inline char const* RoleName(Role role)
{
    switch (role)
    {
        case Role::Tank: return "tank";
        case Role::Healer: return "healer";
        case Role::Dps: return "dps";
        case Role::Unknown: break;
    }
    return "unknown";
}

// What the caller knows about one bot. The caller fills it from the game on the world thread; nothing here reads the game.
// - assigned: a role the manager itself gave this bot for a running group (wins while it lasts).
// - plan: the roster plan role (RosterGuildRolePolicy), Unknown when the bot has no plan.
// - specTank/specHeal: the spec bits (AiFactory::GetPlayerRoles after TankPath/TankKit).
// - kitTank/kitHeal: whether the bot's live kit can hold that seat now. WHICH classes and kits count is the caller's rule (E2).
struct RoleFacts
{
    Role assigned = Role::Unknown;
    Role plan = Role::Unknown;
    bool specTank = false;
    bool specHeal = false;
    bool kitTank = false;
    bool kitHeal = false;
};

// planNeedsKit (E1): true = the plan role only counts when the live kit can hold it (the design's recommendation, no respec);
// false = the plan role always counts.
struct RoleRules
{
    bool planNeedsKit = true;
};

inline bool KitHolds(Role role, RoleFacts const& f)
{
    switch (role)
    {
        case Role::Tank: return f.kitTank;
        case Role::Healer: return f.kitHeal;
        case Role::Dps: return true;
        case Role::Unknown: break;
    }
    return false;
}

// One role per bot, in this order: assigned, then plan (subject to planNeedsKit), then spec bits held by the kit, then dps.
inline Role ResolveRole(RoleFacts const& f, RoleRules const& rules)
{
    if (f.assigned != Role::Unknown)
        return f.assigned;
    if (f.plan != Role::Unknown && (!rules.planNeedsKit || KitHolds(f.plan, f)))
        return f.plan;
    if (f.specTank && f.kitTank)
        return Role::Tank;
    if (f.specHeal && f.kitHeal)
        return Role::Healer;
    return Role::Dps;
}

// ---- dungeon composition --------------------------------------------------------------------------------------------------
constexpr std::size_t kDungeonSize = 5;

struct RoleCount
{
    std::uint32_t tank = 0;
    std::uint32_t healer = 0;
    std::uint32_t dps = 0;
    std::uint32_t unknown = 0;
};

inline RoleCount CountRoles(Role const* roles, std::size_t n)
{
    RoleCount c;
    for (std::size_t i = 0; i < n; ++i)
    {
        switch (roles[i])
        {
            case Role::Tank: ++c.tank; break;
            case Role::Healer: ++c.healer; break;
            case Role::Dps: ++c.dps; break;
            case Role::Unknown: ++c.unknown; break;
        }
    }
    return c;
}

// A dungeon group is exactly 1 tank, 1 healer and 3 dps.
inline bool DungeonCompositionOk(Role const* roles, std::size_t n)
{
    if (n != kDungeonSize)
        return false;
    RoleCount const c = CountRoles(roles, n);
    return c.tank == 1 && c.healer == 1 && c.dps == 3 && c.unknown == 0;
}

// ---- dungeon level fit (G1-4 / G1rc lesson) -------------------------------------------------------------------------------
// The floor is the dungeon's strongest boss, not the meeting-stone minimum: G1rc Stockade at level 24 had 91 deaths in 3 runs,
// at 29 one (RESULT-G1RC 04.11.); in G1-4 the extra deaths came at bosses beyond the group's reach. Every member must reach
// bossTop - bossMargin (the healer bossTop - bossMargin - healerSlack) and stay at or below the dungeon's maximum.
// bossMargin and healerSlack are E3 (owner) and come from config.
struct LevelFitRules
{
    std::uint32_t bossMargin = 0;
    std::uint32_t healerSlack = 0;
};

enum class LevelFit : std::uint8_t
{
    Fits = 0,
    NoData = 1,      // bossTop or dungeonMax unknown (0): fail closed
    Empty = 2,
    BelowBoss = 3,
    AboveMax = 4,
};

inline char const* LevelFitName(LevelFit fit)
{
    switch (fit)
    {
        case LevelFit::Fits: return "fits";
        case LevelFit::NoData: return "no_data";
        case LevelFit::Empty: return "empty";
        case LevelFit::BelowBoss: return "below_boss";
        case LevelFit::AboveMax: return "above_max";
    }
    return "unknown";
}

inline std::uint32_t FloorFor(std::uint32_t bossTop, std::uint32_t margin)
{
    return bossTop > margin ? bossTop - margin : 0;
}

struct MemberLevel
{
    std::uint32_t level = 0;
    Role role = Role::Unknown;
};

// The first failing member decides the verdict; BelowBoss is reported before AboveMax for the same member.
inline LevelFit GroupFitsDungeon(MemberLevel const* members, std::size_t n, std::uint32_t bossTop, std::uint32_t dungeonMax,
    LevelFitRules const& rules)
{
    if (bossTop == 0 || dungeonMax == 0)
        return LevelFit::NoData;
    if (n == 0)
        return LevelFit::Empty;
    std::uint32_t const floor = FloorFor(bossTop, rules.bossMargin);
    std::uint32_t const healerFloor = FloorFor(floor, rules.healerSlack);
    for (std::size_t i = 0; i < n; ++i)
    {
        std::uint32_t const need = members[i].role == Role::Healer ? healerFloor : floor;
        if (members[i].level < need)
            return LevelFit::BelowBoss;
        if (members[i].level > dungeonMax)
            return LevelFit::AboveMax;
    }
    return LevelFit::Fits;
}

// ---- meeting point --------------------------------------------------------------------------------------------------------
// One arrival predicate for every group type (today S7, follow, ad-hoc, DcStranded compare the map id, DcAutoRun the Map*,
// the .dc test harness the instance id). Present = alive, in the world, not teleporting, same map AND instance, inside the
// radius in 2D and inside the z band.
struct Place
{
    bool alive = false;
    bool inWorld = false;
    bool teleporting = false;
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;   // 0 = open world
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class Leg : std::uint8_t
{
    SameMap = 0,
    Transport = 1,
    InstanceEntry = 2,
};

struct GatherPoint
{
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 0.0f;
    float zBand = 0.0f;
    std::uint64_t deadlineMs = 0;   // 0 = no deadline set
    Leg leg = Leg::SameMap;
};

inline bool Arrived(Place const& p, GatherPoint const& g)
{
    if (!p.alive || !p.inWorld || p.teleporting)
        return false;
    if (p.mapId != g.mapId || p.instanceId != g.instanceId)
        return false;
    float const dx = p.x - g.x;
    float const dy = p.y - g.y;
    float const dz = p.z - g.z;
    return dx * dx + dy * dy <= g.radius * g.radius && dz <= g.zBand && -dz <= g.zBand;
}

inline bool LegExpired(std::uint64_t nowMs, GatherPoint const& g)
{
    return g.deadlineMs != 0 && nowMs >= g.deadlineMs;
}

// Fallback ladder after a leg expires: walk, a hidden same-map hop while unobserved, a manager teleport (only when allowed, E4,
// and only while unobserved), the hearthstone, then dissolve with a reason. Never a silent wait. Dissolve is terminal.
enum class Fallback : std::uint8_t
{
    Walk = 0,
    HiddenHop = 1,
    Teleport = 2,
    Hearthstone = 3,
    Dissolve = 4,
};

inline char const* FallbackName(Fallback step)
{
    switch (step)
    {
        case Fallback::Walk: return "walk";
        case Fallback::HiddenHop: return "hidden_hop";
        case Fallback::Teleport: return "teleport";
        case Fallback::Hearthstone: return "hearthstone";
        case Fallback::Dissolve: return "dissolve";
    }
    return "unknown";
}

inline Fallback NextFallback(Fallback current, bool unobserved, bool allowTeleport)
{
    switch (current)
    {
        case Fallback::Walk:
            if (unobserved)
                return Fallback::HiddenHop;
            [[fallthrough]];
        case Fallback::HiddenHop:
            if (allowTeleport && unobserved)
                return Fallback::Teleport;
            [[fallthrough]];
        case Fallback::Teleport:
            return Fallback::Hearthstone;
        case Fallback::Hearthstone:
        case Fallback::Dissolve:
            break;
    }
    return Fallback::Dissolve;
}
}
