#pragma once

#include <cstdint>

class Player;

// Hotfix 8.4 (twow-repo#357 stage 2, owner test with Tierone 2026-10-01): talent rank
// spells whose LEARN_SPELL effects teach other spells. The talent spell is not passive,
// so nothing ever cast it and its effects never ran - a shaman with Ancestral Arms
// (61131) got neither One-Handed (201) nor Two-Handed Swords (202) nor the hub (61132).
// Train 9 replaces this with spell_learn_spell rows.
struct FunserverTalentLearn
{
    uint32_t talentSpell;
    uint32_t taught[3];
};

constexpr FunserverTalentLearn FUNSERVER_TALENT_LEARN_SPELLS[] = {
    { 61131, { 201, 202, 61132 } },  // Ancestral Arms (shaman W talent 9010)
};

// Teaches the spells of every listed talent the player knows; called after a talent is
// learned and at login.
void LearnFunserverTalentSpells(Player* player);
