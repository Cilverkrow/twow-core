#ifndef TW_FUNSERVER_PLAYER_SNARE_H
#define TW_FUNSERVER_PLAYER_SNARE_H

class WorldObject;

// twow-repo#295 (owner 2026-10-02): slows and roots of players and bots, also through their
// pets, totems and traps, are stronger; NPC casts stay unchanged. The scaling rules are pure
// (FunserverRidingStages.h). This is the caster rule shared by the slow hook
// (WorldObject::CalculateSpellDamage) and the root hook (Spell::DoSpellHitOnUnit).
namespace FunserverSnare
{
    // True when the caster is player-controlled (WorldObject::IsControlledByPlayer: a player or
    // bot, a unit owned or charmed by a player, a trap owned by a player, an area effect cast by
    // a player) and is not a unit charmed by a non-player. IsControlledByPlayer() is always true
    // for a Player, so without the charm test a player or bot mind-controlled by an NPC (Dominate
    // Mind, Chains of Kel'Thuzad, Cause Insanity) would snare like a player with the spells the
    // NPC AI makes it cast. Defined in Object.cpp.
    bool IsPlayerSnareCaster(WorldObject const* caster);
}

#endif
