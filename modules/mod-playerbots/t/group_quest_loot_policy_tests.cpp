#include "GroupQuestLootPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Require(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace ai::group_quest_loot;
    std::uint64_t const a = 0xF130000000123456ull, b = 0xF130000000654321ull;
    float const lootDistance = 25.0f;   // AiPlayerbot.LootDistance default

    // The constants of the plan (CLI-GRP 26.11.2026, OB-00 OK).
    Require(AggroMarginYards == 2.0f && LeashHoldMaxSeconds == 30, "constants");

    // Own party-loot quest drop only: a non-party drop goes to the first looter (never eligible); looted by all / by the bot or not
    // allowed for the bot (no quest, enough items) is not eligible either.
    Require(OwnPartyQuestItem(true, false, true, false), "party loot, open, allowed, not taken by the bot: eligible");
    Require(!OwnPartyQuestItem(false, false, true, false), "non-party quest drop: never (the bot would race the player)");
    Require(!OwnPartyQuestItem(true, true, true, false), "looted: no");
    Require(!OwnPartyQuestItem(true, false, false, false), "not allowed for the bot: no");
    Require(!OwnPartyQuestItem(true, false, true, true), "the bot took its copy already: no");

    // (a) Hostile gate: in combat -> blocks; idle outside its aggro range (+ margin) -> does not block; idle inside -> blocks.
    Require(HostileBlocks(true, 29.0f, 10.0f), "a hostile in combat blocks at any distance");
    Require(!HostileBlocks(false, 25.0f, 15.0f), "an idle hostile 25 yd off with 15 yd aggro range does not block");
    Require(HostileBlocks(false, 12.0f, 15.0f), "an idle hostile inside its aggro range blocks");
    Require(HostileBlocks(false, 17.0f, 15.0f), "the margin: 2 yd beyond the aggro range still blocks");
    Require(!HostileBlocks(false, 17.5f, 15.0f), "just beyond aggro range + margin does not block");

    // (b) Range for a quest corpse: from the bot, the player within LootDistance of the corpse as the bound.
    Require(QuestCorpseInRange(20.0f, 22.0f, lootDistance), "corpse 20 yd from the bot, player 22 yd: allowed");
    Require(!QuestCorpseInRange(20.0f, 30.0f, lootDistance), "player 30 yd from the corpse: not allowed (leash bound)");
    Require(!QuestCorpseInRange(26.0f, 5.0f, lootDistance), "corpse beyond LootDistance of the bot: not allowed");
    Require(QuestCorpseInRange(25.0f, 25.0f, lootDistance), "the bound itself is allowed");

    // (c) FarFromCurrentLootTrigger bound: any other corpse keeps followDistance + loot distance (the old rule).
    Require(MasterBound(false, 1.5f, 5.0f, lootDistance) == 6.5f, "other corpse / key 0: followDistance + loot distance");
    Require(MasterBound(true, 1.5f, 5.0f, lootDistance) == lootDistance, "quest corpse: LootDistance");

    // (d) Leash hold: off when not eligible or out of bound; start on a new corpse; hold < 30 s; then expired. A break (combat, the
    //     player beyond the bound) does not restart the clock of the same corpse: the cap is per corpse.
    {
        Hold h;
        Require(LeashStep(h, false, a, 10.0f, 10.0f, lootDistance, 1000) == Leash::Off && h.guid == 0, "not eligible: off");
        Require(!LeashQuiet(Leash::Off), "off is not quiet");
        Require(LeashStep(h, true, 0, 10.0f, 10.0f, lootDistance, 1000) == Leash::Off, "no corpse guid: off");
        Require(LeashStep(h, true, a, 10.0f, 10.0f, lootDistance, 1000) == Leash::Start && h.guid == a && h.since == 1000,
            "a new quest corpse: start");
        Require(LeashQuiet(Leash::Start) && LeashQuiet(Leash::Hold), "start and hold are quiet");
        Require(LeashStep(h, true, a, 8.0f, 30.0f, lootDistance, 1010) == Leash::Off && h.guid == a && h.since == 1000,
            "the player beyond the bound: off, the state kept");
        Require(LeashStep(h, false, a, 8.0f, 12.0f, lootDistance, 1015) == Leash::Off && h.guid == a, "combat: off, the state kept");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1029) == Leash::Hold && h.since == 1000 && h.quiet == QuietGapMaxSeconds,
            "back after a 29 s break: the same hold, the break counts QuietGapMaxSeconds");
        Require(!HoldExpired(h, a, 1029), "not expired while held");
        for (std::int64_t t = 1030; t < 1029 + LeashHoldMaxSeconds - QuietGapMaxSeconds; ++t)
            Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, t) == Leash::Hold, "quiet steps 1 s apart: hold");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1054) == Leash::Expire && h.quiet == LeashHoldMaxSeconds
            && h.expiredGuid == a && h.expiredAt == 1054, "30 s of quiet time: expire (once)");
        Require(!LeashQuiet(Leash::Expire) && !LeashQuiet(Leash::Expired), "expire / expired are not quiet: follow acts");
        Require(HoldExpired(h, a, 1055) && !HoldExpired(h, b, 1055) && !HoldExpired(h, 0, 1055),
            "the expired corpse takes the old rules");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1100) == Leash::Expired && h.since == 1000,
            "the same corpse stays expired (no restart, no second line)");
        Require(LeashStep(h, true, b, 8.0f, 12.0f, lootDistance, 1101) == Leash::Start && h.guid == b && h.since == 1101,
            "another quest corpse: a new start");
        Require(HoldExpired(h, a, 1102), "the expired corpse stays expired while another one is held");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1102) == Leash::Expired && h.guid == b,
            "back to the expired corpse: no new hold, the hold of b untouched");
        // A respawn keeps the guid: after HoldForgetSeconds the expiry and the old hold belong to an earlier corpse.
        Require(HoldExpired(h, a, 1054 + HoldForgetSeconds - 1) && !HoldExpired(h, a, 1054 + HoldForgetSeconds),
            "the expiry is forgotten after HoldForgetSeconds");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1054 + HoldForgetSeconds) == Leash::Start && h.guid == a
            && h.quiet == 0, "a later corpse of the same spawn: a new start");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 1054 + HoldForgetSeconds + 5) == Leash::Hold, "and holds");
        Hold g;
        Require(LeashStep(g, true, a, 8.0f, 12.0f, lootDistance, 2000) == Leash::Start, "a hold of a");
        Require(LeashStep(g, false, a, 8.0f, 12.0f, lootDistance, 2010) == Leash::Off, "a break");
        Require(LeashStep(g, true, a, 8.0f, 12.0f, lootDistance, 2000 + HoldForgetSeconds) == Leash::Start,
            "a hold older than HoldForgetSeconds (never expired) is an earlier corpse: a new start");
    }

    // (d) A fight does not use the hold up: hold 5 s, a 35 s fight, back -> still held; a slow tick adds at most 5 s.
    {
        Hold h;
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 3000) == Leash::Start, "start");
        for (std::int64_t t = 3001; t <= 3005; ++t)
            Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, t) == Leash::Hold, "5 s quiet");
        for (std::int64_t t = 3006; t <= 3040; ++t)
            Require(LeashStep(h, false, a, 8.0f, 12.0f, lootDistance, t) == Leash::Off, "35 s combat: off");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 3041) == Leash::Hold && h.quiet == 5 + QuietGapMaxSeconds,
            "after the fight: still held, the fight counted QuietGapMaxSeconds");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 3041) == Leash::Hold && h.quiet == 10, "same second: adds 0");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 3061) == Leash::Hold && h.quiet == 15, "a 20 s tick adds 5");
        Require(LeashStep(h, true, a, 8.0f, 12.0f, lootDistance, 3050) == Leash::Hold && h.quiet == 15, "a clock step back adds 0");
    }

    // The effect line: once per corpse among the last SeenSize corpses.
    {
        Seen s;
        Require(s.Insert(a) && !s.Insert(a), "first time logs, the second does not");
        Require(s.Insert(b) && !s.Insert(a) && !s.Insert(b), "two corpses, each once");
        for (std::uint64_t g = 1; g <= SeenSize; ++g)
            Require(s.Insert(0x100 + g), "fill the ring");
        Require(s.Insert(a), "after SeenSize newer corpses an old one may log again");
    }

    std::cout << "group_quest_loot_policy_tests: OK\n";
    return 0;
}
