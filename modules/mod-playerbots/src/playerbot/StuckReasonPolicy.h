#pragma once

#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::stuck_reason
{
// A3-HARM-UPSTREAM T40 follow-up (OB-00 (a) 16.11.2026; AiPlayerbot.ActivityTrace.StuckReason, log only, default 0): why a stuck
// bot does not start "attack anything". The action notes where each of its own evaluations returned (the isUseful gate, the
// isPossible result, the Execute result) and when; the bot stamps the start of its newest non-combat pass. One [StuckReason] line
// follows each [StuckDetail] line and reads only these records, cached values, the bot's own data (class included) and its group.
// Design note STUCKREASON-DESIGN-v1.4.md. Pure rules and plain data: no game types, no config reads, no log, no clock.

// Where the last evaluation returned.
enum class AaStage : std::uint8_t { Never, Useful, Possible, Executed };

// The gate tested last (isUseful), or the result (isPossible: Ok / NotPossible; Execute: Ok / Failed).
enum class AaGate : std::uint8_t
{
    None, Activity, RealMaster, CannotMove, NoTarget, Unsafe, TripNoGrind, HealerOrPull, OnSight, Travelling, Ok, NotPossible, Failed
};

// The bot's record of its last "attack anything" evaluation (stage 0 = never). Written only with the switch on.
struct AaRecord
{
    std::uint32_t ms = 0;
    float relevance = 0.0f;
    std::uint8_t stage = 0;
    std::uint8_t gate = 0;
};

// The start of the bot's newest non-combat pass (real-time ms) and whether it was minimal; seen 0 = none since login.
struct PassRecord
{
    std::uint32_t ms = 0;
    std::uint8_t minimal = 0;
    std::uint8_t seen = 0;
};

// Where an evaluation writes its record: null with the switch off; ms = the clock at the start of the evaluation.
struct AaSlot
{
    AaRecord* record;
    std::uint32_t ms;
};

// Scope-exit note, declared first in each evaluation body. The body sets the public gate before each test; when the body returns,
// the destructor stores the last gate (after the return value is formed). A null record: nothing is stored.
class AaNote
{
public:
    AaNote(AaSlot noteSlot, AaStage noteStage, AaGate firstGate, float noteRelevance)
        : gate(firstGate), slot(noteSlot), stage(noteStage), relevance(noteRelevance) {}
    ~AaNote()
    {
        if (slot.record != nullptr)
        {
            slot.record->ms = slot.ms;
            slot.record->relevance = relevance;
            slot.record->stage = std::uint8_t(stage);
            slot.record->gate = std::uint8_t(gate);
        }
    }
    AaNote(AaNote const&) = delete;
    AaNote& operator=(AaNote const&) = delete;

    AaGate gate;

private:
    AaSlot slot;
    AaStage stage;
    float relevance;
};

// A cached value counts as fresh when it was checked within this many seconds.
constexpr std::uint32_t FreshSeconds = 5;

// The isUseful result each gate implies: only OnSight and Ok return true.
inline bool UsefulResult(AaGate gate)
{
    return gate == AaGate::OnSight || gate == AaGate::Ok;
}

// ms since then, wrap-safe as a uint32 difference, clamped to INT32_MAX; -1 = never recorded.
inline std::int32_t AgeMs(std::uint32_t nowMs, std::uint32_t thenMs, bool seen)
{
    if (!seen)
        return -1;
    std::uint32_t const age = std::uint32_t(nowMs - thenMs);
    return age > std::uint32_t(INT32_MAX) ? std::int32_t(INT32_MAX) : std::int32_t(age);
}

// The old rule of "no target" (fires on no target or an invalid one): none = no selection, unseen = the value resolves to null
// (both fire), nc = the invalid check never calculated, stale = cached but not fresh, invalid / valid = the fresh cached check.
enum class OldRule : std::uint8_t { None, Unseen, Nc, Stale, Invalid, Valid };

inline OldRule OldRuleOf(bool hasSelection, bool resolved, bool cached, bool invalid, bool fresh)
{
    if (!hasSelection)
        return OldRule::None;
    if (!resolved)
        return OldRule::Unseen;
    if (!cached)
        return OldRule::Nc;
    if (!fresh)
        return OldRule::Stale;
    return invalid ? OldRule::Invalid : OldRule::Valid;
}

// The group leader: none = no group, self, offline = not found (also during login or a far teleport), other_map = another Map
// object, same_map = on the bot's Map object (only then is a distance read).
enum class Leader : std::uint8_t { None, Self, Offline, OtherMap, SameMap };

inline Leader LeaderOf(bool grouped, bool self, bool found, bool sameMap)
{
    if (!grouped)
        return Leader::None;
    if (self)
        return Leader::Self;
    if (!found)
        return Leader::Offline;
    return sameMap ? Leader::SameMap : Leader::OtherMap;
}

// nc_mult= (review F1): the non-combat strategies whose multiplier can zero "attack anything". stealthed: the druid's feral
// stealth zeroes it and lets "follow" through; the rogue's allows it; no other class registers it. Decode by the line's cls=.
constexpr std::uint32_t NcMultPassive = 1;
constexpr std::uint32_t NcMultWbuffTravel = 2;
constexpr std::uint32_t NcMultDungeonClear = 4;
constexpr std::uint32_t NcMultPull = 8;
constexpr std::uint32_t NcMultStealthed = 16;

inline std::uint32_t NcMultOf(bool passive, bool wbuffTravel, bool dungeonClear, bool pull, bool stealthed)
{
    return (passive ? NcMultPassive : 0u) | (wbuffTravel ? NcMultWbuffTravel : 0u) | (dungeonClear ? NcMultDungeonClear : 0u) |
        (pull ? NcMultPull : 0u) | (stealthed ? NcMultStealthed : 0u);
}

// The strategy name of one nc_mult bit (the names the line's HasStrategy reads); "" for any other value.
inline char const* NcMultName(std::uint32_t bit)
{
    switch (bit)
    {
    case NcMultPassive: return "passive";
    case NcMultWbuffTravel: return "wbuff travel";
    case NcMultDungeonClear: return "dungeon clear";
    case NcMultPull: return "pull";
    case NcMultStealthed: return "stealthed";
    default: return "";
    }
}

inline char const* StageToken(std::uint8_t stage)
{
    switch (AaStage(stage))
    {
    case AaStage::Never: return "never";
    case AaStage::Useful: return "useful";
    case AaStage::Possible: return "possible";
    case AaStage::Executed: return "executed";
    default: return "unknown";
    }
}

inline char const* GateToken(std::uint8_t gate)
{
    switch (AaGate(gate))
    {
    case AaGate::None: return "none";
    case AaGate::Activity: return "activity";
    case AaGate::RealMaster: return "real_master";
    case AaGate::CannotMove: return "cannot_move";
    case AaGate::NoTarget: return "no_target";
    case AaGate::Unsafe: return "unsafe";
    case AaGate::TripNoGrind: return "trip_no_grind";
    case AaGate::HealerOrPull: return "healer_or_pull";
    case AaGate::OnSight: return "on_sight";
    case AaGate::Travelling: return "travelling";
    case AaGate::Ok: return "ok";
    case AaGate::NotPossible: return "not_possible";
    case AaGate::Failed: return "failed";
    default: return "unknown";
    }
}

inline char const* OldRuleToken(OldRule rule)
{
    switch (rule)
    {
    case OldRule::None: return "none";
    case OldRule::Unseen: return "unseen";
    case OldRule::Nc: return "nc";
    case OldRule::Stale: return "stale";
    case OldRule::Invalid: return "invalid";
    case OldRule::Valid: return "valid";
    default: return "unknown";
    }
}

inline char const* LeaderToken(Leader leader)
{
    switch (leader)
    {
    case Leader::None: return "none";
    case Leader::Self: return "self";
    case Leader::Offline: return "offline";
    case Leader::OtherMap: return "other_map";
    case Leader::SameMap: return "same_map";
    default: return "unknown";
    }
}

// One [StuckReason] line; -1 = never / unknown. cls = the bot's class id (11 druid, 4 rogue: decodes nc_mult 16).
struct Line
{
    std::uint32_t bot = 0;
    std::uint32_t cls = 0;
    AaRecord aa;
    std::int32_t aaAgeMs = -1;
    std::int32_t passAgeMs = -1;
    std::int32_t passMinimal = -1;
    std::int32_t grind = 0;
    std::uint32_t ncMult = 0;
    OldRule oldRule = OldRule::None;
    std::int32_t cfmMax = -1;
    std::int32_t cfmFresh = -1;
    Leader leader = Leader::None;
    std::uint32_t leaderGuid = 0;
    std::uint32_t groupSize = 0;
    float leaderDist = -1.0f;
};

inline std::string FormatLine(Line const& line)
{
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer),
        "[StuckReason] bot=%u cls=%u aa_stage=%s aa_gate=%s aa_age_ms=%d aa_rel=%.2f pass_age_ms=%d pass_min=%d"
        " grind=%d nc_mult=%u old_rule=%s cfm_max=%d cfm_fresh=%d leader=%s leader_guid=%u group_size=%u leader_dist=%.0f",
        unsigned(line.bot), unsigned(line.cls), StageToken(line.aa.stage), GateToken(line.aa.gate), int(line.aaAgeMs),
        double(line.aa.relevance), int(line.passAgeMs), int(line.passMinimal), int(line.grind), unsigned(line.ncMult),
        OldRuleToken(line.oldRule), int(line.cfmMax), int(line.cfmFresh), LeaderToken(line.leader), unsigned(line.leaderGuid),
        unsigned(line.groupSize), double(line.leaderDist));
    return std::string(buffer);
}
}
