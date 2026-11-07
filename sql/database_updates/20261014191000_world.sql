-- twow-repo#579, main train 10: quest 42036 "Secrets of Ten Thousand Years" (rogue chain of Lord Jorach
-- Ravenholdt, 42029 -> 42036) cannot be completed. Four defects (analysis in #579):
--   1. 42035 "Lock and Loaded" takes the Elven Elementium Lock 42317 at turn-in, 42036 needs it again;
--   2. the Runed Elementium Key 42318 that 42036 needs has no source at all;
--   3. 42036 names StartScript 42036, but quest_start_scripts has no rows for it;
--   4. the turn-in object, Ancient Elven Chest 2020340, is not spawned anywhere.
-- Fix: the missing start script hands out lock and key when 42036 is accepted (SCRIPT_COMMAND_CREATE_ITEM 17;
-- source = quest giver, the item goes to the player), and the chest gets one spawn in Ravenholdt Manor.
-- Both items are quest items of 42036 (ReqSourceId), so abandoning the quest removes them and accepting it
-- again hands them out again. Players already stuck: abandon 42036 and accept it again (no DB repair).
-- Chest position: PROVISIONAL, behind Lord Jorach Ravenholdt in his upstairs room (Jorach 27.6 / -1625.3 /
-- 204.0). The quest text says "upstairs in the tower of my manor"; the exact spot is confirmed or corrected
-- with a GM .gps on the test realm before merge (no data source has the original position).
-- gameobject guid 2990001 opens a custom range for gameobjects (no gameobject guid 2990000-2999999 exists).
-- Idempotent: each script row only if 42036 does not create that item yet; chest only if 2020340 has no spawn and the guid is free.
-- Rollback (exact): DELETE FROM `quest_start_scripts` WHERE `id` = 42036 AND `command` = 17 AND `datalong` IN (42317, 42318);
--                   DELETE FROM `gameobject` WHERE `guid` = 2990001 AND `id` = 2020340;

INSERT INTO `quest_start_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `datalong4`,
                                   `target_param1`, `target_param2`, `target_type`, `data_flags`, `dataint`, `dataint2`,
                                   `dataint3`, `dataint4`, `x`, `y`, `z`, `o`, `condition_id`, `comments`)
SELECT 42036, 0, 0, 17, 42317, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       'Secrets of Ten Thousand Years: Player - Create Item Elven Elementium Lock (twow-repo#579)'
  FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `quest_start_scripts` WHERE `id` = 42036 AND `datalong` = 42317);
INSERT INTO `quest_start_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `datalong4`,
                                   `target_param1`, `target_param2`, `target_type`, `data_flags`, `dataint`, `dataint2`,
                                   `dataint3`, `dataint4`, `x`, `y`, `z`, `o`, `condition_id`, `comments`)
SELECT 42036, 0, 1, 17, 42318, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       'Secrets of Ten Thousand Years: Player - Create Item Runed Elementium Key (twow-repo#579)'
  FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `quest_start_scripts` WHERE `id` = 42036 AND `datalong` = 42318);

INSERT INTO `gameobject` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                          `rotation0`, `rotation1`, `rotation2`, `rotation3`, `spawntimesecsmin`, `spawntimesecsmax`,
                          `animprogress`, `state`, `spawn_flags`, `visibility_mod`)
SELECT 2990001, 2020340, 0, 30.0, -1625.3, 204.0, 3.12414, 0, 0, 0.999962, 0.008726, 180, 180, 100, 1, 0, 0
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `gameobject` WHERE `id` = 2020340)
   AND NOT EXISTS (SELECT 1 FROM `gameobject` WHERE `guid` = 2990001);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_579c` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_579c` (`ok`)
SELECT (SELECT COUNT(*) FROM `quest_start_scripts` WHERE `id` = 42036 AND `command` = 17 AND `datalong` IN (42317, 42318)) = 2
   AND (SELECT COUNT(*) FROM `quest_template` WHERE `entry` = 42036 AND `StartScript` = 42036) = 1
   AND (SELECT COUNT(*) FROM `gameobject` WHERE `id` = 2020340) >= 1
   AND (SELECT COUNT(*) FROM `gameobject_involvedrelation` WHERE `id` = 2020340 AND `quest` = 42036) = 1;
