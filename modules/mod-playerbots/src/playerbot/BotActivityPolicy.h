#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ai::bot_activity
{
// twow-repo#601 R01 L1 (owner decision D1 19.10.2026 "rum stehen ist keine Option", OB-00 D10, AiPlayerbot.ActivityTrace):
// what a roster bot does with its time. Once a minute the bot's own state is sorted into one kind - parked, dead, in
// combat, stuck (travelling to a target but not moving), at a grind target without a fight, travelling to or working at
// a quest target, at a grind target, at any other target (vendor, trainer, gather, explore, a band trip), or idle (none
// of these: no target, or a target in cooldown / expired). The seconds since the last sample go to that kind. A gap
// longer than MaxCreditSeconds (the bot was not updated) is counted as gap, never as a kind. One [BotActivity] line per
// bot and clock hour (seconds per kind) and one [BotActivityMin] line per minute with the totals per 5-level band. Only
// values the bot already holds: no search, no per-creature value, no lock. Pure (std only), so the unit test and the
// source contract pin it without game headers.
// OB-10 review 19.10.2026: an active target hides a stall (L1-b: Fristan 4+ h at a grind target without a kill, the
// Azora bots 40-60 min in TRAVEL to an unreachable taker), so a target in TRAVEL with less than StuckYards moved since
// the last sample is stuck, and a grind target in WORK without any combat since the last sample is grind_idle.
enum class Kind : std::uint8_t
{
    Quest = 0,  // travel target is a quest giver, objective or taker, and active (prepare / ready / travel / work)
    Grind,      // travel target is a grind destination, active, with combat since the last sample (or not yet at work)
    GrindIdle,  // grind destination in WORK, no combat at any update since the last sample
    Stuck,      // an active target in TRAVEL, not on a taxi, moved less than StuckYards since the last sample
    Combat,     // in combat (whatever the target)
    Other,      // any other active travel target, or a taxi flight without one
    Idle,       // alive, not in combat, no active travel target, not flying
    Dead,       // dead or ghost
    Parked,     // parked roster bot (rndbot park, sleeping at an inn)
    Group,      // twow-repo#627: follower in an S7 help group (no own travel target, follows its leader)
    Count
};

enum class Dest : std::uint8_t
{
    None = 0,   // no travel target, or the null destination
    Quest,
    Grind,
    Other
};

constexpr std::size_t KindCount = std::size_t(Kind::Count);
constexpr std::uint32_t SampleSeconds = 60;
constexpr std::uint32_t MaxCreditSeconds = 120;
constexpr std::uint32_t HourSeconds = 3600;
constexpr float StuckYards = 5.0f;
constexpr std::uint8_t StatusTravel = 3;   // TravelMgr.h TRAVEL_STATUS_TRAVEL
constexpr std::uint8_t StatusWork = 4;     // TravelMgr.h TRAVEL_STATUS_WORK
constexpr std::size_t Bands = 13;   // L0-4, L5-9, L10-14, ..., L55-59, L60 (the R01 bands 10-14 / 15-19 / 20-24)

inline std::size_t BandOf(std::uint32_t level)
{
    std::size_t const band = std::size_t(level / 5);
    return band < Bands ? band : Bands - 1;
}

inline char const* KindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Quest: return "quest";
        case Kind::Grind: return "grind";
        case Kind::GrindIdle: return "grind_idle";
        case Kind::Stuck: return "stuck";
        case Kind::Combat: return "combat";
        case Kind::Other: return "other";
        case Kind::Idle: return "idle";
        case Kind::Dead: return "dead";
        case Kind::Parked: return "parked";
        case Kind::Group: return "group";
        default: return "none";
    }
}

// TravelTarget status (TravelMgr.h TravelStatus): PREPARE 1, READY 2, TRAVEL 3, WORK 4 are active; NONE 0, COOLDOWN 5
// ("free to do what the bot wants") and EXPIRED 6 are not.
inline bool TravelActive(std::uint8_t status)
{
    return status >= 1 && status <= 4;
}

// The bot's state at one sample. moved = yards since the previous sample (2D; a map change or no previous sample counts
// as far), combatSinceSample = in combat at any update since the previous sample.
struct State
{
    bool alive = true;
    bool parked = false;
    bool inCombat = false;
    bool combatSinceSample = false;
    bool taxiFlying = false;
    bool helpFollower = false;   // twow-repo#627: follower in an S7 help group
    std::uint8_t status = 0;   // TravelTarget status, 0 without a target
    Dest dest = Dest::None;
    float moved = 1e9f;
};

