-- twow-repo#579 (QUEST-NPC-SPAWN-01), main train: Argur Rustbrand (63188), turn-in of 42034 "To Fit a Mold" and
-- giver of 42035 "Lock and Loaded" (rogue chain of Lord Jorach Ravenholdt), had a template but no spawn anywhere
-- (no creature row, no script summon, no summoning spell, no pool), so the chain broke at 42034.
-- Quest text: "the Mold Foundry of the Blackrock Depths". The owner placed him by hand on live (GM .npc add,
-- 2026-10-11) and confirmed the spot ("da sollte er auch für die Quest stehen"): guid 2990010, map 230,
-- 1144.44 / -197.937 / -65.308, o 4.73722, respawn 120 s. This file adopts exactly that row, so live keeps its
-- hand-placed row and every other DB (fresh bootstrap, test realm) gets the same spawn. guid 2990010 continues the
-- custom spawn range 2990001-2990009 (20260926150000_world.sql).
-- Idempotent: inserts only if no creature with id 63188 exists and guid 2990010 is free; end-state CHECK requires
-- exactly one spawn of 63188 (fails if guid 2990010 were taken by another creature).
-- Rollback (exact; also removes the owner's live row, so only on an owner decision):
--   DELETE FROM `creature` WHERE `guid` = 2990010 AND `id` = 63188;

INSERT INTO `creature` (`guid`, `id`, `id2`, `id3`, `id4`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                        `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `health_percent`, `mana_percent`,
                        `movement_type`, `spawn_flags`, `visibility_mod`)
SELECT 2990010, 63188, 0, 0, 0, 230, 1144.44, -197.937, -65.308, 4.73722, 120, 120, 0, 100, 100, 0, 0, 0
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `creature` WHERE `id` = 63188)
   AND NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 2990010);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_579` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_579` (`ok`)
SELECT (SELECT COUNT(*) FROM `creature` WHERE `id` = 63188) = 1
   AND (SELECT COUNT(*) FROM `creature` WHERE `guid` = 2990010 AND `id` = 63188 AND `map` = 230) = 1;
