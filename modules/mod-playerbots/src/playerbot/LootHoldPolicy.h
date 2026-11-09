#pragma once

#include <cstdint>

class PlayerbotAI;

namespace ai::loot_hold
{
// AiPlayerbot.Loot.HoldTravelWhileLootable (twow-repo#575 H1, A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix, code default 0
// until its acceptance unit, then 1 with 0 = rollback).
//
// Loot ping-pong: "move to rpg target" and "move to travel target" stood back only while "available loot" offered a lootable
// object. That stack forgets an entry after 30 s (LootObjectStack::OrderByDistance, shrink), while the bot's "loot target"
// (set when it chose the object, cleared when it looted it or chose another) stays. A bot still walking to its loot target
// lost the stand-back, walked to its rpg / travel target, then came back for the loot. With the key both moves (and the
// rpg-target heal branch, CLI-ROT) stand back while the loot target is still open.
//
// "Open" = a loot target is set, it is within the loot distance of the bot (the same bound as the "available loot" check, so a
// bot that left the object behind is not held), LootObject::IsLootPossible still holds (object present, lootable for this
// bot, "should loot object") and the hold on this loot target is younger than AiPlayerbot.Loot.HoldTravelWhileLootable.
// MaxSeconds (OB-10 review 09.11.: "loot target" has no give-up path, so an object that stays lootable but cannot be looted -
// unreachable, a roll that does not release - would hold the bot without end; the old 30 s stack expiry was its way out).
// The clock starts at the first hold of a loot target guid; after MaxSeconds that guid holds no more (one [LootHold] ...
// result=expired line) until the bot takes another loot target.

// Pure part: the hold applies only with the key and an open loot target. The loot distance bound is inclusive.
inline bool Hold(bool key, bool lootTargetOpen)
{
    return key && lootTargetOpen;
}

inline bool WithinLootDistance(float distance, float lootDistance)
{
    return distance >= 0.0f && distance <= lootDistance;
}

// The hold limit per loot target, seconds (default 60 = 2 x the 30 s stack expiry), clamped to 10..600.
constexpr std::uint32_t DefaultMaxSeconds = 60;
inline std::uint32_t ClampMaxSeconds(int value)
{
    return value < 10 ? 10u : (value > 600 ? 600u : std::uint32_t(value));
}

// One step of the per-bot hold clock for a lootable loot target (guid != 0). heldGuid / heldSince are the bot's stored state:
// the guid whose clock runs and the time its first hold began (0 = that guid's hold already expired).
enum class Step
{
    Start,    // a new loot target: hold, clock starts now
    Keep,     // the same loot target within the limit: hold
    Expire,   // the same loot target reached the limit now: no hold, log it once, mark expired
    Expired   // the same loot target expired before: no hold, nothing logged
};

inline Step Next(std::uint64_t guid, std::uint64_t heldGuid, std::int64_t heldSince, std::int64_t now, std::uint32_t maxSeconds)
{
    if (guid != heldGuid)
        return Step::Start;
    if (heldSince == 0)
        return Step::Expired;
    if (now - heldSince < std::int64_t(maxSeconds))
        return Step::Keep;
    return Step::Expire;
}

inline bool Holds(Step step)
{
    return step == Step::Start || step == Step::Keep;
}

// The bot's loot target is open for a hold (LootObjectStack.cpp). Updates only this bot's own hold clock (its AI values
// "manual string" / "manual time", qualifier "loot hold"). Callers test the key first, so with the key off this is never
// called.
bool LootTargetOpen(PlayerbotAI* ai);
}