inline Kind Classify(State const& s)
{
    if (s.parked)
        return Kind::Parked;
    if (!s.alive)
        return Kind::Dead;
    if (s.inCombat)
        return Kind::Combat;
    if (s.helpFollower)
        return Kind::Group;   // twow-repo#627: no own target, so neither idle nor stuck
    if (TravelActive(s.status) && s.dest != Dest::None)
    {
        if (s.status == StatusTravel && !s.taxiFlying && s.moved < StuckYards)
            return Kind::Stuck;
        if (s.dest == Dest::Grind)
            return s.status == StatusWork && !s.combatSinceSample ? Kind::GrindIdle : Kind::Grind;
        return s.dest == Dest::Quest ? Kind::Quest : Kind::Other;
    }
    if (s.taxiFlying)
        return Kind::Other;
    return Kind::Idle;
}

// Per bot (its own update thread only). The first sample after login credits nothing; each later one credits the
// seconds since the previous one to the kind seen now (at most a minute's error at a change). The hour of a sample is
// time / 3600; a sample in a new hour first closes the old one. NoteCombat runs at every gated update (one bool), the
// position is kept at each sample.
struct Clock
{
    std::uint32_t lastSample = 0;
    std::uint32_t hour = 0;
    std::array<std::uint32_t, KindCount> seconds{};
    std::uint32_t gap = 0;
    bool combatSeen = false;
    bool hasPos = false;
    std::uint32_t lastMap = 0;
    float lastX = 0.0f;
    float lastY = 0.0f;

    void NoteCombat(bool inCombat) { combatSeen = combatSeen || inCombat; }

    // yards since the previous sample's position (far without one or after a map change); keeps this position and
    // clears the combat note for the next interval
    float Move(std::uint32_t map, float x, float y)
    {
        float const moved = hasPos && map == lastMap ? std::sqrt((x - lastX) * (x - lastX) + (y - lastY) * (y - lastY)) : 1e9f;
        hasPos = true;
        lastMap = map;
        lastX = x;
        lastY = y;
        return moved;
    }

    bool TakeCombat()
    {
        bool const seen = combatSeen;
        combatSeen = false;
        return seen;
    }

    bool Due(std::uint32_t now) const { return !lastSample || now - lastSample >= SampleSeconds; }

    bool HourDone(std::uint32_t now) const { return lastSample && now / HourSeconds != hour; }

    void StartHour(std::uint32_t now)
    {
        hour = now / HourSeconds;
        seconds = {};
        gap = 0;
    }

    // -> the seconds credited to kind (0: first sample or a gap)
    std::uint32_t Add(Kind kind, std::uint32_t now)
    {
        if (!lastSample)
        {
            lastSample = now;
            StartHour(now);
            return 0;
        }
        std::uint32_t const elapsed = now - lastSample;
        lastSample = now;
        if (elapsed > MaxCreditSeconds)
        {
            gap += elapsed;
            return 0;
        }
        if (kind < Kind::Count)
            seconds[std::size_t(kind)] += elapsed;
        return elapsed;
    }
};

// "[BotActivity] bot=12 level=14 zone=40 hour=493512 quest=1800 grind=300 grind_idle=0 stuck=60 combat=840 other=120
// idle=420 dead=0 parked=0 group=0 gap=0" - seconds of the clock hour `hour` (unix time / 3600).
inline std::string FormatLine(std::uint32_t bot, std::uint32_t level, std::uint32_t zone, Clock const& c)
{
    std::string line = "[BotActivity] bot=" + std::to_string(bot) + " level=" + std::to_string(level) + " zone=" +
        std::to_string(zone) + " hour=" + std::to_string(c.hour);
    for (std::size_t i = 0; i < KindCount; ++i)
        line += std::string(" ") + KindName(Kind(i)) + "=" + std::to_string(c.seconds[i]);
    line += " gap=" + std::to_string(c.gap);
    return line;
}

// twow-repo#601 (AiPlayerbot.ActivityTrace.StuckDetail, OB-00 26.10.2026): in cand3-C the quest cohort was stuck 21 % of its
// time, and no log said why (no position or reason per minute). With the switch a bot that is stuck StuckDetailSamples
// samples in a row writes one [StuckDetail] line for the episode; a sample of any other kind ends it. At most
// StuckDetailPerMinute lines a minute for all bots (about: two threads at a minute change may each pass once more); a refused
// line is counted and the next line carries the count (dropped=). Log only.
constexpr std::uint8_t StuckDetailSamples = 2;
constexpr std::uint32_t StuckDetailPerMinute = 30;

// Per bot (its own update thread only).
struct StuckEpisode
{
    std::uint8_t samples = 0;

    // -> true at the sample that makes the episode (StuckDetailSamples stuck samples in a row), once per episode
    bool Note(bool stuck)
    {
        if (!stuck)
        {
            samples = 0;
            return false;
        }
        if (samples < 255)
            ++samples;
        return samples == StuckDetailSamples;
    }
};

