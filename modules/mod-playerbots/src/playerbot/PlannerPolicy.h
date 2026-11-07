#pragma once

#include <cstddef>
#include <cstdint>

// A3 step 3 (ADR-0041 + amendment 2026-11-06, OB-00 06.11.2026; AiPlayerbot.Supervisor, default 0): the activity planner's pure
// rules. Design: ws-00/projekt-durchleuchtung/A3-S3-ENTWURF.md (c98e6ded), section 1.6. The planner ranks activities by value:
//   value = XpPerMin x (1 - dangerPenalty x danger) - switchCost x [zone change] + brainBonusMax x brainPref / 255
// "Turn-in first" falls out of the value (a turn-in has a small work time), it is no relevance boost. All weights are parameters
// (a PREREG sets them), never constants here. No game types, no config reads, no locks, no log calls. Nothing in the game calls
// these yet: the planner pair does, behind the switch and with its own A/B, after the quest-book core verdict and QB1/QB3
// (OB-00 06.11.2026).
namespace ai::planner
{
enum class Kind : std::uint8_t
{
    QuestObjective = 0,
    QuestTurnIn = 1,
    QuestPickup = 2,
    Grind = 3,
    Profession = 4,
    Errand = 5,
    Rest = 6,
};

inline char const* KindName(Kind kind)
{
    switch (kind)
    {
        case Kind::QuestObjective: return "quest_objective";
        case Kind::QuestTurnIn: return "quest_turnin";
        case Kind::QuestPickup: return "quest_pickup";
        case Kind::Grind: return "grind";
        case Kind::Profession: return "profession";
        case Kind::Errand: return "errand";
        case Kind::Rest: return "rest";
    }
    return "unknown";
}

// Filled by the adapter (a later pair) from the game: numbers and ids only.
struct Candidate
{
    Kind kind = Kind::QuestObjective;
    std::uint32_t questId = 0;
    std::uint32_t entry = 0;
    std::uint32_t destId = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float xp = 0.0f;           // Quest::XPValue, or mob XP x expected kills for a grind window
    float followUpXp = 0.0f;   // follow-up quest (quest book, step 2); 0 until the quest book delivers it
    float routeSec = 0.0f;     // cached route cost
    float workSec = 0.0f;      // QB1/QB3 expected work; a parameter default until then
    float restSec = 0.0f;      // expected rest from the RestGate thresholds; a parameter
    float danger = 0.0f;       // 0..1 (danger map, destination deaths, camp block)
    bool zoneChange = false;   // the activity is in another zone than the bot
    std::uint8_t blockedReason = 0;   // != 0: not eligible (route danger, suppressed, QB2 mark, R9 close memory)
    std::uint8_t brainPref = 0;       // 0..255 bot brain preference (with a TTL, section 3 of the design)
};

struct Weights
{
    float dangerPenalty = 0.0f;   // 0..1 share of the value a fully dangerous activity loses
    float switchCost = 0.0f;      // value lost by a zone change
    float brainBonusMax = 0.0f;   // cap of the bot brain bonus (brainPref 255)
    float turnInWorkSec = 0.0f;   // the work time of a turn-in (replaces workSec for QuestTurnIn)
};

// Why the top pick won (the reason= field of the [Plan] line).
enum class Reason : std::uint8_t
{
    Alternative = 0,     // not the pick
    BestValue = 1,
    TurnInFirst = 2,     // the pick is a turn-in
    OnlyCandidate = 3,   // nothing else was eligible
};

inline char const* ReasonName(Reason reason)
{
    switch (reason)
    {
        case Reason::Alternative: return "alt";
        case Reason::BestValue: return "best_value";
        case Reason::TurnInFirst: return "turnin_first";
        case Reason::OnlyCandidate: return "only_candidate";
    }
    return "unknown";
}

struct Scored
{
    Candidate const* c = nullptr;
    float xpPerMin = 0.0f;
    float value = 0.0f;
    Reason reason = Reason::Alternative;
};

// The time base never drops below one second, so a zero route (already there) gives a finite rate.
constexpr float kMinTotalSec = 1.0f;

inline float NonNegative(float v)
{
    return v > 0.0f ? v : 0.0f;   // NaN and negatives become 0
}

inline float Clamp01(float v)
{
    if (!(v > 0.0f))
        return 0.0f;
    return v < 1.0f ? v : 1.0f;
}

inline float WorkSec(Candidate const& c, Weights const& w)
{
    return c.kind == Kind::QuestTurnIn ? NonNegative(w.turnInWorkSec) : NonNegative(c.workSec);
}

// (xp + followUpXp) per minute of route + work + rest.
inline float XpPerMin(Candidate const& c, Weights const& w)
{
    float total = NonNegative(c.routeSec) + WorkSec(c, w) + NonNegative(c.restSec);
    if (total < kMinTotalSec)
        total = kMinTotalSec;
    return (NonNegative(c.xp) + NonNegative(c.followUpXp)) * 60.0f / total;
}

inline float Value(Candidate const& c, Weights const& w)
{
    float const keep = 1.0f - Clamp01(w.dangerPenalty) * Clamp01(c.danger);
    float value = XpPerMin(c, w) * keep;
    if (c.zoneChange)
        value -= NonNegative(w.switchCost);
    value += NonNegative(w.brainBonusMax) * float(c.brainPref) / 255.0f;
    return value;
}

// Ranks the eligible candidates (blockedReason == 0, value not NaN) by value, highest first, and writes the best k into out. Equal values keep
// the input order (stable), so the same input always gives the same pick. Returns how many were written.
inline std::size_t Rank(Candidate const* in, std::size_t n, Weights const& w, Scored* out, std::size_t k)
{
    if (!in || !out || k == 0)
        return 0;
    std::size_t written = 0;
    std::size_t eligible = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        Candidate const& c = in[i];
        if (c.blockedReason)
            continue;
        Scored s;
        s.c = &c;
        s.xpPerMin = XpPerMin(c, w);
        s.value = Value(c, w);
        if (!(s.value == s.value))
            continue;   // NaN (an infinite input): not eligible
        ++eligible;
        // insertion: after every entry with a value >= this one (stable)
        std::size_t pos = written;
        while (pos > 0 && out[pos - 1].value < s.value)
            --pos;
        if (pos >= k)
            continue;
        std::size_t const last = written < k ? written : k - 1;
        for (std::size_t j = last; j > pos; --j)
            out[j] = out[j - 1];
        out[pos] = s;
        if (written < k)
            ++written;
    }
    for (std::size_t i = 0; i < written; ++i)
        out[i].reason = Reason::Alternative;
    if (written)
    {
        if (eligible == 1)
            out[0].reason = Reason::OnlyCandidate;
        else if (out[0].c->kind == Kind::QuestTurnIn)
            out[0].reason = Reason::TurnInFirst;
        else
            out[0].reason = Reason::BestValue;
    }
    return written;
}
}
