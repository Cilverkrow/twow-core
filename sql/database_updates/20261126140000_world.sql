-- twow-repo#579, main train (Ä15: new world spawn, not travel data): quest 41859 "After All This Time" (Turtle, L28,
-- started by the Rusty Locket 2020240 near Northwind) cannot be completed. Its only turn-in is the Withered Cross
-- 2020241 (gameobject_involvedrelation), and that object has no spawn anywhere.
-- Fix: one spawn of the Withered Cross in Elwynn Forest near Fargodeep Mine, at the quest target of Questie (the turn-in
-- text speaks of a grave in Elwynn). Position read in game by the owner with .gps at that spot (GM check 3 on the test
-- realm, 26.11.2026): map 0, -10052.51 / 181.99, ground z 27.99, orientation 2.624671. The Questie point itself is
-- not checked against other Turtle data; the GM check of the spawned cross follows on the test realm.
-- gameobject guid 2990002 continues the custom gameobject range (2990001: Ancient Elven Chest, core#459); no gameobject
-- guid 2990000-2999999 exists on main.
-- Idempotent: only if 2020241 has no spawn yet and the guid is free; end-state CHECK.
-- Rollback (exact): DELETE FROM `gameobject` WHERE `guid` = 2990002 AND `id` = 2020241;

INSERT INTO `gameobject` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                          `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecsmin`, `spawntimesecsmax`,
                          `animprogress`, `state`, `spawn_flags`, `visibility_mod`)
SELECT 2990002, 2020241, 0, -10052.51, 181.99, 27.99, 2.624671, 0, 0, 0.966785, 0.255593, 300, 300, 100, 1, 0, 0
  FROM DUAL
 WHERE EXISTS (SELECT 1 FROM `gameobject_template` WHERE `entry` = 2020241)
   AND NOT EXISTS (SELECT 1 FROM `gameobject` WHERE `id` = 2020241)
   AND NOT EXISTS (SELECT 1 FROM `gameobject` WHERE `guid` = 2990002);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_41859` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_41859` (`ok`)
SELECT (SELECT COUNT(*) FROM `gameobject` WHERE `id` = 2020241) >= 1
   AND (SELECT COUNT(*) FROM `gameobject` WHERE `guid` = 2990002 AND `id` <> 2020241) = 0
   AND (SELECT COUNT(*) FROM `gameobject_involvedrelation` WHERE `id` = 2020241 AND `quest` = 41859) = 1;
