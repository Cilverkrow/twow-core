-- twow-repo#575 (islands L1-30 #4, CLI-PATH 22.10.2026; OB-00 order 22.10., Ae17 world-data hotfix with twin PR):
-- Wizard's Sanctum (Stormwind) portal nodes of the playerbot travel graph. The mage trainer room (Jennea Cannon 5497,
-- mage class quests L10-26) is reachable only through areatrigger 704 (tower top -9020.1/880.5/151.6 -> room z 29.6) and
-- left through 702 (room -> tower top z 148.6). Nodes: 1312 tower top (702 exit target), 1313 room (704 target),
-- 1314 tower top (704 entrance), 1315 room (702 exit). Portal links 1314->1313 (704) and 1315->1312 (702) are right.
-- Defect: the graph has no walk link between the two tower-top nodes 1312 and 1314 (8.9 yd apart, both on the main
-- navmesh), but two walk links straight through 120 yd of floors, stored as 2-point paths:
--   1312->1315 (tower top -> room, 121.01) and 1313->1314 (room -> tower top, 118.208).
-- So a route into the room never uses portal 704, and the bots walk a path that does not exist.
-- Change: backup + delete the two through-floor links and their 4 path points; insert the walk links 1312<->1314
-- (distance 8.894, 2 path points each, calculated = 1 so no generator run is triggered). Nodes are untouched.
-- After: in = ... -> 1312 -walk-> 1314 -portal 704-> 1313 (room, 1315 / 1483 by walk); out = 1315 -portal 702-> 1312.
-- Guard: the four nodes at their known positions (0.5 yd) with the name 'Stormwind City portal'. Fresh bootstrap: no-op
-- (tables created later); the same PR carries the end state in
-- modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql. Apply while mangosd is stopped.
-- Replay-safe: backup via INSERT IGNORE, deletes only backed-up rows, INSERT IGNORE of the new rows, end-state CHECK.
-- Row counts: link -2 +2, path -4 +4 (totals unchanged); backup 2 links + 4 path rows.
-- Rollback (exact, mangosd stopped):
--   DELETE FROM `ai_playerbot_travelnode_path` WHERE (`node_id` = 1312 AND `to_node_id` = 1314) OR (`node_id` = 1314 AND `to_node_id` = 1312);
--   DELETE FROM `ai_playerbot_travelnode_link` WHERE (`node_id` = 1312 AND `to_node_id` = 1314) OR (`node_id` = 1314 AND `to_node_id` = 1312);
--   INSERT IGNORE INTO `ai_playerbot_travelnode_link` SELECT * FROM `ai_playerbot_travelnode_link_bak_575s`;
--   INSERT IGNORE INTO `ai_playerbot_travelnode_path` SELECT * FROM `ai_playerbot_travelnode_path_bak_575s`;
--   (the *_bak_575s tables stay for a later cleanup)

SET @ps_tables := (SELECT COUNT(DISTINCT `TABLE_NAME`) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE()
  AND `TABLE_NAME` IN ('ai_playerbot_travelnode', 'ai_playerbot_travelnode_link', 'ai_playerbot_travelnode_path'));
SET @ps_nodes := IF(@ps_tables = 3, (SELECT COUNT(*) FROM `ai_playerbot_travelnode` WHERE `name` = 'Stormwind City portal' AND `map_id` = 0 AND (
     (`id` = 1312 AND ABS(`x` + 9014.94) < 0.5 AND ABS(`y` - 873.326) < 0.5 AND ABS(`z` - 148.616) < 0.5)
  OR (`id` = 1313 AND ABS(`x` + 9017.46) < 0.5 AND ABS(`y` - 885.901) < 0.5 AND ABS(`z` - 29.6207) < 0.5)
  OR (`id` = 1314 AND ABS(`x` + 9020.09) < 0.5 AND ABS(`y` - 880.502) < 0.5 AND ABS(`z` - 147.576) < 0.5)
  OR (`id` = 1315 AND ABS(`x` + 9023.26) < 0.5 AND ABS(`y` - 892.398) < 0.5 AND ABS(`z` - 29.5096) < 0.5))), 0);