// Any thread. Relaxed atomics only, no lock.
class StuckDetailLimit
{
public:
    // -> true while the minute of `now` has a line left
    bool Take(std::uint32_t now)
    {
        std::uint32_t const key = now / SampleSeconds + 1;   // 0 = no minute yet
        std::uint32_t seen = minute.load(std::memory_order_relaxed);
        if (seen != key && minute.compare_exchange_strong(seen, key, std::memory_order_relaxed))
            used.store(0, std::memory_order_relaxed);
        if (used.fetch_add(1, std::memory_order_relaxed) < StuckDetailPerMinute)
            return true;
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // -> the lines refused since the last call
    std::uint32_t TakeDropped() { return dropped.exchange(0, std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> minute{ 0 };
    std::atomic<std::uint32_t> used{ 0 };
    std::atomic<std::uint32_t> dropped{ 0 };
};

inline StuckDetailLimit& StuckDetailGlobal()
{
    static StuckDetailLimit limit;
    return limit;
}

// What the engine last did, from its trail (Engine::LogAction: "|PUSH:<action> - <relevance> (<kind>)|T:<trigger>|...|A:<action>
// - OK [<n>us]"; a trigger is logged after the actions it pushed). action = the newest executed action, trigger = the first trigger
// after the newest push of that action before it. "-" when the trail does not hold one. Double quotes become single ones.
struct TrailPick
{
    std::string action = "-";
    std::string trigger = "-";
};

inline TrailPick PickFromTrail(std::string const& trail)
{
    std::vector<std::string> entries;
    for (std::size_t start = 0; start <= trail.size();)
    {
        std::size_t const end = trail.find('|', start);
        std::size_t const stop = end == std::string::npos ? trail.size() : end;
        if (stop > start)
            entries.push_back(trail.substr(start, stop - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    TrailPick pick;
    std::size_t done = entries.size();
    for (std::size_t i = entries.size(); i-- > 0;)
    {
        std::size_t const ok = entries[i].find(" - OK");
        if (entries[i].compare(0, 2, "A:") == 0 && ok != std::string::npos)
        {
            pick.action = entries[i].substr(2, ok - 2);
            done = i;
            break;
        }
    }
    if (done == entries.size())
        return pick;
    std::string const push = "PUSH:" + pick.action + " - ";
    for (std::size_t i = done; i-- > 0;)
    {
        if (entries[i].compare(0, push.size(), push) != 0)
            continue;
        for (std::size_t j = i + 1; j < done; ++j)
            if (entries[j].compare(0, 2, "T:") == 0)
            {
                pick.trigger = entries[j].substr(2);
                break;
            }
        break;
    }
    for (std::string* text : { &pick.action, &pick.trigger })
        for (char& c : *text)
            if (c == '"')
                c = '\'';
    return pick;
}

struct Snapshot
{
    std::array<std::array<std::uint32_t, KindCount>, Bands> seconds{};
    std::array<std::uint32_t, Bands> samples{};
};

// Any thread (the bots' map threads add, the world thread takes once a minute). Relaxed atomics only, no lock.
class Counters
{
public:
    void Add(std::size_t band, Kind kind, std::uint32_t credited)
    {
        if (band >= Bands || kind >= Kind::Count)
            return;
        sampleCount[band].fetch_add(1, std::memory_order_relaxed);
        kindSeconds[band][std::size_t(kind)].fetch_add(credited, std::memory_order_relaxed);
    }

    Snapshot Take()
    {
        Snapshot s;
        for (std::size_t b = 0; b < Bands; ++b)
        {
            s.samples[b] = sampleCount[b].exchange(0, std::memory_order_relaxed);
            for (std::size_t i = 0; i < KindCount; ++i)
                s.seconds[b][i] = kindSeconds[b][i].exchange(0, std::memory_order_relaxed);
        }
        return s;
    }

private:
    std::array<std::atomic<std::uint32_t>, Bands> sampleCount{};
    std::array<std::array<std::atomic<std::uint32_t>, KindCount>, Bands> kindSeconds{};
};

inline Counters& Global()
{
    static Counters counters;
    return counters;
}

// "[BotActivityMin] L10-14=n/quest/grind/grind_idle/stuck/combat/other/idle/dead/parked/group L15-19=..." - per band with
// samples this minute: the sample count and the credited seconds per kind. One line a minute; empty bands are left out.
inline std::string FormatMinuteLine(Snapshot const& s)
{
    std::string line = "[BotActivityMin]";
    for (std::size_t b = 0; b < Bands; ++b)
    {
        if (!s.samples[b])
            continue;
        line += " L" + std::to_string(b * 5) + (b + 1 < Bands ? "-" + std::to_string(b * 5 + 4) : std::string("+")) + "=" +
            std::to_string(s.samples[b]);
        for (std::size_t i = 0; i < KindCount; ++i)
            line += "/" + std::to_string(s.seconds[b][i]);
    }
    return line;
}
}
