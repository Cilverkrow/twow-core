#pragma once

#include <atomic>
#include <cstdint>

namespace ai::cast_wait
{
// twow-repo#602 ROT-01 (L1-b finding 19.10.2026, OB-10; AiPlayerbot.CastWait.Preparing, default 0, BEHAVIOUR-CHANGING):
// PlayerbotAI::UpdateAI stretches the internal AI wait to the end of a running cast only while the spell is in
// SPELL_STATE_CASTING. A spell with a cast time stays in SPELL_STATE_PREPARING until it is cast (Spell::update counts
// m_timer down there, pushback adds to it), so for Smite, Lesser Heal, Frostbolt ... the wait ended after the nominal
// action duration and the combat engine re-ran every ReactDelay, getting SPELL_IN_PROGRESS for its own cast each time
// (L1-b: 281 Smite retries in 74 s). With the switch on, a PREPARING cast of the bot's own generic spell holds the
// internal AI until the spell's real end. Reactions run before that gate and are not held. Pure rules, no game types.
//
// OB-10 guards: the wait is capped by a deadline fixed once per spell from its first sighting; a wait never shortens
// a longer delay set by someone else; when the spell ends early (interrupt, cancel, target dead) the wait is released
// back to what was pending before it.

// Deadline length from the first sighting of the cast: twice the cast time, at least cast time + 1.5 s.
inline std::uint32_t CapMs(std::int32_t castTimeMs)
{
    std::uint32_t const ct = castTimeMs > 0 ? std::uint32_t(castTimeMs) : 0u;
    std::uint32_t const twice = ct * 2u;
    std::uint32_t const plus = ct + 1500u;
    return twice > plus ? twice : plus;
}

// ms from now until end on a wrapping millisecond clock (WorldTimer::getMSTime), 0 when end has passed
inline std::uint32_t Until(std::uint32_t now, std::uint32_t end)
{
    std::int32_t const d = std::int32_t(end - now);
    return d > 0 ? std::uint32_t(d) : 0u;
}

// The bot's pending cast wait. spell = identity of the Spell object (0 = none); with the cast time as a second key, so
// a new Spell at a reused address with another cast time is a new sighting (the release check normally clears the
// watch between two casts anyway).
struct Watch
{
    std::uint64_t spell = 0;
    std::int32_t castTime = 0;
    std::uint32_t deadline = 0;   // absolute ms, fixed at the first sighting
    std::uint32_t otherEnd = 0;   // absolute ms at which the delay pending before the wait would have ended
    std::uint32_t grantEnd = 0;   // absolute ms at which the last granted wait ends
    bool capped = false;          // the deadline cut this spell's wait (counted once)
};

inline bool Active(Watch const& w)
{
    return w.spell != 0;
}

struct Grant
{
    std::uint32_t delay = 0;      // new internal delay; 0 = no wait (the pass runs as before)
    bool firstSighting = false;   // a new spell: counted as one wait
    bool cappedNow = false;       // the deadline cut the wait for the first time for this spell
};

// Called when the internal AI could run and the bot's own generic spell is PREPARING with timerMs left.
// want = the spell's real rest + ReactDelay + one average world diff (the same margin as the CASTING branch).
inline Grant Extend(Watch& w, std::uint64_t spell, std::uint32_t now, std::uint32_t timerMs, std::int32_t castTimeMs,
    std::uint32_t reactDelay, std::uint32_t avgDiff, std::uint32_t currentDelay)
{
    Grant g;
    if (w.spell != spell || w.castTime != castTimeMs)
    {
        w.spell = spell;
        w.castTime = castTimeMs;
        w.deadline = now + CapMs(castTimeMs);
        w.otherEnd = now + currentDelay;
        w.capped = false;
        g.firstSighting = true;
    }
    std::uint32_t const want = timerMs + reactDelay + avgDiff;
    std::uint32_t const left = Until(now, w.deadline);
    std::uint32_t wait = want;
    if (left < want)
    {
        wait = left;
        if (!w.capped)
        {
            w.capped = true;
            g.cappedNow = true;
        }
    }
    // never shorten a delay someone else set; deadline passed (wait 0): no grant, the pass runs as before
    g.delay = wait == 0 ? 0u : (wait > currentDelay ? wait : currentDelay);
    if (g.delay)
        w.grantEnd = now + g.delay;
    return g;
}

// A pending wait ends when the bot no longer has that spell PREPARING (cast, interrupted, cancelled, replaced).
inline bool ShouldRelease(Watch const& w, std::uint64_t currentSpell, std::int32_t castTimeMs, bool preparing)
{
    return Active(w) && (currentSpell != w.spell || castTimeMs != w.castTime || !preparing);
}

// The delay after a release: what was pending before the wait. Only the wait's own part is cut: a delay that someone
// set after the grant (it ends later than the grant, 5 ms slack for the clock steps) is kept as it is.
inline std::uint32_t ReleasedDelay(Watch const& w, std::uint32_t now, std::uint32_t currentDelay)
{
    if (currentDelay > Until(now, w.grantEnd) + 5u)
        return currentDelay;
    std::uint32_t const other = Until(now, w.otherEnd);
    return other < currentDelay ? other : currentDelay;
}

inline void Clear(Watch& w)
{
    w = Watch();
}

// Estimated AI passes the wait saved: the engine would otherwise re-run every ReactDelay during it.
inline std::uint32_t SavedTicks(std::uint32_t delay, std::uint32_t reactDelay)
{
    return reactDelay ? delay / reactDelay : 0u;
}

// [CastWait] counters. Region threads add, the world thread takes them once per minute.
inline std::atomic<std::uint64_t> waits{ 0 };
inline std::atomic<std::uint64_t> waitMs{ 0 };
inline std::atomic<std::uint64_t> capped{ 0 };
inline std::atomic<std::uint64_t> released{ 0 };
inline std::atomic<std::uint64_t> savedTicks{ 0 };
}
