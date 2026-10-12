#pragma once

// twow-repo#541 (audit A15): when PartyMemberValue::FindPartyMember may skip the
// out-of-group classification and the list scan. Pure: no game types, no state.
// AiPlayerbot.Perf.GiveItemGroupOnlyScan, default 0 = off.
namespace ai::party_scan
{
// Model of Player::IsInGroup(Player const* other) in core Player.h:
// other && GetGroup() && GetGroup() == other->GetGroup().
inline bool SharesGroup(void const* memberGroup, void const* botGroup)
{
    return memberGroup != nullptr && botGroup != nullptr && memberGroup == botGroup;
}

// Skip only with the switch on, a predicate that accepts nothing but members of
// the bot's own group, and a bot without a group: then nobody can pass.
inline bool SkipUngroupedScan(bool switchOn, bool predicateOnlyBotGroup, bool botHasGroup)
{
    return switchOn && predicateOnlyBotGroup && !botHasGroup;
}
}
