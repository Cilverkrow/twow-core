#pragma once

#include <atomic>
#include <cstdint>

#include "CohortPolicy.h"

namespace ai::stale_target
{
// A3-HARM-UPSTREAM H6 (T40) (AiPlayerbot.Rotation.ReleaseStaleTarget, default 0 = the old rule; idea only: cmangos/playerbots
// 21b8aef2 + f6f1d3f, no code taken). A bot out of combat can keep a valid "current target" that nothing acts on: the grind pick took
// the pull path and returned before "attack target" was set (ChooseTargetActions.cpp:165-174 before :179), the pull ended and cleared
// only "pull target" (PullStrategy.cpp:226-230), or "attack target" was reset by travel (ChooseTravelTargetAction.cpp:1502,
// WorldBuffTravelActions.cpp:698, QuestGroupWorld.cpp:141). "combat end" then switches the engine back while "current target" stays,
// and "no target" never fires again. With the switch the bot drops ONLY that "current target" after StoppedSeconds standing still or
// StillSeconds without moving MoveYards, so "no target" -> "attack anything" re-picks. Pure rules, no game types, no config reads.

// Named constants (design note H6-DESIGN.md v1.1, 4.2).
constexpr std::uint32_t DefaultStillSeconds = 60;    // f6f1d3f: no position change for 60 s
constexpr std::uint32_t DefaultStoppedSeconds = 5;   // 21b8aef2 fires at once; 5 s grace
constexpr std::uint32_t GapMs = 5000;                // an unobserved gap longer than this never counts as standing
constexpr float MoveYards = 5.0f;                    // = BotActivityPolicy.h StuckYards and the TooCloseDistance default
constexpr std::uint32_t FollowWindowMs = 30000;      // re_same / moved_on are decided within 30 s after a release

// The switch acts on this bot: mode 1 = every bot, 2 = cohort B only (CohortPolicy.h). The loader clamps > 2 to 0, so mode 3 never
// reaches this rule.
inline bool Applies(std::uint32_t mode, std::uint32_t guidLow)
{
    return ai::cohort::Applies(mode, guidLow);
}

// Shadow (AiPlayerbot.Rotation.ReleaseStaleTarget.Shadow = 1): the mode is on, but this bot is the control cohort - it only counts.
inline bool ShadowApplies(std::uint32_t mode, std::uint32_t shadow, std::uint32_t guidLow)
{
    return shadow == 1 && mode != 0 && !Applies(mode, guidLow);
}

// 0 = cohort A (even low guid), 1 = cohort B (odd low guid): the index of the A/B counter pairs.
inline unsigned CohortIndex(std::uint32_t guidLow)
{
    return ai::cohort::InB(guidLow) ? 1u : 0u;
}

// Not eligible at all: the clock is cleared. In combat (core flag), dead, a real player master (AttackAnythingAction::isUseful is
// false for it, ChooseTargetActions.cpp:36), taxi, transport, duel, battleground.
inline bool IsReset(bool inCombat, bool alive, bool realPlayer, bool taxi, bool transport, bool duel, bool battleground)
{
    return inCombat || !alive || realPlayer || taxi || transport || duel || battleground;
}

// The holds: the clock keeps running, the release waits. Read only when a clause is due. Order: Pull, Wait, Loot, Cast, Rest.
enum class Hold : std::uint8_t { None = 0, Pull = 1, Wait = 2, Loot = 3, Cast = 4, Rest = 5 };
constexpr unsigned HoldCount = 6;

inline Hold HoldOf(bool pullRunning, bool waitForAttack, bool lootOpen, bool casting, bool resting)
{
    if (pullRunning)
        return Hold::Pull;
    if (waitForAttack)
        return Hold::Wait;
    if (lootOpen)
        return Hold::Loot;
    if (casting)
        return Hold::Cast;
    if (resting)
        return Hold::Rest;
    return Hold::None;
}

// Rest = sitting, eating or drinking. A MOD_REGEN / MOD_POWER_REGEN aura counts only with the core's AURA_INTERRUPT_FLAG_NOT_SEATED
// (SpellDefines.h; the core's own eating test, SpellAuras.cpp:6468/6483): food and drink. Blessing of Wisdom and mp5 / hp5 gear use
// the same aura types without that flag and are up all the time - counted as Rest they would block the release for good (v1.2 F1).
constexpr std::uint32_t NotSeatedInterruptFlag = 0x00040000u;

inline bool IsEatOrDrinkAura(std::uint32_t auraInterruptFlags)
{
    return (auraInterruptFlags & NotSeatedInterruptFlag) != 0;
}

// One clock per bot (a member of the bot's "no target" trigger). Its own anchor and times, separate running flags (no 0 sentinel).
// Times are WorldTimer ms; uint32 differences are wrap-safe.
struct Clock
{
    bool running = false;          // an episode is observed (a target, no Reset)
    std::uint64_t target = 0;      // the raw guid of the current target of this episode
    std::uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;   // the anchor of the still clock
    std::uint32_t lastSeenMs = 0;
    bool stillRunning = false;
    std::uint32_t stillSinceMs = 0;
    bool stoppedRunning = false;
    std::uint32_t stoppedSinceMs = 0;
    bool holding = false;          // the last due pass was held
    std::uint8_t heldMask = 0;     // the hold kinds already counted in this episode (once per episode)
    bool shadowCounted = false;    // the shadow verdict was counted in this episode (once per episode)
};

// The last WorldTimer minute a bot was counted in elig_bot_min (per bot, kept across episodes).
struct MinuteMark
{
    bool counted = false;
    std::uint32_t minute = 0;
};

inline void Clear(Clock& c)
{
    c = Clock();
}

inline float Dist2(float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

// Restart BOTH clocks at this place and time (anchor = here). The stopped clock starts only while the bot is stopped. A restart also
// ends a hold (a held pass never counts as standing).
inline void Restart(Clock& c, std::uint32_t nowMs, float x, float y, float z, bool stopped)
{
    c.holding = false;
    c.x = x;
    c.y = y;
    c.z = z;
    c.stillRunning = true;
    c.stillSinceMs = nowMs;
    c.stoppedRunning = stopped;
    c.stoppedSinceMs = nowMs;
}

enum class Observed : std::uint8_t { Continued, NewEpisode, Restarted };

// One observation of an eligible bot (target set and valid, no Reset). A new target (or the first observation) opens a new episode;
// a map change, a gap > GapMs or a move >= MoveYards from the anchor restarts both clocks; otherwise the still clock runs on and the
// stopped clock runs only while stopped (a moving pass stops it, the next stopped pass starts it again).
inline Observed Observe(Clock& c, std::uint32_t nowMs, std::uint64_t target, std::uint32_t mapId, float x, float y, float z, bool stopped)
{
    Observed result = Observed::Continued;
    if (!c.running || c.target != target)
    {
        Clear(c);
        c.running = true;
        c.target = target;
        c.mapId = mapId;
        Restart(c, nowMs, x, y, z, stopped);
        result = Observed::NewEpisode;
    }
    else if (c.mapId != mapId || std::uint32_t(nowMs - c.lastSeenMs) > GapMs || Dist2(x, y, z, c.x, c.y, c.z) >= MoveYards * MoveYards)
    {
        c.mapId = mapId;
        Restart(c, nowMs, x, y, z, stopped);
        result = Observed::Restarted;
    }
    else if (!stopped)
        c.stoppedRunning = false;
    else if (!c.stoppedRunning)
    {
        c.stoppedRunning = true;
        c.stoppedSinceMs = nowMs;
    }
    c.lastSeenMs = nowMs;
    return result;
}

// The end of a hold restarts both clocks (a hold never counts as standing).
inline bool EndHold(Clock& c, std::uint32_t nowMs, float x, float y, float z, bool stopped)
{
    if (!c.holding)
        return false;
    c.holding = false;
    Restart(c, nowMs, x, y, z, stopped);
    return true;
}

// True once per episode and hold kind: the caller counts held_<kind>.
inline bool NoteHold(Clock& c, Hold hold)
{
    if (hold == Hold::None)
        return false;
    c.holding = true;
    std::uint8_t const bit = std::uint8_t(1u << unsigned(hold));
    if (c.heldMask & bit)
        return false;
    c.heldMask = std::uint8_t(c.heldMask | bit);
    return true;
}

// True once per episode: the caller counts the shadow verdict.
inline bool NoteShadow(Clock& c)
{
    if (c.shadowCounted)
        return false;
    c.shadowCounted = true;
    return true;
}

// True once per bot and WorldTimer minute: the caller counts one elig_bot_min.
inline bool NoteEligibleMinute(MinuteMark& m, std::uint32_t nowMs)
{
    std::uint32_t const minute = nowMs / 60000u;
    if (m.counted && m.minute == minute)
        return false;
    m.counted = true;
    m.minute = minute;
    return true;
}

enum class Due : std::uint8_t { None, Still, Stopped };

// Still wins when both are due. A clause with 0 seconds is off.
inline Due DueOf(Clock const& c, std::uint32_t nowMs, std::uint32_t stillSeconds, std::uint32_t stoppedSeconds)
{
    if (!c.running)
        return Due::None;
    if (stillSeconds > 0 && c.stillRunning && std::uint32_t(nowMs - c.stillSinceMs) >= stillSeconds * 1000u)
        return Due::Still;
    if (stoppedSeconds > 0 && c.stoppedRunning && std::uint32_t(nowMs - c.stoppedSinceMs) >= stoppedSeconds * 1000u)
        return Due::Stopped;
    return Due::None;
}

// Release only for an acting bot, a due clause and no hold.
inline bool Release(bool applies, Due due, Hold hold)
{
    return applies && due != Due::None && hold == Hold::None;
}

// The follow-up of the last release: re_same = the same guid is the current target again, moved_on = a different current target or the
// bot moved >= MoveYards (or changed the map) - both only within FollowWindowMs; after that the follow-up closes without a count.
struct FollowUp
{
    bool open = false;
    std::uint64_t target = 0;
    std::uint32_t sinceMs = 0;
    std::uint32_t mapId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

enum class Follow : std::uint8_t { Open, ReSame, MovedOn, Lapsed };

inline void OpenFollowUp(FollowUp& f, std::uint64_t target, std::uint32_t nowMs, std::uint32_t mapId, float x, float y, float z)
{
    f.open = true;
    f.target = target;
    f.sinceMs = nowMs;
    f.mapId = mapId;
    f.x = x;
    f.y = y;
    f.z = z;
}

// currentTarget = the raw guid of the current target now (0 = none). Closes the follow-up unless the result is Open.
inline Follow FollowOf(FollowUp& f, std::uint32_t nowMs, std::uint64_t currentTarget, std::uint32_t mapId, float x, float y, float z)
{
    if (!f.open)
        return Follow::Lapsed;
    Follow result = Follow::Open;
    if (std::uint32_t(nowMs - f.sinceMs) > FollowWindowMs)
        result = Follow::Lapsed;
    else if (currentTarget != 0 && currentTarget == f.target)
        result = Follow::ReSame;
    else if (currentTarget != 0 || mapId != f.mapId || Dist2(x, y, z, f.x, f.y, f.z) >= MoveYards * MoveYards)
        result = Follow::MovedOn;
    if (result != Follow::Open)
        f.open = false;
    return result;
}

// [StaleTarget] counters, one per cohort ([0] = A, [1] = B). Map threads add, the world thread takes them once per minute (exchange).
inline std::atomic<std::uint64_t> eligBotMin[2]{};   // bot-minutes with a valid out-of-combat current target and no Reset
inline std::atomic<std::uint64_t> released[2]{};     // releases (= byStill + byStopped)
inline std::atomic<std::uint64_t> byStill[2]{};
inline std::atomic<std::uint64_t> byStopped[2]{};
inline std::atomic<std::uint64_t> shadow[2]{};       // control-cohort episodes that would have released (once per episode)
inline std::atomic<std::uint64_t> reSame[2]{};       // the released guid is the current target again within 30 s
inline std::atomic<std::uint64_t> movedOn[2]{};      // a different current target or a move >= MoveYards within 30 s
inline std::atomic<std::uint64_t> held[HoldCount][2]{};   // [Hold] once per episode; [0] (None) unused
}
