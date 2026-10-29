-- twow-repo#575 F9 / F-b V1 (owner 29.10.2026 via OB-00: "Setzte bitte v1 und v2 um."; Ä17 classification "Zwischen-Update",
-- like #622): graveyard 149 on Blackstone Island (map 1, zone 5536, spirit healer 2581569 at -120.0 / -7552.2) stands inside
-- a mob camp. F9-2 (arm A / B): 46 % / 28 % of the graveyard revives died again within 180 s; 9 hostile or neutral spawns
-- stood within 60 yd (the nearest 33 yd), 8 more within 60-80 yd (CAUSE.md, evidence/ws-10/cli-pathfinding/goblin-island-b/
-- f9/f9-2-harm). These 17 spawns move into their own camps, at least 96 yd from the graveyard point (-124.92 / -7550.25),
-- toward the mining pit (miners, overseers, mine-bots, floor z ~13) and the north-east / south plateau (scorpids, buzzards,
-- Battletrons) - none toward the friendly hub in the north-west. Only position_x/y/z change: entry, faction, orientation,
-- wander distance and movement type stay. None of the 17 is pooled, linked, event-bound, grouped or has waypoints
-- (creature_movement); three miners keep their mining emote (creature_addon).
-- Left as it is (owner/OB-00 29.10., D3): Mudpaw Overseer 2579748 patrols in the pit 25 yd below the graveyard (nearest
-- waypoint 62 yd); F9-3 measures its share of the re-deaths.
-- Guard: a spawn is only moved while it still stands within 80 yd of the graveyard point, so a replay or a world that has
-- these guids elsewhere changes nothing. Backup creature_bak_gy149 (full rows, as before the move); end-state CHECK.
-- Rollback (exact): UPDATE `creature` c JOIN `creature_bak_gy149` b ON b.`guid` = c.`guid`
--   SET c.`position_x` = b.`position_x`, c.`position_y` = b.`position_y`, c.`position_z` = b.`position_z`;

CREATE TABLE IF NOT EXISTS `creature_bak_gy149` LIKE `creature`;
INSERT IGNORE INTO `creature_bak_gy149`
SELECT * FROM `creature`
 WHERE `map` = 1 AND `guid` IN (2579593, 2579596, 2579591, 2579597, 2579740, 2579735, 2579734, 2579739, 2579746, 2579733, 2579744, 2579745, 2579743, 2579716, 2579721, 2579592, 2579594)
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;

-- 2579593 Stonetail Scorpid (was -158.71 / -7563.43 / 44.63, 36 yd -> 104 yd)
UPDATE `creature` SET `position_x` = -84.00, `position_y` = -7455.00, `position_z` = 47.60
 WHERE `guid` = 2579593 AND `id` = 61660 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579596 Stonetail Scorpid (was -169.67 / -7509.83 / 44.90, 60 yd -> 117 yd)
UPDATE `creature` SET `position_x` = -50.00, `position_y` = -7460.00, `position_z` = 47.40
 WHERE `guid` = 2579596 AND `id` = 61660 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579591 Stonetail Scorpid (was -155.86 / -7490.09 / 43.55, 68 yd -> 123 yd)
UPDATE `creature` SET `position_x` = -18.00, `position_y` = -7490.00, `position_z` = 46.50
 WHERE `guid` = 2579591 AND `id` = 61660 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579597 Stonetail Scorpid (was -137.81 / -7614.58 / 41.92, 66 yd -> 115 yd)
UPDATE `creature` SET `position_x` = -17.00, `position_y` = -7511.00, `position_z` = 44.00
 WHERE `guid` = 2579597 AND `id` = 61660 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579740 Mudpaw Miner (was -85.91 / -7514.47 / 17.02, 53 yd -> 103 yd)
UPDATE `creature` SET `position_x` = -22.00, `position_y` = -7555.00, `position_z` = 13.40
 WHERE `guid` = 2579740 AND `id` = 61668 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579735 Mudpaw Miner (was -91.21 / -7544.24 / 13.94, 34 yd -> 108 yd)
