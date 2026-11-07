#pragma once

#include "CohortPolicy.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor, default 0): one supervisor per bot owns
// the bot's mode (QUEST, GRIND, REST, GROUP, RESCUE), its phase, and later its travel target and its tick cadence. Design:
// ws-00/projekt-durchleuchtung/A3-S3-ENTWURF.md (c98e6ded). This header is the scaffold's pure part: no game types, no config
// reads, no locks, no log calls. Nothing calls Decide or CadenceFloorMs in the game yet; the pairs that give the supervisor its
// behaviour do, each behind this switch and each with its own A/B (ADR-0041 rules 1 and 3). Every threshold that is not decided
// yet (the cadence factors, the dwell time) is a parameter here, never a constant. <cstdio> is only for std::snprintf in the
// two pure line formatters (decision F2 a: the lines come with the first behaviour pair, the formatters are tested now).
namespace ai::supervisor
{
// ---- the switch -----------------------------------------------------------------------------------------------------------
// AiPlayerbot.Supervisor: 0 off (default, rollback), 1 all bots, 2 cohort B (odd low guid), 3 cohort A (even low guid). Any
// other value is 0 (the loader writes one error line). Same cohort rule as the other A3 switches (CohortPolicy.h).
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

// ---- modes and phases -----------------------------------------------------------------------------------------------------
// Exactly five modes. GROUP also covers the dungeon (step 7). PASSIVE is not a mode: it is the state "the supervisor gave up the
// lead" (a real player master, a battleground, go/debug commands, a parked bot) - it holds still, writes nothing, only counts.
enum class Mode : std::uint8_t
{
    Quest = 0,
    Grind = 1,
    Rest = 2,
    Group = 3,
    Rescue = 4,
    Passive = 5,
};
constexpr std::size_t kModes = 5;   // without Passive

inline bool IsMode(Mode mode)
{
    return std::size_t(mode) < kModes;
}

// The phase inside QUEST, GRIND and GROUP; it maps today's TravelStatus (NONE/PREPARE/READY = Plan, TRAVEL, WORK, COOLDOWN,
// EXPIRED = Closed). RegroupRest exists only inside GROUP (gather and rest after a wipe) and is entered by a manager directive.
enum class Phase : std::uint8_t
{
    Plan = 0,
    Travel = 1,
    Work = 2,
    Cooldown = 3,
    Closed = 4,
    RegroupRest = 5,
};
constexpr std::size_t kPhases = 6;

// Who asks for a change (table 1.3 of the design).
enum class Requester : std::uint8_t
{
    Planner = 0,        // the activity planner: QUEST <-> GRIND only
    Self = 1,           // the supervisor's own event handling (died, resurrected, rest done, group left)
    GroupMgr = 2,       // the group manager (A3 step 6), the only one that may enter GROUP
    CombatGuard = 3,    // R8 combat loop guard
    ReplanSink = 4,     // R9 progress meter (CLI-PATH IReplanSink)
    RestGate = 5,       // the rest check (RestGatePolicy thresholds)
    LegacyWriter = 6,   // a legacy travel-target writer (section 1.7, with its WriterId): only the RESCUE funnel
    External = 7,       // go/debug, real player master, battleground, park: PASSIVE and back
};

// Mailbox events (section 1.10). Wired by the later pairs; the scaffold only defines them.
enum class EventKind : std::uint8_t
{
    QuestAccepted = 0,
    QuestComplete,
    QuestRewarded,
    QuestFailed,
    QuestAbandoned,
    TravelStatus,
    Relocate,          // R2
    CombatStart,
    CombatEnd,
    KillCredit,
    LevelUp,
    Died,
    ReleasedGhost,
    Resurrected,
    SpellLearned,
    SkillUp,
    ItemLooted,
    GroupJoin,
    GroupLeave,
    MapChanged,
    ZoneChanged,
    Stall,             // R9
    CombatLoop,        // R8
    GroupDirective,    // the group manager, with a = seq
    BrainIntent,       // bot brain preference, with a TTL
    Count,
};

// Plain data, copyable across threads: ids and numbers only, never a pointer to a world object (ADR-0039).
struct Event
{
    EventKind kind = EventKind::Count;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t atMs = 0;   // game ms clock (wraps); compare only by unsigned difference
    std::uint8_t source = 0;
};

// The eight families of travel-target writers (section 1.7), plus Unknown and Test.
enum class WriterId : std::uint8_t
{
    Unknown = 0,
    Select,
    Requester,
    Execution,
    Lifecycle,
    Manager,
    GroupFollow,
    Command,
    Brain,
    Test,
};

inline char const* ModeName(Mode mode)
{
    switch (mode)
    {
        case Mode::Quest: return "QUEST";
        case Mode::Grind: return "GRIND";
        case Mode::Rest: return "REST";
        case Mode::Group: return "GROUP";
        case Mode::Rescue: return "RESCUE";
        case Mode::Passive: return "PASSIVE";
    }
    return "UNKNOWN";
}

inline char const* PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::Plan: return "PLAN";
        case Phase::Travel: return "TRAVEL";
        case Phase::Work: return "WORK";
        case Phase::Cooldown: return "COOLDOWN";
        case Phase::Closed: return "CLOSED";
        case Phase::RegroupRest: return "REGROUP_REST";
    }
    return "UNKNOWN";
}

