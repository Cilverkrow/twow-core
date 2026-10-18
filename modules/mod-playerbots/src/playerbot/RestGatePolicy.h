#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace ai::rest_gate
{
// twow-repo#602 ROT-01 X1 (AiPlayerbot.Rotation.RestGate): rest before the next pull for solo free bots. Pure rules, no game types,
// no locks, no config reads (the caller passes the configured thresholds).
//
// One rest episode per drop below the start line, out of combat:
//   start  HP below StartHp, or (the bot has a mana pool) mana below StartMana;
//   end    done    HP at or above TargetHp and (no mana pool or mana at or above TargetMana),
//          combat  the bot entered combat (closed on the combat-start event, review 18.10.: nothing reads the value in combat) or is
//                  seen in combat / attacked: in cohort B aggro while resting, in cohort A (applied=0) its own pull or an aggro,
//          death   the bot died (closed on the death event),
//          timeout MaxRestSeconds ran out (no food, a shapeshifted druid): the bot may pull again, and no new episode
//                  starts for RestartCooldownMs, so a bot that cannot regenerate is never held for good,
//          stale   the value was not evaluated for StaleGapMs (an inactive or parked bot): dropped without a cooldown.
// While an episode runs the bot neither pulls nor travels (can move around = false) and eats / drinks up to the targets.
struct Thresholds
{
    std::uint32_t startHp = 70;
    std::uint32_t startMana = 40;
    std::uint32_t targetHp = 90;
    std::uint32_t targetMana = 85;
    std::uint32_t maxRestSeconds = 120;
};

// Out-of-range values fall back to the defaults; a target below its start line is raised to the start line.
inline Thresholds Clamp(Thresholds t)
{
    Thresholds const d;
    if (t.startHp > 100)
        t.startHp = d.startHp;
    if (t.targetHp > 100)
        t.targetHp = d.targetHp;
    if (t.startMana > 100)
        t.startMana = d.startMana;
    if (t.targetMana > 100)
        t.targetMana = d.targetMana;
    if (t.targetHp < t.startHp)
        t.targetHp = t.startHp;
    if (t.targetMana < t.startMana)
        t.targetMana = t.startMana;
    if (t.maxRestSeconds == 0 || t.maxRestSeconds > 3600)
        t.maxRestSeconds = d.maxRestSeconds;
    return t;
}

struct Reading
{
    std::uint32_t hp = 100;     // percent
    bool manaPool = false;      // power type mana (a druid in cat / bear form has none)
    std::uint32_t mana = 100;   // percent, only meaningful with manaPool
    bool inCombat = false;
    bool attacked = false;      // the attacker set is not empty
};

enum class End : std::uint8_t { None, Done, Combat, Death, Timeout, Stale };

constexpr std::uint32_t StaleGapMs = 15000;   // longer than an inactive bot's update delay (PassiveDelay, 4-10 s)

constexpr std::uint32_t RestartCooldownMs = 60000;   // after a timeout: no new episode for a minute

inline bool Rested(Reading const& r, Thresholds const& t)
{
    return r.hp >= t.targetHp && (!r.manaPool || r.mana >= t.targetMana);
}

inline bool StartRest(Reading const& r, Thresholds const& t)
{
    return !r.inCombat && !r.attacked && (r.hp < t.startHp || (r.manaPool && r.mana < t.startMana));
}

// getMSTime math is unsigned, so the timer wrap does not end or stretch an episode.
inline End EndOf(Reading const& r, Thresholds const& t, std::uint32_t nowMs, std::uint32_t startMs)
{
    if (r.inCombat || r.attacked)
        return End::Combat;
    if (Rested(r, t))
        return End::Done;
    if (std::uint32_t(nowMs - startMs) >= t.maxRestSeconds * 1000u)
        return End::Timeout;
    return End::None;
}

inline bool RestartBlocked(bool timedOut, std::uint32_t nowMs, std::uint32_t endMs)
{
    return timedOut && std::uint32_t(nowMs - endMs) < RestartCooldownMs;
}

// The episode state of one bot (one value per bot, bot thread).
struct Episode
{
    bool open = false;          // an episode runs (applied or not)
    bool applied = false;       // the switch applies to this bot's cohort
    bool timedOut = false;      // the last episode ended by timeout (restart cooldown)
    std::uint32_t startMs = 0;
    std::uint32_t lastSeenMs = 0;   // last evaluation of the open episode
    std::uint32_t endMs = 0;
    std::uint32_t startHp = 0;
    int startMana = -1;
};

// What an evaluation or an event closed.
struct Closed
{
    bool closed = false;
    End end = End::None;
    bool applied = false;
    std::uint32_t durMs = 0;
    std::uint32_t startHp = 0;
    int startMana = -1;
};

inline Closed Close(Episode& ep, End end, std::uint32_t nowMs)
{
    Closed c;
    if (!ep.open)
        return c;
    c.closed = true;
    c.end = end;
    c.applied = ep.applied;
    // A stale episode lasted until it was last seen, not until the bot came back.
    c.durMs = (end == End::Stale ? ep.lastSeenMs : nowMs) - ep.startMs;
    c.startHp = ep.startHp;
    c.startMana = ep.startMana;
    ep.open = false;
    ep.timedOut = end == End::Timeout;
    ep.endMs = nowMs;
    return c;
}

// One evaluation of the value: closes an episode that ended (or went stale), then opens a new one when the start rule holds. applies =
// the switch applies to this bot's cohort (decided when an episode opens). Afterwards Resting(ep) says whether the bot rests now.
inline Closed Step(Episode& ep, Reading const& r, Thresholds const& t, std::uint32_t nowMs, bool applies)
{
    Closed c;
    if (ep.open)
    {
        End const end = std::uint32_t(nowMs - ep.lastSeenMs) > StaleGapMs ? End::Stale : EndOf(r, t, nowMs, ep.startMs);
        if (end == End::None)
        {
            ep.lastSeenMs = nowMs;
            return c;
        }
        c = Close(ep, end, nowMs);
    }
    if (RestartBlocked(ep.timedOut, nowMs, ep.endMs) || !StartRest(r, t))
        return c;
    ep.open = true;
    ep.applied = applies;
    ep.timedOut = false;
    ep.startMs = nowMs;
    ep.lastSeenMs = nowMs;
    ep.startHp = r.hp;
    ep.startMana = r.manaPool ? int(r.mana) : -1;
    return c;
}

inline bool Resting(Episode const& ep)
{
    return ep.open && ep.applied;
}

// Food 24005 and drink 24355 restore 2 % a second for 25 s / 30 s (SpellDuration.dbc index 63 / 9). The old duration
// (27000 ms x missing share = 270 ms per %) stood the bot up after 54 % of the gap; resting waits 500 ms per missing %,
// capped at the aura.
constexpr std::uint32_t MsPerMissingPct = 500;
constexpr std::uint32_t FoodAuraMs = 25000;
constexpr std::uint32_t DrinkAuraMs = 30000;

inline std::uint32_t DurationMs(std::uint32_t missingPct, std::uint32_t auraMs)
{
    std::uint32_t const ms = (missingPct > 100 ? 100 : missingPct) * MsPerMissingPct;
    return ms < auraMs ? ms : auraMs;
}

// A failed food / drink cast (cooldown, GCD, an aura still on) waits this long before the next try instead of retrying every tick
// (OB-10 review 18.10.2026).
constexpr std::uint32_t CastRetryMs = 2000;

inline bool CastRetryWaiting(bool failed, std::uint32_t nowMs, std::uint32_t failMs)
{
    return failed && std::uint32_t(nowMs - failMs) < CastRetryMs;
}

inline char const* EndName(End e)
{
    return e == End::Done ? "done" : e == End::Combat ? "combat" : e == End::Death ? "death" : e == End::Timeout ? "timeout" :
        e == End::Stale ? "stale" : "none";
}

// One line per finished episode. mana = -1 without a mana pool.
inline std::string FormatEpisodeLine(std::uint32_t guid, std::string const& name, std::uint32_t cls, std::uint32_t level, char cohort,
    bool applied, std::uint32_t startHp, int startMana, std::uint32_t endHp, int endMana, std::uint32_t durMs, End end, bool minimal)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf), "[RestGate] bot=%u name=%.40s cls=%u level=%u cohort=%c applied=%u start_hp=%u start_mana=%d end_hp=%u "
        "end_mana=%d dur_ms=%u reason=%s minimal=%u", guid, name.c_str(), cls, level, cohort, applied ? 1u : 0u, startHp, startMana, endHp,
        endMana, durMs, EndName(end), minimal ? 1u : 0u);
    return buf;
}

// Per class and cohort counters of one hour.
struct Counters
{
    std::uint64_t episodes = 0, done = 0, combat = 0, death = 0, timeout = 0, stale = 0, restMs = 0, castFail = 0;
    bool Empty() const { return !episodes && !castFail; }
};

inline std::string FormatTotalLine(std::uint32_t windowMin, char cohort, std::uint32_t cls, Counters const& c, std::uint32_t dropped)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf), "[RestGateTotal] window_min=%u cohort=%c cls=%u episodes=%llu done=%llu combat=%llu death=%llu "
        "timeout=%llu stale=%llu rest_s=%llu cast_fail=%llu dropped=%u", windowMin, cohort, cls, (unsigned long long)c.episodes,
        (unsigned long long)c.done, (unsigned long long)c.combat, (unsigned long long)c.death, (unsigned long long)c.timeout,
        (unsigned long long)c.stale, (unsigned long long)(c.restMs / 1000), (unsigned long long)c.castFail, dropped);
    return buf;
}
}
