#pragma once

namespace ai::custom_cast_target
{
// twow-repo#741 (AUDIT-L03-5, R2 bug fix, no switch): CastCustomSpellAction picked the bot as target for
// `Targets & TARGET_FLAG_ITEM` or `Targets & TARGET_FLAG_SELF`. TARGET_FLAG_SELF is 0 (DBCEnums.h:131), so the second test
// was always false and a self spell (Inner Fire, a self buff) took the requester's selection, which CanCastSpell can refuse.
// `Targets == 0` is NOT "self": Fireball, Frostbolt, Smite and Sinister Strike have Targets 0 with an enemy implicit target.
// The core's own reading is SpellEntry::IsSpellWithCasterSourceTargetsOnly() (SpellEntry.h:931, flag set in SpellMgr.cpp:3390):
// every effect targets the caster or starts at the caster. Only those spells and item-target spells get the bot as target;
// every other spell keeps the selection (or the bot's own target). Pure rule, no game types.
inline bool UseBotAsTarget(bool itemTargetFlag, bool casterSourceTargetsOnly)
{
    return itemTargetFlag || casterSourceTargetsOnly;
}
}