inline char const* RequesterName(Requester who)
{
    switch (who)
    {
        case Requester::Planner: return "Planner";
        case Requester::Self: return "Self";
        case Requester::GroupMgr: return "GroupMgr";
        case Requester::CombatGuard: return "CombatGuard";
        case Requester::ReplanSink: return "ReplanSink";
        case Requester::RestGate: return "RestGate";
        case Requester::LegacyWriter: return "Legacy";
        case Requester::External: return "External";
    }
    return "Unknown";
}

// ---- priority -------------------------------------------------------------------------------------------------------------
// RESCUE > GROUP > REST > QUEST = GRIND. PASSIVE outranks everything but is only reachable through External.
inline std::uint8_t Priority(Mode mode)
{
    switch (mode)
    {
        case Mode::Passive: return 5;
        case Mode::Rescue: return 4;
        case Mode::Group: return 3;
        case Mode::Rest: return 2;
        case Mode::Quest:
        case Mode::Grind: return 1;
    }
    return 0;
}

// ---- cadence (R7) ---------------------------------------------------------------------------------------------------------
// The cadence is a minimum gap the supervisor will lay on the engine delay after each action (mode x phase). The factors are
// NOT decided (ADR-0041 open point; a PREREG sets them): rate in permille, 1000 = full rate, 250 = a quarter (the delay x4).
// A factor of 0 is read as 1000, so an unset entry never throttles.
struct Cadence
{
    std::uint16_t ratePermille[kModes][kPhases];
    std::uint32_t minDwellMs;   // QUEST -> GRIND hysteresis
    std::uint32_t maxDelayMs;   // upper bound of any floor
};

// Full rate everywhere: the neutral parameter set.
inline Cadence FullRate(std::uint32_t minDwellMs, std::uint32_t maxDelayMs)
{
    Cadence c{};
    for (std::size_t m = 0; m < kModes; ++m)
        for (std::size_t p = 0; p < kPhases; ++p)
            c.ratePermille[m][p] = 1000;
    c.minDwellMs = minDwellMs;
    c.maxDelayMs = maxDelayMs;
    return c;
}

// The delay floor in ms for this mode and phase: reactDelay / rate, never below reactDelay, never above maxDelayMs (unless
// reactDelay itself is above it). PASSIVE and unknown values get reactDelay: the supervisor never slows a bot it does not lead.
inline std::uint32_t CadenceFloorMs(Mode mode, Phase phase, std::uint32_t reactDelayMs, Cadence const& c)
{
    if (!IsMode(mode) || std::size_t(phase) >= kPhases)
        return reactDelayMs;
    std::uint32_t rate = c.ratePermille[std::size_t(mode)][std::size_t(phase)];
    if (rate == 0 || rate > 1000)
        rate = 1000;
    std::uint64_t const floor = std::uint64_t(reactDelayMs) * 1000u / rate;
    if (floor <= reactDelayMs)
        return reactDelayMs;
    if (floor > c.maxDelayMs)
        return c.maxDelayMs > reactDelayMs ? c.maxDelayMs : reactDelayMs;
    return std::uint32_t(floor);
}

// ---- transitions (table 1.3) ----------------------------------------------------------------------------------------------
enum class Deny : std::uint8_t
{
    None = 0,
    SameMode = 1,        // already in that mode
    NotAMode = 2,        // an out-of-range value
    Requester = 3,       // this requester may not ask for this change
    PassiveHold = 4,     // PASSIVE is left only through External
    Hysteresis = 5,      // QUEST -> GRIND before minDwellMs
    Phase = 6,           // RegroupRest outside GROUP
    RescueFirst = 7,     // GROUP is not entered from RESCUE (the rescue returns to the group itself)
};

