-- twow-repo#338 (train 8), owner rules 2026-09-30 (relayed by OB-00, #338):
-- "Bosse sollten gar nicht wieder spawnen in dungeon und raids." / "auch der Trash soll tatsächlich
--  erst nach 7 Tagen wieder respawnen. Also zum ID-Reset der wöchentlich passiert für alle Spieler
--  und Bots ... man selber kann den Dungeon zur Hälfte machen und sich ausloggen und drei Tage
--  später weitermachen."
-- DynamicRespawn never runs inside instances (Creature.cpp: map id > 1 returns), so only
-- creature.spawntimesecsmin/max decide there. For five-player dungeons the core sets the instance
-- reset to "highest creature respawn + 2 h" (MapPersistentStateMgr.cpp), so a started dungeon now
-- keeps its state for about a week.
--
-- Scope: creature spawns on instance maps (map_template.map_type 1 dungeon, 2 raid) with a positive
-- respawn time (negative = event/script, untouched). Rule: new = GREATEST(old, 604800) (7 days) for
--   boss  = rank 3, or an entry of creature_loot_bonus_registry on that map (also bosses that start
--           friendly and a script turns hostile: Vaelastrasz, Solnius, Moroes, Archaedas, ...);
--   rare  = rank 2 or 4;
--   trash = every other creature whose faction does not belong to or befriend the players.
-- Kept as they are (friendly or event NPCs, so events stay repeatable):
--   * non-boss creatures of the player-affiliated factions used in instances (FactionTemplate.dbc,
--     FactionGroup or FriendGroup with bit 1/2/4): 12, 23, 35, 55, 68, 80, 113, 122, 534, 714, 875,
--     1608 - e.g. Weegli Blastfuse, Ribbly's Crony;
--   * explicit: 50105 Medivh (Black Morass escort), 91931 Crypt Watcher (owner to decide),
--     3850 Sorcerer Ashcrombe (friendly), 15378/15379/15380 Merithra, Caelestrasz, Arygos
--     (friendly AQ40 event dragons).
-- Timbermaw Hold (819) is a raid without a weekly reset (reset_delay 0); it gets 7 like the
-- other 20/40 raids.
--
-- Reversible and replay-safe: only spawns below the target are recorded once in creature_bak_338
-- (original values and target); the update is always computed from that table. Naming per OB-40:
-- <source>_bak_<issue>. Later migrations that change these respawn times must update this table
-- too (INSERT IGNORE keeps the first values).
-- Rollback:
--   UPDATE creature c JOIN creature_bak_338 b ON b.guid = c.guid
--      SET c.spawntimesecsmin = b.old_min, c.spawntimesecsmax = b.old_max;
--   UPDATE map_template SET reset_delay = 0 WHERE entry = 819 AND reset_delay = 7;

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
SELECT k.`guid`, k.`id`, k.`map`, k.`kind`, k.`old_min`, k.`old_max`, 604800
FROM (
    SELECT c.`guid`, c.`id`, c.`map`, c.`spawntimesecsmin` AS `old_min`, c.`spawntimesecsmax` AS `old_max`, t.`faction`,
           CASE
               WHEN t.`rank` = 3 OR EXISTS (
                   SELECT 1 FROM `creature_loot_bonus_registry` r
                   WHERE r.`creature_entry` = c.`id` AND r.`map_id` = c.`map`) THEN 'boss'
               WHEN t.`rank` IN (2, 4) THEN 'rare'
               ELSE 'trash'
           END AS `kind`
    FROM `creature` c
    JOIN `map_template` m ON m.`entry` = c.`map` AND m.`map_type` IN (1, 2)
    JOIN `creature_template` t ON t.`entry` = c.`id`
    WHERE c.`spawntimesecsmin` > 0
      AND c.`id` NOT IN (50105, 91931, 3850, 15378, 15379, 15380)
) k
WHERE (k.`kind` = 'boss' OR k.`faction` NOT IN (12, 23, 35, 55, 68, 80, 113, 122, 534, 714, 875, 1608))
  AND (k.`old_min` < 604800 OR k.`old_max` < 604800);

UPDATE `creature` c JOIN `creature_bak_338` b ON b.`guid` = c.`guid`
   SET c.`spawntimesecsmin` = GREATEST(b.`old_min`, b.`target`),
       c.`spawntimesecsmax` = GREATEST(b.`old_max`, b.`target`);

UPDATE `map_template` SET `reset_delay` = 7 WHERE `entry` = 819 AND `reset_delay` = 0;
