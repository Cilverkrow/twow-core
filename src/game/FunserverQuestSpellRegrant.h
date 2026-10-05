#pragma once

#include <cstdint>

class Player;

// Hotfix 8.23 (twow-repo#527): quest rewards whose spell never arrived. Quest 40348 "Way of
// Spiritwalking" taught the non-existent spell 47262 until hotfix 8.21; tauren shamans who
// had already finished it never got their racial Ethereal Form 45502. At login a character
// of the listed race and class who has the quest rewarded learns the missing spell once
// (idempotent through HasSpell, no character migration).
struct FunserverQuestSpellRegrant
{
    uint32_t questId;
    uint32_t raceMask;
    uint32_t classMask;
    uint32_t spellId;
};

constexpr FunserverQuestSpellRegrant FUNSERVER_QUEST_SPELL_REGRANTS[] = {
    { 40348, 1u << (6 - 1), 1u << (7 - 1), 45502 },  // tauren shaman: Ethereal Form
};

// Teaches every missing spell of the table; called at login.
void RegrantFunserverQuestSpells(Player* player);
