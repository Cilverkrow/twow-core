-- twow-repo#579 (QUEST-NPC-SPAWN-01), main train 10, batch 1: Turtle custom quest givers that have a
-- creature_template and quests but no spawn anywhere (no creature row, no script or spell summon, no pool).
-- Positions: map coordinates from the pfQuest-turtle database (MIT, (c) Eric Mauser) and OctoWoW (AoWoW),
-- converted with the server's WorldMapArea.dbc; z = height of the nearest existing spawn (<= 23 yd away),
-- orientation = facing that spawn. Only NPCs whose position is unambiguous are spawned here; the doubtful
-- ones (Fangorn, Ralwen Ivon, Nizzle, the Thorn Gorge battlemasters, NPCs without any known location) stay
-- out and are listed in #579. Every spawn is checked in game on the test realm before merge.
--   2990011 62209 Pietro Paraveno      Deadwind Pass (Karazhan quests)
--   2990012 62511 Hufan Earthbow       Moonglade
--   2990013 62512 Lissandra Skysoar    Moonglade
--   2990014 62638 Okul                 Arathi Highlands
--   2990015 62700 Jama'nali            Un'Goro Crater
--   2990016 62719 Markus Farwind       Dustwallow Marsh
--   2990017 62726 Zalsu the Wanderer   Tanaris (first point of his path)
--   2990018 62741 Rethevus             Moonglade
--   2990019 62797 Grakk Windtamer      Arathi Highlands
--   2990020 63099 Pargan Starstrider   Bloodhoof Village, Mulgore
--   2990021 63160 Sentinel Velaya      Azshara
--   2990022 63196 Thorinn Darkgrovel   Dun Morogh
--   2990023 63209 Brother Raymond      Elwynn Forest
--   2990024 63210 Hefhani              Bloodhoof Village, Mulgore
-- guids 2990011-2990024 continue the custom range (2990001-2990009: 20260926150000_world.sql, 2990010: Argur,
-- core#447). Respawn 300 s as for other town quest givers.
-- Idempotent: one row per NPC, only if that NPC has no spawn yet and the guid is free; end-state CHECK.
-- Rollback (exact): DELETE FROM `creature` WHERE `guid` BETWEEN 2990011 AND 2990024;

DROP TEMPORARY TABLE IF EXISTS `tmp_spawn_579`;
CREATE TEMPORARY TABLE `tmp_spawn_579` (
  `guid` INT UNSIGNED NOT NULL PRIMARY KEY, `id` MEDIUMINT UNSIGNED NOT NULL, `map` SMALLINT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL, `y` FLOAT NOT NULL, `z` FLOAT NOT NULL, `o` FLOAT NOT NULL);
INSERT INTO `tmp_spawn_579` (`guid`, `id`, `map`, `x`, `y`, `z`, `o`) VALUES
    (2990011, 62209, 0, -11175.0, -1845.83, 78.46, 3.4554),
    (2990012, 62511, 1, 7937.0, -2638.0, 492.94, 6.2257),
    (2990013, 62512, 1, 7891.0, -2552.0, 487.11, 3.3777),
    (2990014, 62638, 0, -954.0, -3526.0, 71.02, 0.4135),
    (2990015, 62700, 1, -7842.0, -2088.0, -271.8, 1.3808),
    (2990016, 62719, 1, -3857.0, -4439.0, 17.33, 5.5002),
    (2990017, 62726, 1, -7514.0, -2748.0, 11.77, 5.5795),
    (2990018, 62741, 1, 7775.76, -2895.51, 466.09, 1.7373),
    (2990019, 62797, 0, -1468.0, -2617.0, 48.6, 4.1707),
    (2990020, 63099, 1, -2304.0, -455.0, -5.36, 2.9056),
    (2990021, 63160, 1, 2694.0, -3888.0, 109.25, 2.4138),
    (2990022, 63196, 0, -6049.0, 395.0, 398.86, 1.2149),
    (2990023, 63209, 0, -9498.0, 61.0, 56.01, 2.0352),
    (2990024, 63210, 1, -2166.0, -397.0, -3.8, 2.6799);

INSERT INTO `creature` (`guid`, `id`, `id2`, `id3`, `id4`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                        `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `health_percent`, `mana_percent`,
                        `movement_type`, `spawn_flags`, `visibility_mod`)
SELECT s.`guid`, s.`id`, 0, 0, 0, s.`map`, s.`x`, s.`y`, s.`z`, s.`o`, 300, 300, 0, 100, 100, 0, 0, 0
  FROM `tmp_spawn_579` s
 WHERE EXISTS (SELECT 1 FROM `creature_template` t WHERE t.`entry` = s.`id`)
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE c.`id` = s.`id`)
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE c.`guid` = s.`guid`);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_579b` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_579b` (`ok`)
SELECT (SELECT COUNT(*) FROM `tmp_spawn_579` s
         WHERE (SELECT COUNT(*) FROM `creature` c WHERE c.`id` = s.`id`) >= 1) = 14
   AND (SELECT COUNT(*) FROM `creature` c JOIN `tmp_spawn_579` s ON s.`guid` = c.`guid`
         WHERE c.`id` <> s.`id`) = 0;