inline char const* DenyName(Deny deny)
{
    switch (deny)
    {
        case Deny::None: return "-";
        case Deny::SameMode: return "same_mode";
        case Deny::NotAMode: return "not_a_mode";
        case Deny::Requester: return "requester";
        case Deny::PassiveHold: return "passive_hold";
        case Deny::Hysteresis: return "hysteresis";
        case Deny::Phase: return "phase";
        case Deny::RescueFirst: return "rescue_first";
    }
    return "unknown";
}

struct TransitionResult
{
    bool allowed;
    Mode to;
    Deny reason;
};

inline TransitionResult Allow(Mode to)
{
    return TransitionResult{true, to, Deny::None};
}

inline TransitionResult Refuse(Mode from, Deny reason)
{
    return TransitionResult{false, from, reason};
}

// One rule for every mode change. The caller applies the result; this function only decides.
// - Nobody but External enters or leaves PASSIVE; External may only go to PASSIVE or, at the end of the external lead, back to
//   QUEST or GRIND (the planner replans, R2).
// - RESCUE is the funnel for deaths, stuck, R8 and the legacy rescue writers: it is never refused to them (only to the planner,
//   the group manager and RestGate, which never ask for it). RESCUE is left by Self only.
// - GROUP is entered only by the group manager and never from RESCUE; it is left by the manager or by Self (group left), back
//   to QUEST or GRIND only.
// - REST is entered by RestGate or Self from QUEST or GRIND (never by the planner, never from GROUP: inside a group the rest is
//   the RegroupRest phase), and left by Self.
// - QUEST <-> GRIND belong to the planner; QUEST -> GRIND waits minDwellMs, GRIND -> QUEST does not (QB4: a finished quest ends
//   the fallback at once; the planner only asks after an event).
// - RegroupRest is a GROUP phase: outside GROUP it refuses every request except the RESCUE funnel and the RESCUE exit, which
//   never depend on the phase.
inline TransitionResult Decide(Mode from, Mode to, Requester who, Phase phase, std::uint32_t dwellMs, Cadence const& c)
{
    if (std::uint8_t(from) > std::uint8_t(Mode::Passive) || std::uint8_t(to) > std::uint8_t(Mode::Passive))
        return Refuse(from, Deny::NotAMode);
    if (from == to)
        return Refuse(from, Deny::SameMode);

    if (to == Mode::Passive)
        return who == Requester::External ? Allow(to) : Refuse(from, Deny::Requester);
    if (from == Mode::Passive)
    {
        if (who != Requester::External)
            return Refuse(from, Deny::PassiveHold);
        return (to == Mode::Quest || to == Mode::Grind) ? Allow(to) : Refuse(from, Deny::Requester);
    }
    if (who == Requester::External)
        return Refuse(from, Deny::Requester);

    if (to == Mode::Rescue)
    {
        bool const funnel = who == Requester::Self || who == Requester::CombatGuard || who == Requester::LegacyWriter ||
            who == Requester::ReplanSink;
        return funnel ? Allow(to) : Refuse(from, Deny::Requester);
    }
    if (from == Mode::Rescue)
    {
        if (who == Requester::GroupMgr && to == Mode::Group)
            return Refuse(from, Deny::RescueFirst);
        if (who != Requester::Self)
            return Refuse(from, Deny::Requester);
        return (to == Mode::Quest || to == Mode::Grind || to == Mode::Group) ? Allow(to) : Refuse(from, Deny::Requester);
    }

    if (phase == Phase::RegroupRest && from != Mode::Group)
        return Refuse(from, Deny::Phase);

    if (to == Mode::Group)
        return who == Requester::GroupMgr ? Allow(to) : Refuse(from, Deny::Requester);
    if (from == Mode::Group)
    {
        if (who != Requester::GroupMgr && who != Requester::Self)
            return Refuse(from, Deny::Requester);
        // back to QUEST or GRIND; the group's target is closed, not taken over. No REST out of GROUP (RegroupRest instead).
        return (to == Mode::Quest || to == Mode::Grind) ? Allow(to) : Refuse(from, Deny::Requester);
    }

    if (to == Mode::Rest)
        return (who == Requester::RestGate || who == Requester::Self) ? Allow(to) : Refuse(from, Deny::Requester);
    if (from == Mode::Rest)
        return who == Requester::Self ? Allow(to) : Refuse(from, Deny::Requester);

    // QUEST <-> GRIND
    if (who != Requester::Planner)
        return Refuse(from, Deny::Requester);
    if (from == Mode::Quest && dwellMs < c.minDwellMs)
        return Refuse(from, Deny::Hysteresis);
    return Allow(to);
}

