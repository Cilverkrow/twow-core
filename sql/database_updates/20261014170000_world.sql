-- twow-repo#583 PET-01 (owner 13.10.2026, Beast Training window empty for tamed Turtle beasts: Nightpelt Ursa 62821,
-- Moonsilk Moth 62824, ...): `pet_spell_data` (server copy of the client CreatureSpellData.dbc) lacks 33 lists that the
-- 1.18.1 client has (patch-9.mpq). 99 tameable Turtle creatures point to them through `pet_spell_list_id`, so the
-- server builds no pet-create spells for them (ObjectMgr::LoadPetCreateSpells: "for teaching work we need learn spell
-- ids") while the client expects the list. Rows taken 1:1 from the client DBC (entry, spell 1-4), read-only
-- extraction: Y:\backup twwow\workspace-relocation-20260902\evidence\WS-10\pet01-client-dbc\client\CreatureSpellData.dbc.
-- Vanilla lists (e.g. 5940 Winter Wolf) already exist and are untouched.
-- Replay-safe: INSERT IGNORE (an existing row is never changed). Rollback (exact):
--   DELETE FROM `pet_spell_data` WHERE `entry` IN (9486,9487,9488,9489,9490,9491,9492,9493,9494,9495,9496,9497,9498,9499,9500,9501,9502,9503,9504,9505,9506,9507,9508,9509,9510,9511,9512,9513,9514,9515,9516,9517,9518);
INSERT IGNORE INTO `pet_spell_data` (`entry`, `spell_id1`, `spell_id2`, `spell_id3`, `spell_id4`) VALUES
(9486, 46271, 0, 0, 0),
(9487, 46272, 0, 0, 0),
(9488, 46272, 17260, 0, 0),
(9489, 46272, 17261, 0, 0),
(9490, 46273, 0, 0, 0),
(9491, 46296, 17259, 0, 0),
(9492, 46296, 17260, 0, 0),
(9493, 24423, 16828, 0, 0),
(9494, 36533, 0, 0, 0),
(9495, 36548, 0, 0, 0),
(9496, 36549, 0, 0, 0),
(9497, 17256, 36549, 0, 0),
(9498, 36550, 0, 0, 0),
(9499, 36553, 0, 0, 0),
(9500, 36536, 0, 0, 0),
(9501, 36537, 0, 0, 0),
(9502, 36538, 0, 0, 0),
(9503, 36539, 0, 0, 0),
(9504, 36540, 0, 0, 0),
(9505, 36541, 0, 0, 0),
(9506, 36535, 0, 0, 0),
(9507, 36531, 0, 0, 0),
(9508, 36532, 0, 0, 0),
(9509, 36523, 0, 0, 0),
(9510, 36524, 0, 0, 0),
(9511, 36525, 0, 0, 0),
(9512, 3009, 36526, 0, 0),
(9513, 36552, 0, 0, 0),
(9514, 24587, 0, 0, 0),
(9515, 17260, 42051, 0, 0),
(9516, 17260, 36535, 0, 0),
(9517, 17260, 36531, 0, 0),
(9518, 17260, 1756, 0, 0);
