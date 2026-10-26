#pragma once

class Player;

// twow-repo#629 G1 (CLI-GRP 26.10.2026): a module may keep a dead bot a corpse where the stock "auto release" would send
// its spirit to the graveyard (AutoReleaseSpiritAction::isUseful asks here, after its own dead/ghost checks).
//
// Out of line on purpose. mod-dungeon-clear once overrode the stock action NAME with its own StayDeadAction, whose
// isUseful called the stock inline isUseful from a DC-compiled TU; on 22.08.2026 the DC TUs were built without the bot
// macros (CMANGOS, MANGOSBOT_ZERO), the stock inline bodies there fell off their end (ServerFacade::UnitIsDead) and the
// map pool crashed. Here the decision runs only in code the playerbots library compiles: a module registers one plain
// function, the stock action calls it - no module-compiled copy of a stock inline class exists.
namespace ai::release_veto
{
using Predicate = bool (*)(Player* bot);

// One predicate (the last registration wins; nullptr clears). Thread-safe; called from every dead bot's own thread.
void Register(Predicate predicate);

// True when the registered predicate keeps this bot a corpse. False without a predicate.
bool Vetoed(Player* bot);
}