SET @ps_do := (@ps_tables = 3 AND @ps_nodes = 4);

SET @ps_sql := IF(@ps_do, 'CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_link_bak_575s` LIKE `ai_playerbot_travelnode_link`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do, 'CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_path_bak_575s` LIKE `ai_playerbot_travelnode_path`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- S1a backup: the two through-floor walk links (2 rows) and their path points (4 rows)
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_link_bak_575s`
   SELECT l.* FROM `ai_playerbot_travelnode_link` l
    WHERE l.`type` = 1 AND l.`object` = 0
      AND ((l.`node_id` = 1312 AND l.`to_node_id` = 1315) OR (l.`node_id` = 1313 AND l.`to_node_id` = 1314))', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_path_bak_575s`
   SELECT p.* FROM `ai_playerbot_travelnode_path` p
     JOIN `ai_playerbot_travelnode_link_bak_575s` k ON k.`node_id` = p.`node_id` AND k.`to_node_id` = p.`to_node_id`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- S1b delete only what was backed up
SET @ps_sql := IF(@ps_do,
  'DELETE p FROM `ai_playerbot_travelnode_path` p
     JOIN `ai_playerbot_travelnode_link_bak_575s` k ON k.`node_id` = p.`node_id` AND k.`to_node_id` = p.`to_node_id`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do,
  'DELETE l FROM `ai_playerbot_travelnode_link` l
     JOIN `ai_playerbot_travelnode_link_bak_575s` k ON k.`node_id` = l.`node_id` AND k.`to_node_id` = l.`to_node_id`
    WHERE l.`type` = 1 AND l.`object` = 0', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- S2 the missing walk links on the tower top, 1312 <-> 1314 (2 path points each = the node positions)
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_link`
     (`node_id`, `to_node_id`, `type`, `object`, `distance`, `swim_distance`, `extra_cost`, `calculated`, `max_creature_0`, `max_creature_1`, `max_creature_2`)
   VALUES (1312, 1314, 1, 0, 8.894, 0, 0, 1, 0, 0, 0), (1314, 1312, 1, 0, 8.894, 0, 0, 1, 0, 0, 0)', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_path` (`node_id`, `to_node_id`, `nr`, `map_id`, `x`, `y`, `z`) VALUES
     (1312, 1314, 0, 0, -9014.94, 873.326, 148.616), (1312, 1314, 1, 0, -9020.09, 880.502, 147.576),
     (1314, 1312, 0, 0, -9020.09, 880.502, 147.576), (1314, 1312, 1, 0, -9014.94, 873.326, 148.616)', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- End state: guard not met -> ok (no-op); otherwise no through-floor link or path row, both tower-top links with
-- 2 path points each, the portal links untouched, backup holds exactly 2 + 4 rows.
DROP TEMPORARY TABLE IF EXISTS `_ps575s_check`;
CREATE TEMPORARY TABLE `_ps575s_check` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
SET @ps_sql := IF(@ps_do,
  'INSERT INTO `_ps575s_check` (`ok`) SELECT
      (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link` WHERE (`node_id` = 1312 AND `to_node_id` = 1315) OR (`node_id` = 1313 AND `to_node_id` = 1314)) = 0
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_path` WHERE (`node_id` = 1312 AND `to_node_id` = 1315) OR (`node_id` = 1313 AND `to_node_id` = 1314)) = 0
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link` WHERE `type` = 1 AND ((`node_id` = 1312 AND `to_node_id` = 1314) OR (`node_id` = 1314 AND `to_node_id` = 1312))) = 2
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_path` WHERE (`node_id` = 1312 AND `to_node_id` = 1314) OR (`node_id` = 1314 AND `to_node_id` = 1312)) = 4
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link` WHERE `type` = 2 AND ((`node_id` = 1314 AND `to_node_id` = 1313 AND `object` = 704) OR (`node_id` = 1315 AND `to_node_id` = 1312 AND `object` = 702))) = 2
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link_bak_575s`) = 2
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_path_bak_575s`) = 4',
  'INSERT INTO `_ps575s_check` (`ok`) VALUES (1)');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
