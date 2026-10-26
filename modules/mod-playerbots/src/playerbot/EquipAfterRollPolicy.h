#pragma once

// twow-repo#341 (AiPlayerbot.Equip.AfterRollWon, owner repro 25.10.2026 on C): bots never put on gear they won by Need/Greed.
// The core stores a won roll item (Group::CountTheRoll: StoreNewItem + OnReceivedItem) without SendNewItem, so the bot never
// gets SMSG_ITEM_PUSH_RESULT - the packet whose "item push result" trigger runs "equip upgrades" for looted gear - and it had
// no handler for SMSG_LOOT_ROLL_WON (sent to every voter). With the switch that packet runs "equip upgrades" too.
namespace ai::equip_after_roll
{
// "equip upgrades" for a SMSG_LOOT_ROLL_WON: the switch on, this bot the winner (the packet goes to every voter, the player
// included - nobody else's item is touched) and the item gear (weapon, armour, bag: the push-result rule).
inline bool Equips(bool switchOn, bool ownWin, bool gear)
{
    return switchOn && ownWin && gear;
}
}
