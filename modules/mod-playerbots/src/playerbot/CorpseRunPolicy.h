#pragma once

namespace ai::corpse_run
{
// The death contract as the "corpse run" command sees it. A freshly dead bot
// has no corpse object yet: the core only spawns it on repop (release spirit).
// Treating "no corpse" as "alive" made the command answer "I am not dead" to a
// server-dead bot that simply had not released (#277).
enum class State
{
    Alive,
    DeadUnreleased,     // dead body, no corpse object, no ghost flag: release first
    GhostWithCorpse,    // released: run to the corpse
    GhostWithoutCorpse, // released but the corpse is gone: nothing to run to
};

inline State Classify(bool alive, bool hasCorpse, bool ghost)
{
    if (alive)
        return State::Alive;
    if (hasCorpse)
        return State::GhostWithCorpse;
    return ghost ? State::GhostWithoutCorpse : State::DeadUnreleased;
}
}
