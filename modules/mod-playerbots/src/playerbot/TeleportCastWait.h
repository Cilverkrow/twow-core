#pragma once

#include "Objects/Player.h"
#include "Spells/Spell.h"
#include "playerbot/TeleportCastPolicy.h"

namespace ai::teleport_cast
{
// twow-repo#333 finding 4 (TeleportCastPolicy.h): the wait for the bot's own generic spell while it PREPARES the teleport
// stepSpell asks for - the rest of the cast (Spell::m_timer, pushback included) plus one react delay, taken from the spell
// itself; 0 = not prepared.
inline std::uint32_t PreparedWaitMs(Player* bot, std::uint32_t stepSpell, std::uint32_t reactDelayMs)
{
    Spell* const spell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell || spell->getState() != SPELL_STATE_PREPARING || !SameTeleport(stepSpell, spell->m_spellInfo->Id))
        return 0;
    return WaitMs(spell->GetCastedTime(), reactDelayMs);
}
}
