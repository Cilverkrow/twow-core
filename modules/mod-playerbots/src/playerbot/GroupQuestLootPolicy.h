#pragma once

#include <cstddef>
#include <cstdint>

namespace ai::group_quest_loot
{
// AiPlayerbot.Loot.GroupQuestLoot (twow-repo#780, bug fix; OB-10 triage a2ada213, CLI-GRP plan 26.11.2026; code default 0 until the
// owner group test on realm C, then 1, 0 = rollback).
//
// A bot in a group with an active player master did not loot its own quest items (owner test, Westfall quest 40471: player 10/10,
// bots 0/10). For a party-loot quest drop every group member gets its own copy, so a bot looting it takes nothing from the player.
// Four gates blocked it, all built for "a bot trails a player":
//   1. AddAllLootAction::AddLoot: a non-leader rejects every corpse with a live hostile NPC within 30 yd of it;
//   2. AddAllLootAction::AddLoot: the corpse must lie within GroupMemberLootDistanceWithActiveMaster (10 yd) of the PLAYER;
//   3. LootAction (the loot target pick) skips a corpse more than followDistance + loot distance from the player;
//   4. the follow leash: "out of free move range" runs "follow" at ACTION_HIGH outside FollowDistance (1.5 yd) of the formation
//      point, above "move to loot" (7); FarFromCurrentLootTrigger therefore drops corpses more than followDistance + loot distance
//      from the player.
// "A quest item for this bot" (OwnPartyQuestItem): a quest drop of the corpse (Loot::m_questItems) that is party loot (freeforall,
// item flag ITEM_FLAG_PARTY_LOOT: every group member gets its own copy, so the bot takes nothing from the player), not looted, allowed
// for the bot (it has the quest and needs the item) and not yet looted by the bot itself. A non-party quest drop goes to the first
// looter, so a bot would race the player for it: never eligible (a loot rule, not this fix). PlayerbotAI::HasQuestItemsInWOLootList
// cannot be used on this core: the shim's Loot::GetLootItemsListFor returns only `items`, never the quest drops.
// With the key, and only for such a corpse:
//   (a) a hostile blocks only if it is in combat, or if its own aggro range (+ AggroMarginYards) reaches the corpse;
//   (b) the corpse must lie within LootDistance of the BOT, and the player within LootDistance of the corpse (the leash bound);
//   (c) FarFromCurrentLootTrigger keeps the loot target while the player is within LootDistance of the corpse;
//   (c') LootAction picks such a corpse as the loot target under the same bound (it had the same followDistance + loot distance
//       filter as FarFromCurrentLootTrigger);
//   (d) "out of free move range" stays quiet while that quest corpse is the loot target, the bound (b) holds, the bot is not in
//       combat, and for at most LeashHoldMaxSeconds of quiet time per corpse (a break - combat, the player beyond the bound -
//       neither restarts the clock nor counts more than QuietGapMaxSeconds, so a fight does not use the hold up); when it expires the
//       bot resets its loot target, the corpse takes the old rules (a)-(c') for this bot from then on and "follow"
//       acts as before - no follow / loot oscillation.
// Every other corpse, and every case with the key 0, takes the old path. Pure rules here: no game types, no config reads.

constexpr float AggroMarginYards = 2.0f;          // (a) a hostile this much beyond its aggro range still counts as blocking
constexpr std::int64_t LeashHoldMaxSeconds = 30;  // (d) the leash is quiet at most this long per quest corpse
constexpr std::int64_t QuietGapMaxSeconds = 5;    // (d) one step adds at most this much quiet time (a break, a slow tick)

// Is this quest drop of the corpse the bot's own party-loot copy to take? (see above)
inline bool OwnPartyQuestItem(bool partyLoot, bool lootedByAll, bool allowedForBot, bool lootedByBot)
{
    return partyLoot && !lootedByAll && allowedForBot && !lootedByBot;
}

// (a) Does this hostile near the corpse block the loot? hostileToCorpse and aggroRange in yards.
inline bool HostileBlocks(bool inCombat, float hostileToCorpse, float aggroRange)
{
    return inCombat || hostileToCorpse <= aggroRange + AggroMarginYards;
}

// (b) The range rule for a quest corpse: measured from the bot, the player bound as the leash.
inline bool QuestCorpseInRange(float botToCorpse, float masterToCorpse, float lootDistance)
{
    return botToCorpse <= lootDistance && masterToCorpse <= lootDistance;
}

// (c) The bound on the player's distance to the corpse that FarFromCurrentLootTrigger keeps the loot target under.
inline float MasterBound(bool questCorpse, float followDistance, float maxLootDistance, float lootDistance)
{
    return questCorpse ? lootDistance : followDistance + maxLootDistance;
}

// (d) The leash hold: the per-bot state of the current quest corpse (LootObjectStack, the bot's own thread). A creature keeps its
// guid over a respawn, so a hold or an expiry older than HoldForgetSeconds (Corpse.Decay.NORMAL default, the longest life of an
// unlooted corpse) belongs to an earlier corpse of that spawn and is forgotten.
constexpr std::int64_t HoldForgetSeconds = 300;

struct Hold
{
    std::uint64_t guid = 0;          // the quest corpse the leash is quiet for; 0 = none
    std::int64_t since = 0;          // when the hold began (seconds)
    std::int64_t last = 0;           // the last quiet step (seconds)
    std::int64_t quiet = 0;          // the quiet time of this hold (seconds; each step adds at most QuietGapMaxSeconds)
    std::uint64_t expiredGuid = 0;   // the last quest corpse whose hold expired: it takes the old rules for HoldForgetSeconds
    std::int64_t expiredAt = 0;      // when it expired (seconds)
};

enum class Leash
{
    Off,      // not eligible: follow as before (the state is kept: a short break does not restart the clock of that corpse)
    Start,    // a new quest corpse: the leash is quiet from now
    Hold,     // the same quest corpse, less than LeashHoldMaxSeconds quiet: the leash stays quiet
    Expire,   // LeashHoldMaxSeconds of quiet time reached just now: follow as before, the caller resets the loot target (one line)
    Expired   // an expired corpse: follow as before, no restart
};

// After its hold expired a corpse is no quest corpse for this bot any more: (a)-(c') fall back to the old rules, so
// FarFromCurrentLootTrigger drops it and LootAction does not pick it again (no follow / loot oscillation).
inline bool HoldExpired(Hold const& h, std::uint64_t guid, std::int64_t now)
{
    return guid != 0 && guid == h.expiredGuid && now - h.expiredAt < HoldForgetSeconds;
}

// eligible: the key, not in combat, a group member with an active player master, the loot target is a lootable corpse with a
// quest item for this bot on the master's map. The bound (b) is checked here.
inline Leash LeashStep(Hold& h, bool eligible, std::uint64_t guid, float botToCorpse, float masterToCorpse, float lootDistance,
                       std::int64_t now)
{
    if (!eligible || guid == 0 || !QuestCorpseInRange(botToCorpse, masterToCorpse, lootDistance))
        return Leash::Off;
    if (HoldExpired(h, guid, now))
        return Leash::Expired;
    if (h.guid != guid || now - h.since >= HoldForgetSeconds)
    {
        h.guid = guid;
        h.since = now;
        h.last = now;
        h.quiet = 0;
        return Leash::Start;
    }
    std::int64_t gap = now - h.last;
    h.quiet += gap < 0 ? 0 : (gap > QuietGapMaxSeconds ? QuietGapMaxSeconds : gap);
    h.last = now;
    if (h.quiet < LeashHoldMaxSeconds)
        return Leash::Hold;
    h.expiredGuid = guid;
    h.expiredAt = now;
    return Leash::Expire;
}

inline bool LeashQuiet(Leash step)
{
    return step == Leash::Start || step == Leash::Hold;
}

// The effect line of AddLoot: once per corpse and bot. The available-loot list forgets an entry after 30 s and "add all loot"
// adds it again, so without this a corpse would log every 30 s. The last SeenSize corpses are remembered (the bot's own thread).
constexpr std::size_t SeenSize = 8;

struct Seen
{
    std::uint64_t guids[SeenSize] = {};
    std::size_t next = 0;

    // true the first time a guid is met (it is remembered then); false while it is among the last SeenSize remembered
    bool Insert(std::uint64_t guid)
    {
        for (std::uint64_t g : guids)
            if (g == guid)
                return false;
        guids[next] = guid;
        next = (next + 1) % SeenSize;
        return true;
    }
};
}
