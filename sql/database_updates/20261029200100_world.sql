-- twow-repo#575 F9 / F-b V2 (owner 29.10.2026 via OB-00: "Setzte bitte v1 und v2 um."; Ä17 classification "Zwischen-Update",
-- like #622; location "Bizzlo-Platz, ~140 m"): a second graveyard on Blackstone Island (map 1, zone 5536) for players up
-- to level 10, at the friendly hub plaza by Mining Foreman Bizzlo, 137.7 yd from graveyard 149 (which sits in a mob camp,
-- CAUSE.md in evidence/ws-10/cli-pathfinding/goblin-island-b/f9/f9-2-harm). From level 11 graveyard 149 stays.
-- The core reads this row through ObjectMgr::LoadCustomGraveyards (Graveyard.Custom.Enable = 1, Graveyard.Custom.Zones =
-- "5536"; the other custom_graveyards rows are Turtle data and stay unread). Graveyard point -256 / -7508 / 73.19 (ghosts
-- face the spirit healer); a new spirit healer (6491, as at graveyard 149) 6.4 yd away at -252 / -7513, facing the point.
-- guid 2990011 continues the custom spawn range 2990001-2990010 (2990010 = Argur, #447).
-- Nearest player-hostile spawn after V1 (20261029200000_world.sql): Blackvenom Scorpid 2579694, 98 yd, 19 yd lower.
-- Idempotent: the row only if zone 5536 has none and id 51 is free; the healer only if guid 2990011 is free and no spirit
-- healer stands within 30 yd of the point. End-state CHECK.
-- Rollback (exact; Graveyard.Custom.Enable = 0 switches the graveyard off without touching data):
--   DELETE FROM `custom_graveyards` WHERE `id` = 51 AND `zone_id` = 5536;
--   DELETE FROM `creature` WHERE `guid` = 2990011 AND `id` = 6491;

-- The table is part of the world base (sql/base/tw_world_custom_graveyards.sql); created here only if a world lacks it.
CREATE TABLE IF NOT EXISTS `custom_graveyards` (
  `id` mediumint(8) unsigned NOT NULL DEFAULT 0 COMMENT 'Identifier',
  `name` varchar(64) NOT NULL DEFAULT '',
  `map_id` smallint(5) unsigned NOT NULL DEFAULT 0,
  `zone_id` smallint(5) unsigned NOT NULL DEFAULT 0,
  `area_id` smallint(5) unsigned NOT NULL DEFAULT 0,
  `max_level` tinyint(3) unsigned NOT NULL DEFAULT 0,
  `map_gy_alliance` smallint(5) unsigned NOT NULL DEFAULT 0,
  `gy_x_alliance` float NOT NULL DEFAULT 0,
  `gy_y_alliance` float NOT NULL DEFAULT 0,
  `gy_z_alliance` float NOT NULL DEFAULT 0,
  `orientation_alliance` float NOT NULL DEFAULT 0,
  `map_gy_horde` smallint(5) unsigned NOT NULL DEFAULT 0,
  `gy_x_horde` float NOT NULL DEFAULT 0,
  `gy_y_horde` float NOT NULL DEFAULT 0,
  `gy_z_horde` float NOT NULL DEFAULT 0,
  `orientation_horde` float NOT NULL DEFAULT 0,
  PRIMARY KEY (`id`) USING BTREE
) ENGINE=MyISAM DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci ROW_FORMAT=FIXED COMMENT='Custom graveyards';

-- Graveyard: whole zone (area_id = zone_id), up to level 10, the same point for both teams.
INSERT INTO `custom_graveyards` (`id`, `name`, `map_id`, `zone_id`, `area_id`, `max_level`,
                                 `map_gy_alliance`, `gy_x_alliance`, `gy_y_alliance`, `gy_z_alliance`, `orientation_alliance`,
                                 `map_gy_horde`, `gy_x_horde`, `gy_y_horde`, `gy_z_horde`, `orientation_horde`)
SELECT 51, 'Blackstone Island - Bizzlo plaza', 1, 5536, 5536, 10,
       1, -256, -7508, 73.19, 5.387,
       1, -256, -7508, 73.19, 5.387
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `custom_graveyards` WHERE `zone_id` = 5536)
   AND NOT EXISTS (SELECT 1 FROM `custom_graveyards` WHERE `id` = 51);

-- Spirit healer at the new graveyard (same template and respawn as guid 2581569 at graveyard 149).
INSERT INTO `creature` (`guid`, `id`, `id2`, `id3`, `id4`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
                        `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `health_percent`, `mana_percent`,
                        `movement_type`, `spawn_flags`, `visibility_mod`)
SELECT 2990011, 6491, 0, 0, 0, 1, -252, -7513, 73.19, 2.246, 300, 300, 0, 100, 100, 0, 0, 0
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `creature` WHERE `guid` = 2990011)
   AND NOT EXISTS (SELECT 1 FROM `creature` WHERE `id` = 6491 AND `map` = 1
                     AND SQRT(POW(`position_x` + 256, 2) + POW(`position_y` + 7508, 2)) < 30);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_gy149v2` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_gy149v2` (`ok`)
SELECT (SELECT COUNT(*) FROM `custom_graveyards` WHERE `zone_id` = 5536) = 1
   AND (SELECT COUNT(*) FROM `custom_graveyards` WHERE `id` = 51 AND `zone_id` = 5536 AND `map_id` = 1 AND `max_level` = 10) = 1
   AND (SELECT COUNT(*) FROM `creature` WHERE `id` = 6491 AND `map` = 1
          AND SQRT(POW(`position_x` + 256, 2) + POW(`position_y` + 7508, 2)) < 30) = 1;
