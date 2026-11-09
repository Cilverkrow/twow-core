#pragma once

#include <cstdint>

namespace ai::transport_samemap
{
// AiPlayerbot.Path.TransportSameMapGuard (twow-repo#575 H4, A3-HARM-UPSTREAM, OB-00 09.11.2026; bug fix,
// code default 0 until its acceptance unit, then 1 with 0 = rollback). Pure rule, no game types, no config reads, no logging.
//
// MovementAction::UseTransport, not aboard, scanned the vessels of the dock's map (WorldPosition::getTransports: the map's
// transport list, the elevator spawns of that map) before it tested whether the dock lies on the bot's map. A bot updates
// in its own map's update thread; the dock's map, when it is another one, is updated by another thread, so the scan read
// another map's transport set and its vessels while that thread could change them (crash class). Nothing on another map
// can be boarded anyway: the legacy boarding test, FixBoardOpen and the deck probe all need the dock on the bot's map.
//
// With the guard the scan runs only for a dock on the bot's map. A dock on another map waits as before ("different map"),
// with no candidate vessel: the [TransportWait] trace counts the wait as before (cand=0, near=-1 on that path instead of
// values read from the other map's thread) and the deck probe (S3b-0) does not probe a vessel of another map.

// True = UseTransport may scan the dock's vessels. Guard off (0): always, as before.
inline bool ScanAllowed(bool guard, std::uint32_t dockMap, std::uint32_t botMap)
{
    return !guard || dockMap == botMap;
}
}
