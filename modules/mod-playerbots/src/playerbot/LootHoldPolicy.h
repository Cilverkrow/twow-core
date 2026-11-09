#pragma once

class PlayerbotAI;

namespace ai::loot_hold
{
// AiPlayerbot.Loot.HoldTravelWhileLootable (twow-repo#575 H1, A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix, code default 0
// until its acceptance unit, then 1 with 0 = rollback).
//
// Loot ping-pong: "move to rpg target" and "move to travel target" stood back only while "available loot" offered a lootable
// object. That stack forgets an entry after 30 s (LootObjectStack::OrderByDistance, shrink), while the bot's "loot target"
// (set when it chose the object, cleared when it looted it or gave it up) stays. A bot still walking to its loot target lost
// the stand-back, walked to its rpg / travel target, then came back for the loot. With the key both moves (and the rpg-target
// heal branch, CLI-ROT) stand back while the loot target is still open.
//
// "Open" = a loot target is set, it is within the loot distance of the bot (the same bound as the "available loot" check, so a
// bot that left the object behind is not held) and LootObject::IsLootPossible still holds (object present, lootable for this
// bot, "should loot object").

// Pure part: the hold applies only with the key and an open loot target. The loot distance bound is inclusive.
inline bool Hold(bool key, bool lootTargetOpen)
{
    return key && lootTargetOpen;
}

inline bool WithinLootDistance(float distance, float lootDistance)
{
    return distance >= 0.0f && distance <= lootDistance;
}

// The bot's loot target is open (LootObjectStack.cpp). Callers test the key first, so with the key off this is never called.
bool LootTargetOpen(PlayerbotAI* ai);
}
