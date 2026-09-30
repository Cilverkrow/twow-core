-- twow-repo#338 (train 8), owner rule 2026-09-30 (relayed by OB-00, #338 issuecomment-5908026810):
-- "Dungeon und raids sind Instanztierte Bereiche dort soll und muss der respawn länger dauern -
--  Bosse sollten gar nicht wieder spawnen in dungeon und raids." / "trash respawn in raids und
--  dungeons ... auf 2.5h erhöhen."
-- DynamicRespawn never runs inside instances (Creature.cpp: map id > 1 returns), so only
-- creature.spawntimesecsmin/max decide there.
--
-- Scope: creature spawns on instance maps (map_template.map_type 1 dungeon, 2 raid) with a
-- positive respawn time (negative = event/script, untouched).
--   boss  = rank 3, or an entry of creature_loot_bonus_registry on that map that is not a
--           #330 class-B elite (class B stays trash until the owner decides otherwise);
--   rare  = rank 2 (rare elite) or 4 (rare);
--   trash = everything else.
-- Rule: trash  new = GREATEST(old, 9000)    (2.5 h)
--       boss/rare new = GREATEST(old, 604800) (7 days: longer than any instance id lives,
--                  raid resets clear saved respawns, dungeons are recreated after reset).
-- Values never go down. Examples fixed here: all Tower of Karazhan bosses (120 s), all
-- Scarlet Citadel bosses (25 s), AQ40 The Master's Eye (3600 s) and Caelestrasz, Arygos,
-- Merithra (6380 s).
--
-- Reversible and replay-safe: only spawns below their target are recorded once in
-- creature_bak_338 (original values and target); the update is always computed from that
-- table. Naming per OB-40: <source>_bak_<issue>. Later migrations that change these respawn
-- times must update this table too (INSERT IGNORE keeps the first values).
-- Rollback:
--   UPDATE creature c JOIN creature_bak_338 b ON b.guid = c.guid
--      SET c.spawntimesecsmin = b.old_min, c.spawntimesecsmax = b.old_max;

CREATE TABLE IF NOT EXISTS `creature_bak_338` (
  `guid` int(10) unsigned NOT NULL,
  `id` mediumint(8) unsigned NOT NULL,
  `map` smallint(5) unsigned NOT NULL,
  `kind` enum('trash','boss','rare') NOT NULL,
  `old_min` int(10) unsigned NOT NULL,
  `old_max` int(10) unsigned NOT NULL,
  `target` int(10) unsigned NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='twow-repo#338: original respawn times of instance creatures';

INSERT IGNORE INTO `creature_bak_338` (`guid`, `id`, `map`, `kind`, `old_min`, `old_max`, `target`)
SELECT k.`guid`, k.`id`, k.`map`, k.`kind`, k.`old_min`, k.`old_max`,
       IF(k.`kind` = 'trash', 9000, 604800)
FROM (
    SELECT c.`guid`, c.`id`, c.`map`, c.`spawntimesecsmin` AS `old_min`, c.`spawntimesecsmax` AS `old_max`,
           CASE
               WHEN t.`rank` = 3 OR EXISTS (
                   SELECT 1 FROM `creature_loot_bonus_registry` r
                   WHERE r.`creature_entry` = c.`id` AND r.`map_id` = c.`map`
                     AND r.`note` NOT LIKE '%class B%') THEN 'boss'
               WHEN t.`rank` IN (2, 4) THEN 'rare'
               ELSE 'trash'
           END AS `kind`
    FROM `creature` c
    JOIN `map_template` m ON m.`entry` = c.`map` AND m.`map_type` IN (1, 2)
    JOIN `creature_template` t ON t.`entry` = c.`id`
    WHERE c.`spawntimesecsmin` > 0
) k
WHERE k.`old_min` < IF(k.`kind` = 'trash', 9000, 604800)
   OR k.`old_max` < IF(k.`kind` = 'trash', 9000, 604800);

UPDATE `creature` c JOIN `creature_bak_338` b ON b.`guid` = c.`guid`
   SET c.`spawntimesecsmin` = GREATEST(b.`old_min`, b.`target`),
       c.`spawntimesecsmax` = GREATEST(b.`old_max`, b.`target`);
