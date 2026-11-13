-- twow-repo#579 (QUEST-NPC-SPAWN-01), main train 10, batch 1: Turtle custom quest givers that have a
-- creature_template and quests but no spawn anywhere (no creature row, no script or spell summon, no pool).
-- Positions cross-checked against pfQuest-turtle (MIT, (c) Eric Mauser/Shagu) and OctoWoW map pins (2D map
-- percentages only), converted with the server's WorldMapArea.dbc; final positions verified in game (owner GM check,
-- test realm, 11.11.2026); z = height of the nearest existing spawn (<= 23 yd away),
-- orientation = facing that spawn. Only NPCs whose position is unambiguous are spawned here; the doubtful
-- ones (Fangorn, Ralwen Ivon, Nizzle, the Thorn Gorge battlemasters, NPCs without any known location) stay
-- out and are listed in #579. Every spawn was checked in game by the owner on the test realm (11.11.2026).
-- Holiday NPCs stay out as well: no permanent spawn while holidays are off (#580). Brother Raymond 63209 /
-- Hefhani 63210 (quests 42100/42101, decision OB-00 10.11.2026, twow-repo#695), Hufan Earthbow 62511 /
-- Lissandra Skysoar 62512 (Moonglade, Lunar Festival quests 41653-41655; owner GM check 11.11.2026:
-- "event quests out for now").
--   2990012 62209 Pietro Paraveno      Deadwind Pass (Karazhan quests)
--   2990013 62638 Okul                 Arathi Highlands
--   2990014 62700 Jama'nali            Un'Goro Crater
--   2990015 62719 Markus Farwind       Dustwallow Marsh
--   2990016 62726 Zalsu the Wanderer   Tanaris coast, open point of his OctoWoW wander path, random walk 10 yd
--                                      (the first path point lay between rocks; owner GM check 11.11.2026)
--   2990017 62741 Rethevus             Moonglade
--   2990018 62797 Grakk Windtamer      Arathi Highlands
--   2990019 63099 Pargan Starstrider   Bloodhoof Village, Mulgore
--   2990020 63160 Sentinel Velaya      Azshara
--   2990021 63196 Thorinn Darkgrovel   Dun Morogh
-- guids 2990012-2990021 continue the custom range (2990001-2990009: 20260926150000_world.sql, 2990010: Argur,
-- core#447; 2990011: graveyard spirit healer, 20261029200100_world.sql). Respawn 300 s as for other town quest givers.
-- Idempotent: one row per NPC, only if that NPC has no spawn yet and the guid is free; end-state CHECK.
-- Rollback (exact): DELETE FROM `creature` WHERE `guid` BETWEEN 2990012 AND 2990021;

DROP TEMPORARY TABLE IF EXISTS `tmp_spawn_579`;
CREATE TEMPORARY TABLE `tmp_spawn_579` (
  `guid` INT UNSIGNED NOT NULL PRIMARY KEY, `id` MEDIUMINT UNSIGNED NOT NULL, `map` SMALLINT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL, `y` FLOAT NOT NULL, `z` FLOAT NOT NULL, `o` FLOAT NOT NULL,
  `wander` FLOAT NOT NULL, `movement` TINYINT UNSIGNED NOT NULL);
INSERT INTO `tmp_spawn_579` (`guid`, `id`, `map`, `x`, `y`, `z`, `o`, `wander`, `movement`) VALUES
    (2990012, 62209, 0, -11175.0, -1845.83, 78.46, 3.4554, 0, 0),
    (2990013, 62638, 0, -954.0, -3526.0, 71.02, 0.4135, 0, 0),
    (2990014, 62700, 1, -7842.0, -2088.0, -271.8, 1.3808, 0, 0),
    (2990015, 62719, 1, -3857.0, -4439.0, 17.33, 5.5002, 0, 0),
    (2990016, 62726, 1, -7443.0, -2945.0, 9.57, 5.5795, 10, 1),
    (2990017, 62741, 1, 7775.76, -2895.51, 466.09, 1.7373, 0, 0),
    (2990018, 62797, 0, -1468.0, -2617.0, 48.6, 4.1707, 0, 0),
    (2990019, 63099, 1, -2304.0, -455.0, -5.36, 2.9056, 0, 0),
    (2990020, 63160, 1, 2694.0, -3888.0, 109.25, 2.4138, 0, 0),
    (2990021, 63196, 0, -6049.0, 395.0, 398.86, 1.2149, 0, 0);

INSERT INTO `creature` (`guid`, `id`, `id2`, `id3`, `id4`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                        `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `health_percent`, `mana_percent`,
                        `movement_type`, `spawn_flags`, `visibility_mod`)
SELECT s.`guid`, s.`id`, 0, 0, 0, s.`map`, s.`x`, s.`y`, s.`z`, s.`o`, 300, 300, s.`wander`, 100, 100, s.`movement`, 0, 0
  FROM `tmp_spawn_579` s
 WHERE EXISTS (SELECT 1 FROM `creature_template` t WHERE t.`entry` = s.`id`)
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE c.`id` = s.`id`)
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE c.`guid` = s.`guid`);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_579b` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_579b` (`ok`)
SELECT (SELECT COUNT(*) FROM `tmp_spawn_579` s
         WHERE (SELECT COUNT(*) FROM `creature` c WHERE c.`id` = s.`id`) >= 1) = 10
   AND (SELECT COUNT(*) FROM `creature` c JOIN `tmp_spawn_579` s ON s.`guid` = c.`guid`
         WHERE c.`id` <> s.`id`) = 0;
