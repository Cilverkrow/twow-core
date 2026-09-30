#pragma once

namespace ai::shaman_weapons
{
// twow-repo#357 stage 2, S2-7: the Enhancement weapon talent "Ancestral Arms"
// (61131, core#217) teaches shamans One-Handed Swords (skill 43) and Two-Handed
// Swords (skill 55). The bot gear filter knew swords for no shaman, so a bot
// with the talent never got one from the factory. A sword is allowed exactly
// when the bot has the matching skill - without the talent nothing changes.
inline bool SwordAllowed(bool twoHanded, bool hasSwordSkill, bool hasTwoHandedSwordSkill)
{
    return twoHanded ? hasTwoHandedSwordSkill : hasSwordSkill;
}
}
