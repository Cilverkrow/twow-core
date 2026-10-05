#pragma once

#include <cstdint>

// Train 9 (twow-repo#484, owner test 03.10.2026 after hotfix 8.10: Storm Wisdom "funktioniert
// gar nicht"): stacking spell-mod buffs that the next affected cast consumes as a whole stack.
// Aura::HandleAddModifier gives a spell mod no charges when the spell stacks (StackAmount > 1),
// so such a buff never expired at use and lasted its full duration on every cast. For the
// spells listed here the mod gets one charge: the next affected cast drops it, and
// Player::RemoveSpellMods removes the whole aura (all stacks); a failed cast restores it.
constexpr uint32_t FUNSERVER_CONSUME_ON_USE_STACK_MODS[] = {
    61124,  // Storm Wisdom buff: Lightning Bolt, -20 % cast time and mana cost per stack (5)
    61126,  // Storm Wisdom buff with Chain Storm: Lightning Bolt and Chain Lightning
};

inline bool IsFunserverConsumeOnUseStackMod(uint32_t spellId)
{
    for (uint32_t id : FUNSERVER_CONSUME_ON_USE_STACK_MODS)
        if (id == spellId)
            return true;
    return false;
}
