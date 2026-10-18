-- twow-repo#575 (PATH-01 b, CLI-PATH): owner 11.10.2026 "yes löschen, toller Fund" (via OB-00) - delete exactly the stale
-- ship 176310 links + their path points, with backup. Main train only (Ae15). The same PR drops the rows from
-- modules/mod-playerbots/sql/world/classic/ai_playerbot_travel_nodes.sql (fresh bootstraps).
-- Stale transport link: the playerbot graph has a ship leg 1196 (Menethil Harbor, map 0) <-> 1197 (Auberdine,
-- map 1) for gameobject 176310 "Serenity's Shore" (type 15, TaxiPath 295). This realm has no `transports` row
-- for 176310 (13 rows, MEASURED) and TaxiPath 295 is now the Orgrimmar - Thunder Bluff zeppelin (190549), so
-- the ship never spawns. The leg costs 0.003 (distance 0.1 / speed 30, upstream generator), so routes between
-- the Eastern Kingdoms and Kalimdor prefer it; a bot that takes it waits at the Menethil dock for a vehicle that
-- never comes (UseTransport -> getTransports(176310) empty, MovementActions.cpp:500-560, WorldPosition.cpp:578).
-- Delete the two ship links and their 34 path points. Nodes 1196-1199 and their dock/walk links stay
-- (the loader dereferences link ends unchecked - TravelNode.cpp loadNodeStore - so no node is removed);
-- 1197/1199 are reused by the Stormwind - Auberdine ship 181646 once the generator delta exists (report T3).
-- Guard: only if `transports` still has no 176310 row. Fresh bootstrap: no-op (tables created later by
-- stage 40), the base dump must drop the same rows (patch_base_dump.pl). Apply while mangosd is stopped.
-- Replay-safe: backup via INSERT IGNORE, guarded DELETE, end-state CHECK.
-- Rollback (exact, mangosd stopped):
--   INSERT IGNORE INTO `ai_playerbot_travelnode_link` SELECT * FROM `ai_playerbot_travelnode_link_bak_575`;
--   INSERT IGNORE INTO `ai_playerbot_travelnode_path` SELECT * FROM `ai_playerbot_travelnode_path_bak_575`;
--   (the *_bak_575 tables stay for a later cleanup)

SET @ps_tables := (SELECT COUNT(DISTINCT `TABLE_NAME`) FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = DATABASE()
  AND `TABLE_NAME` IN ('ai_playerbot_travelnode', 'ai_playerbot_travelnode_link', 'ai_playerbot_travelnode_path'));
SET @ps_ship := (SELECT COUNT(*) FROM `transports` WHERE `entry` = 176310);
SET @ps_do := (@ps_tables = 3 AND @ps_ship = 0);

SET @ps_sql := IF(@ps_do, 'CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_link_bak_575` LIKE `ai_playerbot_travelnode_link`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do, 'CREATE TABLE IF NOT EXISTS `ai_playerbot_travelnode_path_bak_575` LIKE `ai_playerbot_travelnode_path`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- T1a backup (2 link rows, 34 path rows)
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_link_bak_575`
   SELECT l.* FROM `ai_playerbot_travelnode_link` l
     JOIN `ai_playerbot_travelnode` a ON a.`id` = l.`node_id`
     JOIN `ai_playerbot_travelnode` b ON b.`id` = l.`to_node_id`
    WHERE l.`type` = 3 AND l.`object` = 176310
      AND ((l.`node_id` = 1196 AND l.`to_node_id` = 1197) OR (l.`node_id` = 1197 AND l.`to_node_id` = 1196))
      AND a.`name` = ''Serenitys Shore'' AND b.`name` = ''Serenitys Shore''', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do,
  'INSERT IGNORE INTO `ai_playerbot_travelnode_path_bak_575`
   SELECT p.* FROM `ai_playerbot_travelnode_path` p
     JOIN `ai_playerbot_travelnode_link_bak_575` k ON k.`node_id` = p.`node_id` AND k.`to_node_id` = p.`to_node_id`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- T1b delete only what was backed up
SET @ps_sql := IF(@ps_do,
  'DELETE p FROM `ai_playerbot_travelnode_path` p
     JOIN `ai_playerbot_travelnode_link_bak_575` k ON k.`node_id` = p.`node_id` AND k.`to_node_id` = p.`to_node_id`', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
SET @ps_sql := IF(@ps_do,
  'DELETE l FROM `ai_playerbot_travelnode_link` l
     JOIN `ai_playerbot_travelnode_link_bak_575` k ON k.`node_id` = l.`node_id` AND k.`to_node_id` = l.`to_node_id`
    WHERE l.`type` = 3 AND l.`object` = 176310', 'DO 0');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;

-- End state: tables absent or ship present -> ok; otherwise no 176310 link, no path rows for the pair,
-- backup holds exactly 2 + 34 rows.
DROP TEMPORARY TABLE IF EXISTS `_ps575_check`;
CREATE TEMPORARY TABLE `_ps575_check` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
SET @ps_sql := IF(@ps_do,
  'INSERT INTO `_ps575_check` (`ok`) SELECT
      (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link` WHERE `type` = 3 AND `object` = 176310) = 0
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_path` WHERE (`node_id` = 1196 AND `to_node_id` = 1197) OR (`node_id` = 1197 AND `to_node_id` = 1196)) = 0
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_link_bak_575`) = 2
  AND (SELECT COUNT(*) FROM `ai_playerbot_travelnode_path_bak_575`) = 34',
  'INSERT INTO `_ps575_check` (`ok`) VALUES (1)');
PREPARE ps_stmt FROM @ps_sql; EXECUTE ps_stmt; DEALLOCATE PREPARE ps_stmt;
