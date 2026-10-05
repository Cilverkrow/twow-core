#pragma once

#include <cstdint>

// Hotfix 8.10 (twow-repo#484, owner test 02.10.2026 on v24): some of our own spells that
// players learn were created as bot auras and lack SPELL_ATTR_PASSIVE. Shadow Dance
// (61143-61145) then sat in the spellbook as a castable spell: a heal animation, no proc,
// and a cast that blocked ("Another action is in progress"). The server marks these spells
// passive when it loads them, so the aura is applied at learning and login and a cast is
// refused.
// Train 9 (twow-repo#484): the data is correct since migration 20261003200000 (attributes
// | 0x40 in spell_template, client patch 8 mirrors it). The list stays as a safeguard - the
// |= is idempotent and still protects a database without that migration.
constexpr uint32_t FUNSERVER_FORCED_PASSIVE_SPELLS[] = {
    61143, 61144, 61145,    // Shadow Dance R1-R3 (Subtlety): passive proc, teaches 61146/61147 buffs
};

// Our custom spells live in 61002-65535 (train 8b renumbering; 65535 = the 16-bit client
// spell ID limit, MAX_SPELL_ID). The list must stay inside.
constexpr uint32_t FUNSERVER_CUSTOM_SPELL_MIN = 61002;
constexpr uint32_t FUNSERVER_CUSTOM_SPELL_MAX = 65535;
