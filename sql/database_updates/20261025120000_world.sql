-- twow-repo#579 follow-up (owner 25.10.2026 via OB-00: root fix for missing ghost entrances): instance maps without a valid
-- ghost entrance. A ghost entrance (map_template.ghost_entrance_map/x/y) is the spot in front of the instance portal; the core
-- uses it for the dead player's arrow to the dungeon (QueryHandler corpse query) and for the closest graveyard / go-back trigger
-- of the instance (ObjectMgr). ghost_entrance_map -1, or map 0 with x/y 0/0, counts as "no entrance" (ObjectMgr.cpp ~5633).
-- Scan of all dungeon / raid maps (map_type 1/2) on a world built from core main: five playable maps had none. The entrance is
-- taken from the instance's own exit trigger (areatrigger_teleport inside the map, target = the spot in front of the portal):
--     35 Stormwind Vault      trigger 109  -> map 0  -8679.1 /   639.3
--    807 Emerald Sanctum      trigger 5016 -> map 1   4819.1 / -1746.3
--    819 Timbermaw Hold       trigger 5614 -> map 1   4724.6 / -4911.8
--    820 Windhorn Canyon      trigger 5612 -> map 1  -5126.2 / -3532.5
--    822 Frostmane Hollow     trigger 5663 -> map 0  -5024.0 / -1443.1
-- Left out on purpose: 29 CashTest (test map), 44 / 45 Scarlet Citadel (switched off, #427), 533 Naxxramas (no exit trigger in
-- the data; entrance by teleporter), 806 Frostmane Retreat (no trigger, replaced by 822).
-- Idempotent: only rows that still have no valid entrance are changed; backup map_template_bak_ghost; end-state CHECK.
-- Rollback (exact): UPDATE `map_template` m JOIN `map_template_bak_ghost` b ON b.`entry` = m.`entry`
--   SET m.`ghost_entrance_map` = b.`ghost_entrance_map`, m.`ghost_entrance_x` = b.`ghost_entrance_x`, m.`ghost_entrance_y` = b.`ghost_entrance_y`;

CREATE TABLE IF NOT EXISTS `map_template_bak_ghost` LIKE `map_template`;
INSERT IGNORE INTO `map_template_bak_ghost`
SELECT * FROM `map_template`
 WHERE `entry` IN (35, 807, 819, 820, 822)
   AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));

UPDATE `map_template` SET `ghost_entrance_map` = 0, `ghost_entrance_x` = -8679.1, `ghost_entrance_y` = 639.3
 WHERE `entry` = 35 AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));
UPDATE `map_template` SET `ghost_entrance_map` = 1, `ghost_entrance_x` = 4819.1, `ghost_entrance_y` = -1746.3
 WHERE `entry` = 807 AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));
UPDATE `map_template` SET `ghost_entrance_map` = 1, `ghost_entrance_x` = 4724.6, `ghost_entrance_y` = -4911.8
 WHERE `entry` = 819 AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));
UPDATE `map_template` SET `ghost_entrance_map` = 1, `ghost_entrance_x` = -5126.2, `ghost_entrance_y` = -3532.5
 WHERE `entry` = 820 AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));
UPDATE `map_template` SET `ghost_entrance_map` = 0, `ghost_entrance_x` = -5024.0, `ghost_entrance_y` = -1443.1
 WHERE `entry` = 822 AND (`ghost_entrance_map` < 0 OR (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0));

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_ghost` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_ghost` (`ok`)
SELECT (SELECT COUNT(*) FROM `map_template`
         WHERE `entry` IN (35, 807, 819, 820, 822)
           AND `ghost_entrance_map` >= 0 AND NOT (`ghost_entrance_x` = 0 AND `ghost_entrance_y` = 0)) = 5;