UPDATE `creature` SET `position_x` = -25.00, `position_y` = -7590.00, `position_z` = 13.80
 WHERE `guid` = 2579735 AND `id` = 61668 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579734 Mudpaw Miner (was -111.15 / -7582.37 / 14.34, 35 yd -> 102 yd)
UPDATE `creature` SET `position_x` = -50.00, `position_y` = -7620.00, `position_z` = 13.70
 WHERE `guid` = 2579734 AND `id` = 61668 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579739 Mudpaw Miner (was -58.64 / -7532.71 / 13.51, 69 yd -> 119 yd)
UPDATE `creature` SET `position_x` = -25.00, `position_y` = -7615.00, `position_z` = 14.00
 WHERE `guid` = 2579739 AND `id` = 61668 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579746 Mudpaw Overseer (was -97.12 / -7600.42 / 15.71, 57 yd -> 107 yd)
UPDATE `creature` SET `position_x` = -33.00, `position_y` = -7605.00, `position_z` = 13.50
 WHERE `guid` = 2579746 AND `id` = 61669 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579733 Mudpaw Overseer (was -65.65 / -7554.06 / 15.87, 59 yd -> 122 yd)
UPDATE `creature` SET `position_x` = -10.00, `position_y` = -7590.00, `position_z` = 13.90
 WHERE `guid` = 2579733 AND `id` = 61669 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579744 Malfunctioning Mine-Bot (was -69.65 / -7514.08 / 13.30, 66 yd -> 110 yd)
UPDATE `creature` SET `position_x` = -15.00, `position_y` = -7550.00, `position_z` = 13.40
 WHERE `guid` = 2579744 AND `id` = 61687 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579745 Malfunctioning Mine-Bot (was -67.22 / -7581.64 / 12.79, 66 yd -> 124 yd)
UPDATE `creature` SET `position_x` = -30.00, `position_y` = -7630.00, `position_z` = 13.20
 WHERE `guid` = 2579745 AND `id` = 61687 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579743 Malfunctioning Mine-Bot (was -78.47 / -7607.92 / 13.17, 74 yd -> 121 yd)
UPDATE `creature` SET `position_x` = -50.00, `position_y` = -7645.00, `position_z` = 13.40
 WHERE `guid` = 2579743 AND `id` = 61687 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579716 Faulty Battletron 1000 (was -153.71 / -7598.90 / 44.20, 57 yd -> 97 yd)
UPDATE `creature` SET `position_x` = -145.00, `position_y` = -7645.00, `position_z` = 44.10
 WHERE `guid` = 2579716 AND `id` = 61688 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579721 Faulty Battletron 1000 (was -104.79 / -7489.32 / 48.80, 64 yd -> 111 yd)
UPDATE `creature` SET `position_x` = -110.00, `position_y` = -7660.00, `position_z` = 44.70
 WHERE `guid` = 2579721 AND `id` = 61688 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579592 Ashfeather Buzzard (was -121.21 / -7517.41 / 43.88, 33 yd -> 117 yd)
UPDATE `creature` SET `position_x` = -40.00, `position_y` = -7470.00, `position_z` = 47.40
 WHERE `guid` = 2579592 AND `id` = 61656 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;
-- 2579594 Ashfeather Buzzard (was -174.90 / -7549.70 / 50.05, 50 yd -> 103 yd)
UPDATE `creature` SET `position_x` = -172.00, `position_y` = -7642.00, `position_z` = 43.00
 WHERE `guid` = 2579594 AND `id` = 61656 AND `map` = 1
   AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80;

-- End state. The updater ignores statement results, so a failed or skipped step must fail here: none of the 17 may still
-- stand within 80 yd of the graveyard point (a world without these guids passes; the CHECK turns a wrong end state into an
-- error - temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_gy149` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_gy149` (`ok`)
SELECT (SELECT COUNT(*) FROM `creature`
         WHERE `map` = 1 AND `guid` IN (2579593, 2579596, 2579591, 2579597, 2579740, 2579735, 2579734, 2579739, 2579746, 2579733, 2579744, 2579745, 2579743, 2579716, 2579721, 2579592, 2579594)
           AND SQRT(POW(`position_x` + 124.92, 2) + POW(`position_y` + 7550.25, 2)) < 80) = 0;
