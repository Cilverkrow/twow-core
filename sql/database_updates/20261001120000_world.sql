-- Train 9, owner decisions 2026-10-01 (Ä13 via OB-00): "abschalten, nicht löschen".
-- Both parts only switch content off; every original row is kept in a backup table first.
-- Replay-safe: CREATE TABLE IF NOT EXISTS, INSERT IGNORE, guarded UPDATEs.
--
-- Part A6, twow-repo#427 (Scarlet Citadel back with train 10): maps 44 "Old Scarlet Citadel"
-- and 45 "Scarlet Citadel" exist in no client Map.dbc and have no WDT (#455, OB-50), so every
-- teleport there hangs the client. Map 45 was secured in #408 (portal 112920 flags | 16,
-- game_tele 500/819 removed; that backup only lived in a comment and is now kept in the table).
-- Map 44 has no spawns; the only ways in are five GM .tele marks. They stay, but point to the
-- Scarlet Monastery entrance in Tirisfal (same spot as game_tele 215) until train 10.
-- No areatrigger_teleport, spell_target_position, script teleport (command 6) or C++ TeleportTo
-- leads to map 44/45 (read-only check on live v20, 2026-10-01).
-- Rollback A6:
--   UPDATE `game_tele` t JOIN `game_tele_bak_427` b ON b.`id` = t.`id`
--   SET t.`position_x` = b.`position_x`, t.`position_y` = b.`position_y`, t.`position_z` = b.`position_z`,
--       t.`orientation` = b.`orientation`, t.`map` = b.`map`;
-- Train 10 (maps in the client) additionally brings back the #408 marks:
--   INSERT IGNORE INTO `game_tele` SELECT * FROM `game_tele_bak_427` WHERE `map` = 45;
--
-- Part A7, twow-repo#461 (full list there): 116 Rabbit (721) spawns on 26 map-0 tiles for which
-- the client has no terrain (#455 A7). They get SPAWN_FLAG_DISABLED (0x02), which
-- Creature::LoadFromDB skips. None of them is pooled or event-bound; all had spawn_flags 0.
-- The excavation tent GO 4004476 on tile 24_42 stays (owner decision named creatures only).
-- Rollback A7:
--   UPDATE `creature` c JOIN `creature_bak_ws30_terrain` b ON b.`guid` = c.`guid`
--   SET c.`spawn_flags` = b.`spawn_flags`;

-- A6 backup: the five map-44 marks as they are, plus the two map-45 marks removed in #408.
CREATE TABLE IF NOT EXISTS `game_tele_bak_427` LIKE `game_tele`;
INSERT IGNORE INTO `game_tele_bak_427`
  SELECT * FROM `game_tele` WHERE `id` IN (621, 622, 809, 810, 827) AND `map` = 44;
INSERT IGNORE INTO `game_tele_bak_427` (`id`, `position_x`, `position_y`, `position_z`, `orientation`, `map`, `name`) VALUES
  (500, 32.5495, 13.2999, 16.869, 6.28138, 45, 'ScarletCitadel'),
  (819, 83.6082, -2.90589, 16.8695, 1.73362, 45, 'sizetest');

-- A6: map-44 marks lead to the Scarlet Monastery entrance instead of the missing map.
UPDATE `game_tele`
SET `map` = 0, `position_x` = 2872.6, `position_y` = -764.398, `position_z` = 160.332, `orientation` = 5.05735
WHERE `id` IN (621, 622, 809, 810, 827) AND `map` = 44;

-- A7 backup: full rows of the 116 spawns.
CREATE TABLE IF NOT EXISTS `creature_bak_ws30_terrain` LIKE `creature`;
INSERT IGNORE INTO `creature_bak_ws30_terrain`
  SELECT * FROM `creature` WHERE `map` = 0 AND `id` = 721 AND `guid` IN (
  38999,39047,39049,39052,39053,39054,39055,39056,39057,39058,42341,42354,
  42398,42405,42437,42442,42444,42463,42470,42473,42476,42477,42481,42482,
  42491,42497,42505,42507,42510,42511,42516,42519,42522,42528,42533,42539,
  42552,42555,42558,42564,42565,42578,42729,42730,47633,47639,47647,47660,
  47661,47662,47663,47752,47753,47756,47770,47776,47851,47852,47853,47854,
  47855,47856,47857,47858,47859,47860,47861,47862,47863,47864,47865,47866,
  47867,47869,47870,47876,47879,47880,47883,47900,47902,47903,47904,47905,
  47906,47907,47908,47909,47910,47911,47913,47915,47916,47917,47919,47920,
  47921,47922,47923,47924,47925,47926,47927,47928,47929,47931,47932,47933,
  47934,47935,47936,47937,47938,301767,301768,301769);

-- A7: switch the backed-up spawns off (only rows that really are in the backup).
UPDATE `creature` c JOIN `creature_bak_ws30_terrain` b ON b.`guid` = c.`guid`
SET c.`spawn_flags` = c.`spawn_flags` | 2;
