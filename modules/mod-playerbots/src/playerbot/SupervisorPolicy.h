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

// Mailbox events (section 1.10). The mailbox pair (SupervisorMail.cpp) carries them; what a supervisor does with each kind comes
// in the later pairs.
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
    GroupDirective,    // the group manager: a = seq, b = directive type, c = target id, mapId/x/y/z = meeting point
    BrainIntent,       // bot brain preference, with a TTL
    Count,
};

// Plain data, copyable across threads: ids and numbers only, never a pointer to a world object (ADR-0039).
struct Event
{
    EventKind kind = EventKind::Count;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;      // a target id (mailbox pair, agreed with CLI-GRP 09.11.2026 for GroupDirective)
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
    Group,    // A3 step 3 T02-a: the group lane (AiPlayerbot.Supervisor.Arbiter = 2, default 0)
    Errand,   // A3 step 3 T02-a: the close-then-request errands (petition, guild trip, profession trip, station craft)
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

// ---- arbiter shadow (A3 step 3, behaviour pair 1; AiPlayerbot.Supervisor.Arbiter, default 0) -------------------------------
// Design: ws-00/projekt-durchleuchtung/A3-S3-ARBITER-SCHATTEN-ENTWURF.md v1.2 (OB-00 07.11.: E1-E4 a). The shadow only watches
// the live travel target of a bot the supervisor runs for: one observer at the end of the status write, the steps of one engine
// pass bundled into one record. A line is written only for an open target closed by a family that is not its own plan, for a
// COOLDOWN expiry and for a writer without scope; every other bundle is only counted, once per minute and cohort
// ([ArbiterMin]). The shadow writes nothing of the bot. A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): 2 is
// enforcement (the shadow stays on, because 2 >= 1); every value above 2 is off.
inline bool ValidArbiter(std::uint32_t value)
{
    return value <= 2;
}

// Live arbiter switch (OB-00 09.11.2026, arbiter shadow run: the kit switches the shadow without a config reload). The console
// command "rndbot arbiter on|off|enforce" sets an override: kArbiterFollowConfig (the start state) = the gate uses
// AiPlayerbot.Supervisor.Arbiter, kArbiterOff / kArbiterOn / kArbiterEnforce = the console value until the next restart. The
// conf key stays the start value. A3 step 3 T02-a (AiPlayerbot.Supervisor.Arbiter = 2, default 0): enforce = 2.
constexpr int kArbiterFollowConfig = -1;
constexpr int kArbiterOff = 0;
constexpr int kArbiterOn = 1;
constexpr int kArbiterEnforce = 2;

// The console argument: "on" or "1" = kArbiterOn, "off" or "0" = kArbiterOff, "enforce" or "2" = kArbiterEnforce (surrounding
// blanks ignored); anything else, the empty argument too, is kArbiterFollowConfig = usage, nothing is set.
inline int ParseArbiterSwitch(char const* arg)
{
    if (!arg)
        return kArbiterFollowConfig;
    while (*arg == ' ' || *arg == '\t')
        ++arg;
    std::size_t len = 0;
    while (arg[len])
        ++len;
    while (len && (arg[len - 1] == ' ' || arg[len - 1] == '\t' || arg[len - 1] == '\r' || arg[len - 1] == '\n'))
        --len;
    auto const is = [arg, len](char const* word) {
        std::size_t i = 0;
        for (; word[i]; ++i)
            if (i >= len || arg[i] != word[i])
                return false;
        return i == len;
    };
    if (is("on") || is("1"))
        return kArbiterOn;
    if (is("off") || is("0"))
        return kArbiterOff;
    if (is("enforce") || is("2"))
        return kArbiterEnforce;
    return kArbiterFollowConfig;
}

// The value the gate passes to Tick: the console override when one is set, else the config value (0 off, 1 shadow, 2 enforce;
// above 2 is off).
inline std::uint32_t ArbiterEffective(int override, std::uint32_t configValue)
{
    if (override == kArbiterOff)
        return 0;
    if (override == kArbiterOn)
        return 1;
    if (override == kArbiterEnforce)
        return 2;
    return configValue <= 2 ? configValue : 0;
}

// The travel status as numbers, in the order of the game's enum (TravelMgr.h TravelStatus, uint8), so this header stays free of
// game types. SupervisorShadow.cpp asserts the order at compile time.
constexpr std::uint8_t kStatusNone = 0;
constexpr std::uint8_t kStatusPrepare = 1;
constexpr std::uint8_t kStatusReady = 2;
constexpr std::uint8_t kStatusTravel = 3;
constexpr std::uint8_t kStatusWork = 4;
constexpr std::uint8_t kStatusCooldown = 5;
constexpr std::uint8_t kStatusExpired = 6;

inline char const* StatusName(std::uint8_t status)
{
    switch (status)
    {
        case kStatusNone: return "NONE";
        case kStatusPrepare: return "PREPARE";
        case kStatusReady: return "READY";
        case kStatusTravel: return "TRAVEL";
        case kStatusWork: return "WORK";
        case kStatusCooldown: return "COOLDOWN";
        case kStatusExpired: return "EXPIRED";
        default: break;
    }
    return "?";
}

// Open (design 4.4): READY, TRAVEL, WORK or COOLDOWN on a destination other than the null target. A COOLDOWN is no end for the
// supervisor (CLI-Berufe).
inline bool IsOpen(std::uint8_t status, bool nullDest)
{
    return !nullDest && (status == kStatusReady || status == kStatusTravel || status == kStatusWork || status == kStatusCooldown);
}

constexpr std::size_t kWriterIds = 12;

inline char const* WriterName(WriterId writer)
{
    switch (writer)
    {
        case WriterId::Unknown: return "Unknown";
        case WriterId::Select: return "Select";
        case WriterId::Requester: return "Requester";
        case WriterId::Execution: return "Execution";
        case WriterId::Lifecycle: return "Lifecycle";
        case WriterId::Manager: return "Manager";
        case WriterId::GroupFollow: return "GroupFollow";
        case WriterId::Command: return "Command";
        case WriterId::Brain: return "Brain";
        case WriterId::Test: return "Test";
        case WriterId::Group: return "Group";
        case WriterId::Errand: return "Errand";
    }
    return "Unknown";
}

// Design 4.4 point 1: an open target closed by one of these families gets a line. Lifecycle, Execution, Command and Test close
// by plan and are only counted.
inline bool CloserIsLine(WriterId writer)
{
    return writer == WriterId::Select || writer == WriterId::Requester || writer == WriterId::GroupFollow ||
        writer == WriterId::Manager || writer == WriterId::Brain || writer == WriterId::Group || writer == WriterId::Unknown;
}

constexpr std::size_t kPathSlots = 4;

// The steps one engine pass made on the live target (design 4.2): opened by the first observed write after a tick, closed by the
// next tick of the bot or by Release. The path keeps the first kPathSlots steps; steps counts on.
struct Bundle
{
    std::uint32_t steps = 0;
    std::uint8_t fromStatus = kStatusNone;
    bool fromNull = true;
    std::uint32_t fromSinceMs = 0;   // when the from-destination was set (game ms; compare by unsigned difference only)
    std::uint8_t path[kPathSlots] = {};
    bool pathNewDest[kPathSlots] = {};
    std::uint8_t curStatus = kStatusNone;
    bool curNull = true;
    bool otherDest = false;          // the destination after the last step is not the from-destination
    std::uint16_t writers = 0;       // one bit per WriterId
    WriterId first = WriterId::Unknown;
    bool hasCloser = false;          // the first step that left an open target
    WriterId closer = WriterId::Unknown;
    std::uint32_t closeAtMs = 0;
    bool hasExpire = false;          // the first step into EXPIRED
    std::uint8_t expireFrom = kStatusNone;
    WriterId expireWriter = WriterId::Unknown;
    std::uint32_t expireAtMs = 0;
    bool toNone = false;             // a step into NONE
    std::uint32_t lastAtMs = 0;
    bool forced = false;             // the target's forced flag after the last step
    bool gap = false;                // a write the shadow could not follow (no thread tag, another bot's thread) came first
};

inline void BundleStart(Bundle& b, std::uint8_t fromStatus, bool fromNull, std::uint32_t fromSinceMs)
{
    b = Bundle();
    b.fromStatus = fromStatus;
    b.fromNull = fromNull;
    b.fromSinceMs = fromSinceMs;
    b.curStatus = fromStatus;
    b.curNull = fromNull;
}

// One observed status write. newDest: the destination differs from the one before this step; otherDest: from the bundle's
// from-destination.
inline void BundleStep(Bundle& b, std::uint8_t status, bool newDest, bool otherDest, bool nullDest, WriterId writer, bool forced,
    bool gap, std::uint32_t nowMs)
{
    if (!b.hasCloser && IsOpen(b.curStatus, b.curNull) && (newDest || !IsOpen(status, nullDest)))
    {
        b.hasCloser = true;
        b.closer = writer;
        b.closeAtMs = nowMs;
    }
    if (!b.hasExpire && status == kStatusExpired && b.curStatus != kStatusExpired)
    {
        b.hasExpire = true;
        b.expireFrom = newDest ? kStatusNone : b.curStatus;
        b.expireWriter = writer;
        b.expireAtMs = nowMs;
    }
    if (status == kStatusNone)
        b.toNone = true;
    if (b.steps < kPathSlots)
    {
        b.path[b.steps] = status;
        b.pathNewDest[b.steps] = newDest;
    }
    if (b.steps == 0)
        b.first = writer;
    ++b.steps;
    b.writers = std::uint16_t(b.writers | (1u << unsigned(writer)));
    b.curStatus = status;
    b.curNull = nullDest;
    b.otherDest = otherDest;
    b.forced = forced;
    b.gap = b.gap || gap;
    b.lastAtMs = nowMs;
}

struct BundleClass
{
    bool oldOpen = false;
    bool closesOpen = false;
    bool expireCooldown = false;
    bool unknown = false;
    WriterId family = WriterId::Unknown;   // the family the minute sum counts: the closer, else the first writer
};

inline BundleClass ClassifyBundle(Bundle const& b)
{
    BundleClass c;
    c.oldOpen = IsOpen(b.fromStatus, b.fromNull);
    bool const ends = b.otherDest || !IsOpen(b.curStatus, b.curNull);
    c.closesOpen = c.oldOpen && ends && b.hasCloser && CloserIsLine(b.closer);
    c.expireCooldown = b.hasExpire && b.expireFrom == kStatusCooldown;
    c.unknown = (b.writers & (1u << unsigned(WriterId::Unknown))) != 0;
    c.family = b.hasCloser ? b.closer : b.first;
    return c;
}

// Which line a closed bundle gets (design 4.4): the expiry from COOLDOWN first, then a write line, else none (counted only).
enum class LineKind : std::uint8_t
{
    None = 0,
    Write = 1,
    Expire = 2,
};

inline LineKind LineFor(BundleClass const& c)
{
    if (c.expireCooldown)
        return LineKind::Expire;
    if (c.closesOpen || c.unknown)
        return LineKind::Write;
    return LineKind::None;
}

// Design 4.5 reason table, from (writer, path) only. The exact reasons (work_death, relation_done ...) come with the family pairs.
inline char const* ReasonFor(Bundle const& b)
{
    if (b.hasExpire && b.expireWriter == WriterId::Execution)
    {
        switch (b.expireFrom)
        {
            case kStatusCooldown: return "cooldown_end";
            case kStatusReady: return "timeout_ready";
            case kStatusTravel: return "timeout_travel";
            case kStatusWork: return "timeout_work";
            default: break;
        }
    }
    if (b.toNone)
        return "reset";
    WriterId const writer = b.hasExpire ? b.expireWriter : (b.hasCloser ? b.closer : b.first);
    if (writer == WriterId::Lifecycle)
        return "lifecycle";
    if (writer == WriterId::Manager)
        return "manager";
    if (writer == WriterId::Errand)
        return "lifecycle";
    if (writer == WriterId::Group)
        return "group";
    return "other";
}

// The time the from-destination stood when the bundle left it (or expired it, or wrote last).
inline std::uint32_t BundleAgeMs(Bundle const& b)
{
    std::uint32_t const at = b.hasCloser ? b.closeAtMs : (b.hasExpire ? b.expireAtMs : b.lastAtMs);
    return at - b.fromSinceMs;
}

// "READY>*TRAVEL>READY": the from-status, then every kept step; '*' marks a new destination, ">.." a cut path.
inline std::size_t FormatPath(char* buf, std::size_t n, Bundle const& b)
{
    if (n == 0)
        return 0;
    std::size_t len = Sanitize(buf, std::snprintf(buf, n, "%s", StatusName(b.fromStatus)), n);
    std::size_t const kept = b.steps < kPathSlots ? std::size_t(b.steps) : kPathSlots;
    for (std::size_t i = 0; i < kept && len + 1 < n; ++i)
        len += Sanitize(buf + len, std::snprintf(buf + len, n - len, ">%s%s", b.pathNewDest[i] ? "*" : "", StatusName(b.path[i])),
            n - len);
    if (b.steps > kPathSlots && len + 1 < n)
        len += Sanitize(buf + len, std::snprintf(buf + len, n - len, ">.."), n - len);
    return len;
}

// "Select,Execution": the families of a bundle in WriterId order, "-" for none.
inline std::size_t FormatWriters(char* buf, std::size_t n, std::uint16_t writers)
{
    if (n == 0)
        return 0;
    buf[0] = '\0';
    std::size_t len = 0;
    for (std::size_t i = 0; i < kWriterIds && len + 1 < n; ++i)
        if (writers & (1u << unsigned(i)))
            len += Sanitize(buf + len, std::snprintf(buf + len, n - len, "%s%s", len ? "," : "", WriterName(WriterId(i))), n - len);
    if (len == 0)
        len = Sanitize(buf, std::snprintf(buf, n, "-"), n);
    return len;
}

// The bot and the destinations of a line; the destinations are "<short name>:<entry>:<quest id|0>", the null target "idle:0:0".
struct ShadowLine
{
    std::uint32_t guid = 0;
    char cohort = 'A';
    char const* from = "-";
    char const* to = "-";
};

inline std::size_t FormatWriteLine(char* buf, std::size_t n, ShadowLine const& l, Bundle const& b)
{
    if (n == 0)
        return 0;
    char path[96];
    FormatPath(path, sizeof(path), b);
    char writers[128];
    FormatWriters(writers, sizeof(writers), b.writers);
    BundleClass const c = ClassifyBundle(b);
    int const w = std::snprintf(buf, n, "[Supervisor] bot=%u cohort=%c arbiter=1 ev=write writer=%s closer=%s writers=%s from=%s "
        "to=%s path=%s steps=%u old_open=%u closes_open=%u forced=%u gap=%u age_ms=%u", l.guid, l.cohort, WriterName(b.first),
        b.hasCloser ? WriterName(b.closer) : "-", writers, l.from ? l.from : "-", l.to ? l.to : "-", path, b.steps,
        c.oldOpen ? 1u : 0u, c.closesOpen ? 1u : 0u, b.forced ? 1u : 0u, b.gap ? 1u : 0u, BundleAgeMs(b));
    return Sanitize(buf, w, n);
}

// The expired destination is the from-destination (an expiry keeps the destination).
inline std::size_t FormatExpireLine(char* buf, std::size_t n, ShadowLine const& l, Bundle const& b)
{
    if (n == 0)
        return 0;
    char path[96];
    FormatPath(path, sizeof(path), b);
    int const w = std::snprintf(buf, n, "[Supervisor] bot=%u cohort=%c arbiter=1 ev=expire writer=%s reason=%s dest=%s path=%s "
        "gap=%u age_ms=%u", l.guid, l.cohort, WriterName(b.hasExpire ? b.expireWriter : b.first), ReasonFor(b),
        l.from ? l.from : "-", path, b.gap ? 1u : 0u, BundleAgeMs(b));
    return Sanitize(buf, w, n);
}

// The minute sum of one cohort. offthread_writes: writes on a thread without tag (not the map update, not the world hook, not
// the console handler); cross_writes: a bot's map thread wrote another bot's target. Neither reads the other bot's supervisor
// (its lifetime belongs to that bot's thread); the next tagged write of that bot carries gap=1.
struct ArbiterMinute
{
    std::uint32_t bundles = 0;
    std::uint32_t lines = 0;
    std::uint32_t dropped = 0;
    std::uint32_t unknown = 0;
    std::uint32_t offthreadWrites = 0;
    std::uint32_t crossWrites = 0;
    std::uint32_t gaps = 0;
    std::uint32_t closesOpen = 0;
    std::uint32_t expireCooldown = 0;
    // A3 step 3 T02-a enforcement counters (minute increments, AiPlayerbot.Supervisor.Arbiter = 2, default 0).
    std::uint32_t deny = 0;
    std::uint32_t denyProbe = 0;
    std::uint32_t heldRequest = 0;
    std::uint32_t heldErrand = 0;
    std::uint32_t probePass = 0;
    std::uint32_t probeFail = 0;
    std::uint32_t probeShadow = 0;
    std::uint32_t depthOverflow = 0;
    std::uint32_t family[kWriterIds] = {};
};

inline void CountBundle(ArbiterMinute& m, Bundle const& b)
{
    BundleClass const c = ClassifyBundle(b);
    ++m.bundles;
    if (c.unknown)
        ++m.unknown;
    if (b.gap)
        ++m.gaps;
    if (c.closesOpen)
        ++m.closesOpen;
    if (c.expireCooldown)
        ++m.expireCooldown;
    ++m.family[std::size_t(c.family)];
}

inline std::size_t FormatArbiterMin(char* buf, std::size_t n, char cohort, ArbiterMinute const& m)
{
    if (n == 0)
        return 0;
    auto const f = [&m](WriterId id) { return m.family[std::size_t(id)]; };
    int const w = std::snprintf(buf, n, "[ArbiterMin] cohort=%c bundles=%u lines=%u dropped=%u unknown=%u offthread_writes=%u "
        "cross_writes=%u gaps=%u closes_open=%u expire_cooldown=%u sel=%u req=%u exe=%u life=%u mgr=%u grp=%u cmd=%u brain=%u test=%u"
        " grp_t02=%u errand=%u deny=%u deny_probe=%u held_request=%u held_errand=%u probe_pass=%u probe_fail=%u probe_shadow=%u "
        "depth_overflow=%u",
        cohort, m.bundles, m.lines, m.dropped, m.unknown, m.offthreadWrites, m.crossWrites, m.gaps, m.closesOpen, m.expireCooldown,
        f(WriterId::Select), f(WriterId::Requester), f(WriterId::Execution), f(WriterId::Lifecycle), f(WriterId::Manager),
        f(WriterId::GroupFollow), f(WriterId::Command), f(WriterId::Brain), f(WriterId::Test), f(WriterId::Group),
        f(WriterId::Errand), m.deny, m.denyProbe, m.heldRequest, m.heldErrand, m.probePass, m.probeFail, m.probeShadow,
        m.depthOverflow);
    return Sanitize(buf, w, n);
}
}
