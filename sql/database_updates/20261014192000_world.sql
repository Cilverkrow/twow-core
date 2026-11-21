-- twow-repo#579, main train 10: quest 41796 "Far From Home" (Markus Farwind 62719, spawned in 20261014190000_world.sql)
-- cannot be completed: accepting it hands out one Farwind Scarf (41902, SrcItemId/SrcItemCount 1), but the turn-in at
-- Boris Farwind (62473) requires two (ReqItemId1 41902 x2). The quest text speaks of "these scarves" for Boris and his
-- sister Alessa, and no other source of 41902 exists (no loot, no vendor). Fix: hand out two scarves (stack size 2).
-- Found in the owner's GM check on the test realm, 11.11.2026.
-- Idempotent: only changes SrcItemCount while it is still 1; the old value is kept in a backup table. End-state CHECK.
-- Rollback (exact):
--   UPDATE `quest_template` q JOIN `quest_template_bak_41796` b ON b.`entry` = q.`entry` SET q.`SrcItemCount` = b.`SrcItemCount`;

CREATE TABLE IF NOT EXISTS `quest_template_bak_41796` (
  `entry` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY, `SrcItemCount` TINYINT UNSIGNED NOT NULL);
INSERT IGNORE INTO `quest_template_bak_41796` (`entry`, `SrcItemCount`)
SELECT `entry`, `SrcItemCount` FROM `quest_template`
 WHERE `entry` = 41796 AND `SrcItemId` = 41902 AND `SrcItemCount` = 1 AND `ReqItemId1` = 41902 AND `ReqItemCount1` = 2;
UPDATE `quest_template` SET `SrcItemCount` = 2
 WHERE `entry` = 41796 AND `SrcItemId` = 41902 AND `SrcItemCount` = 1 AND `ReqItemId1` = 41902 AND `ReqItemCount1` = 2;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_579d` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_579d` (`ok`)
SELECT (SELECT COUNT(*) FROM `quest_template`
         WHERE `entry` = 41796 AND `SrcItemId` = 41902 AND `SrcItemCount` >= `ReqItemCount1`) = 1;
