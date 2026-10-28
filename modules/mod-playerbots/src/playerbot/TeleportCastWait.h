#pragma once

#include "Objects/Player.h"
#include "Spells/Spell.h"
#include "playerbot/PlayerbotAI.h"
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

// The hold of an action that started the teleport or waits for a prepared one: the duration to set (HoldMs), with the watch
// armed that ends it in PlayerbotAI::UpdateAI as soon as the spell is no longer PREPARING.
inline std::uint32_t HoldFor(PlayerbotAI* ai, Player* bot, std::uint32_t stepSpell, std::uint32_t reactDelayMs)
{
    std::uint32_t const waitMs = PreparedWaitMs(bot, stepSpell, reactDelayMs);
    ai->WatchTeleportHold(waitMs);
    return HoldMs(waitMs, reactDelayMs);
}
}