// ---- decision lines (section 4; emitted from the first behaviour pair on) -----------------------------------------------
// Fixed buffers, tag first, then key=value; '\n' and '\r' become '_'. Each returns the length written (< n), 0 when n == 0.
inline std::size_t Sanitize(char* buf, int written, std::size_t n)
{
    if (n == 0)
        return 0;
    std::size_t len = written < 0 ? 0 : std::size_t(written);
    if (len >= n)
        len = n - 1;
    buf[len] = '\0';
    for (std::size_t i = 0; i < len; ++i)
        if (buf[i] == '\n' || buf[i] == '\r')
            buf[i] = '_';
    return len;
}

struct ModeLine
{
    std::uint32_t guid = 0;
    char const* name = "";
    char cohort = 'A';
    std::uint32_t arbiter = 0;
    std::uint32_t level = 0;
    std::uint32_t map = 0;
    std::uint32_t zone = 0;
    char const* cause = "-";
    Mode from = Mode::Passive;
    Mode to = Mode::Passive;
    Phase phase = Phase::Plan;
    Requester req = Requester::Self;
    std::uint32_t dwellMs = 0;
    std::uint32_t cadenceMs = 0;
    Deny deny = Deny::None;
};

inline std::size_t FormatModeLine(char* buf, std::size_t n, ModeLine const& l)
{
    if (n == 0)
        return 0;
    int const w = std::snprintf(buf, n, "[Supervisor] bot=%u name=%.40s cohort=%c arbiter=%u lvl=%u map=%u zone=%u ev=mode from=%s to=%s "
        "phase=%s cause=%s req=%s dwell_ms=%u cadence_ms=%u denied=%u deny_reason=%s", l.guid, l.name ? l.name : "", l.cohort,
        l.arbiter, l.level, l.map, l.zone, ModeName(l.from), ModeName(l.to), PhaseName(l.phase), l.cause ? l.cause : "-",
        RequesterName(l.req), l.dwellMs, l.cadenceMs, l.deny == Deny::None ? 0u : 1u, DenyName(l.deny));
    return Sanitize(buf, w, n);
}

struct PlanAlt
{
    char const* kind = nullptr;   // nullptr = no alternative in this slot
    std::uint32_t id = 0;
    float value = 0.0f;
};

struct PlanLine
{
    std::uint32_t guid = 0;
    char cohort = 'A';
    std::uint32_t arbiter = 0;
    Mode mode = Mode::Quest;
    char const* kind = "-";
    std::uint32_t id = 0;
    std::uint32_t destId = 0;
    float value = 0.0f;
    float xpm = 0.0f;
    float routeSec = 0.0f;
    float workSec = 0.0f;
    float restSec = 0.0f;
    float danger = 0.0f;
    bool turnIn = false;
    std::uint32_t brain = 0;
    char const* reason = "-";
    std::uint32_t cands = 0;
    PlanAlt alt[3];
    std::uint32_t blocked = 0;
    std::uint32_t budgetMs = 0;
};

inline std::size_t FormatPlanLine(char* buf, std::size_t n, PlanLine const& l)
{
    if (n == 0)
        return 0;
    char alts[3][64];
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (l.alt[i].kind)
            std::snprintf(alts[i], sizeof(alts[i]), "%s:%u:%.2f", l.alt[i].kind, l.alt[i].id, double(l.alt[i].value));
        else
            std::snprintf(alts[i], sizeof(alts[i]), "-");
    }
    int const w = std::snprintf(buf, n, "[Plan] bot=%u cohort=%c arbiter=%u mode=%s pick=%s:%u:%u value=%.2f xpm=%.1f route_s=%.0f "
        "work_s=%.0f rest_s=%.0f danger=%.2f turnin=%u brain=%u reason=%s cands=%u alt1=%s alt2=%s alt3=%s blocked=%u budget_ms=%u",
        l.guid, l.cohort, l.arbiter, ModeName(l.mode), l.kind ? l.kind : "-", l.id, l.destId, double(l.value), double(l.xpm),
        double(l.routeSec), double(l.workSec), double(l.restSec), double(l.danger), l.turnIn ? 1u : 0u, l.brain,
        l.reason ? l.reason : "-", l.cands, alts[0], alts[1], alts[2], l.blocked, l.budgetMs);
    return Sanitize(buf, w, n);
}
}
