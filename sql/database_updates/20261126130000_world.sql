-- twow-repo#638 (WS30-GHOST-ENTRANCE-CHECK-01), hotfix pair release/9.x + main (world data, ghost entrances as in
-- core#622 / #623, owner 25.10.2026): two instance maps whose ghost entrance (map_template.ghost_entrance_map/x/y) lies far
-- from the real portal. The core uses the ghost entrance for the dead player's arrow to the dungeon and for the closest
-- graveyard / go-back lookup of the instance, so a ghost (player or bot) ran to the wrong spot.
-- Owner GM check 3 on the test realm (26.11.2026, .gps at the real portal without a selected target):
--    815 Gilneas City       old map 0  -1765.49 /  1607.01 (~920 yd off, the arrow does not point to the instance)
--                           new map 0  -1089.44 /  2231.11 (portal; the exit trigger 5015 lands at -1089.9 / 2231.4)
--    814 Tower of Karazhan  old map 0 -11104.3  / -1999.42 (shared with 532, the arrow points to the Karazhan front door)
--                           new map 0 -11034.08 / -2004.36 (tower entrance)
-- 532 Lower Karazhan Halls keeps -11104.3 / -1999.42: that is its own front door.
-- Idempotent: only rows that still carry the old spot are changed; backup in map_template_bak_ghost (from core#622);
-- end-state CHECK.
-- Rollback (exact): UPDATE `map_template` m JOIN `map_template_bak_ghost` b ON b.`entry` = m.`entry`
--   SET m.`ghost_entrance_map` = b.`ghost_entrance_map`, m.`ghost_entrance_x` = b.`ghost_entrance_x`,
--       m.`ghost_entrance_y` = b.`ghost_entrance_y` WHERE m.`entry` IN (814, 815);

CREATE TABLE IF NOT EXISTS `map_template_bak_ghost` LIKE `map_template`;
INSERT IGNORE INTO `map_template_bak_ghost`
SELECT * FROM `map_template`
 WHERE (`entry` = 815 AND `ghost_entrance_map` = 0
        AND ABS(`ghost_entrance_x` + 1765.49) < 0.01 AND ABS(`ghost_entrance_y` - 1607.01) < 0.01)
    OR (`entry` = 814 AND `ghost_entrance_map` = 0
        AND ABS(`ghost_entrance_x` + 11104.3) < 0.01 AND ABS(`ghost_entrance_y` + 1999.42) < 0.01);

UPDATE `map_template` SET `ghost_entrance_x` = -1089.44, `ghost_entrance_y` = 2231.11
 WHERE `entry` = 815 AND `ghost_entrance_map` = 0
   AND ABS(`ghost_entrance_x` + 1765.49) < 0.01 AND ABS(`ghost_entrance_y` - 1607.01) < 0.01;
UPDATE `map_template` SET `ghost_entrance_x` = -11034.08, `ghost_entrance_y` = -2004.36
 WHERE `entry` = 814 AND `ghost_entrance_map` = 0
   AND ABS(`ghost_entrance_x` + 11104.3) < 0.01 AND ABS(`ghost_entrance_y` + 1999.42) < 0.01;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_ghost_638` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_ghost_638` (`ok`)
SELECT (SELECT COUNT(*) FROM `map_template`
         WHERE (`entry` = 815 AND `ghost_entrance_map` = 0
                AND ABS(`ghost_entrance_x` + 1089.44) < 0.01 AND ABS(`ghost_entrance_y` - 2231.11) < 0.01)
            OR (`entry` = 814 AND `ghost_entrance_map` = 0
                AND ABS(`ghost_entrance_x` + 11034.08) < 0.01 AND ABS(`ghost_entrance_y` + 2004.36) < 0.01)) = 2;
