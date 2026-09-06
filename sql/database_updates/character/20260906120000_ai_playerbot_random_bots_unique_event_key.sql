-- ==============================================
-- FILE: ai_playerbot_random_bots_unique_event_key.sql
-- GENERATED: 20260906120000
-- ==============================================
-- RandomPlayerbotMgr::SetEventValue writes the playerbot event store as a
-- naive DELETE followed by an INSERT. That pair is not atomic and is not
-- serialised, so two worker threads writing the same (owner, bot, event) can
-- interleave as DELETE(A), DELETE(B), INSERT(B), INSERT(A) and leave the
-- older value winning, a crash between the DELETE and the INSERT drops the
-- row outright, and two INSERTs that both land after both DELETEs leave two
-- rows for one key that nothing ever cleans up.
--
-- The fix is to replace that pair with the single-statement upsert in
-- PlayerbotDatabaseContract::EventUpsertSql, which is an
-- INSERT ... ON DUPLICATE KEY UPDATE. That statement only upserts when a
-- UNIQUE index covers the conflict columns. Against a table without one it
-- does not fail: it silently degrades to a plain INSERT, so every event write
-- appends a row, the table grows without bound, and GetEventValue picks an
-- arbitrary duplicate as the current value. This migration installs the key
-- the upsert needs, and MUST be applied before that code change ships.
--
-- Ordering below is deliberate and fail-closed:
--   1. do nothing at all unless the table is really there
--   2. drop rows whose event key is NULL or empty
--   3. collapse duplicate (owner, bot, event) groups to one row
--   4. promote event to NOT NULL
--   5. add the UNIQUE key
--   6. assert the end state, so a silent failure cannot pass as success
--
-- DEDUPLICATION RULE, and why. Where a key already has several rows the row
-- with the highest time survives and the rest are deleted; ties are broken by
-- the highest id, which under an AUTO_INCREMENT primary key is the row
-- inserted last. SetEventValue stamps time with the wall clock of the write,
-- so the highest time is the most recent write, which is exactly the row the
-- upsert would have left behind had it been in place from the start. No other
-- rule reconstructs that: taking the lowest id would resurrect a value the
-- server already believes it overwrote, and failing the migration outright
-- would strand every deployment that has been running the racy code, which is
-- all of them. Duplicates here are corruption, not history, so nothing of
-- value is lost by collapsing them.
--
-- NULL AND EMPTY event. Core declares event as varchar(45) DEFAULT NULL. A
-- nullable column silently defeats a UNIQUE index, because MySQL treats every
-- NULL as distinct and would happily keep unlimited NULL-event rows for one
-- bot. Such rows are already unreadable to the server: GetEventValue skips
-- any row whose event name is null or empty, and SetEventValue has no way to
-- address one. They are deleted rather than repaired, and the column is then
-- promoted to NOT NULL so the constraint cannot be bypassed later.
--
-- COLLATION. event is utf8mb3_general_ci, so the UNIQUE key is
-- case-insensitive. This changes nothing: the DELETE that SetEventValue
-- issues today already matched case-insensitively under the same collation,
-- and no two event names in the codebase differ only by case.
--
-- SCOPE. The table name is spelled literally here because a SQL file cannot
-- read AiPlayerbot.EventStoreTable. A deployment that has repointed that
-- setting at a differently named table must apply the same change to that
-- table by hand; the guards below will see the default table missing and
-- no-op rather than half-applying.
--
-- The redundant non-unique idx_owner_bot_event, if present, is left in place
-- on purpose. It is covered by the new UNIQUE key, but removing an index that
-- exists to stop a live deadlock is not a change worth bundling into an
-- untested migration.

SET @rb_table_present := (
  SELECT COUNT(*) FROM `information_schema`.`TABLES`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'ai_playerbot_random_bots'
    AND `TABLE_TYPE` = 'BASE TABLE'
);

SET @rb_key_present := (
  SELECT COUNT(*) FROM `information_schema`.`STATISTICS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'ai_playerbot_random_bots'
    AND `INDEX_NAME` = 'uq_owner_bot_event'
    AND `NON_UNIQUE` = 0
);

SET @rb_event_nullable := (
  SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'ai_playerbot_random_bots'
    AND `COLUMN_NAME` = 'event'
    AND `IS_NULLABLE` = 'YES'
);

SET @rb_do_data := (@rb_table_present = 1 AND @rb_key_present = 0);

SET @rb_sql := IF(@rb_do_data,
  'DELETE FROM `ai_playerbot_random_bots` WHERE `event` IS NULL OR CHAR_LENGTH(`event`) = 0',
  'DO 0');
PREPARE rb_stmt FROM @rb_sql;
EXECUTE rb_stmt;
DEALLOCATE PREPARE rb_stmt;

DROP TEMPORARY TABLE IF EXISTS `_rb_event_survivor`;
CREATE TEMPORARY TABLE `_rb_event_survivor` (
  `keep_id` bigint(20) NOT NULL,
  PRIMARY KEY (`keep_id`)
) ENGINE=InnoDB;

SET @rb_sql := IF(@rb_do_data,
  'INSERT INTO `_rb_event_survivor` (`keep_id`) SELECT MAX(`live`.`id`) FROM `ai_playerbot_random_bots` AS `live` JOIN (SELECT `owner`, `bot`, `event`, MAX(`time`) AS `newest_time` FROM `ai_playerbot_random_bots` GROUP BY `owner`, `bot`, `event`) AS `newest` ON `newest`.`owner` = `live`.`owner` AND `newest`.`bot` = `live`.`bot` AND `newest`.`event` = `live`.`event` AND `newest`.`newest_time` = `live`.`time` GROUP BY `live`.`owner`, `live`.`bot`, `live`.`event`',
  'DO 0');
PREPARE rb_stmt FROM @rb_sql;
EXECUTE rb_stmt;
DEALLOCATE PREPARE rb_stmt;

SET @rb_sql := IF(@rb_do_data,
  'DELETE FROM `ai_playerbot_random_bots` WHERE `id` NOT IN (SELECT `keep_id` FROM `_rb_event_survivor`)',
  'DO 0');
PREPARE rb_stmt FROM @rb_sql;
EXECUTE rb_stmt;
DEALLOCATE PREPARE rb_stmt;

DROP TEMPORARY TABLE IF EXISTS `_rb_event_survivor`;

SET @rb_sql := IF(@rb_table_present = 1 AND @rb_event_nullable = 1,
  'ALTER TABLE `ai_playerbot_random_bots` MODIFY `event` varchar(45) NOT NULL',
  'DO 0');
PREPARE rb_stmt FROM @rb_sql;
EXECUTE rb_stmt;
DEALLOCATE PREPARE rb_stmt;

SET @rb_sql := IF(@rb_table_present = 1 AND @rb_key_present = 0,
  'ALTER TABLE `ai_playerbot_random_bots` ADD UNIQUE KEY `uq_owner_bot_event` (`owner`, `bot`, `event`)',
  'DO 0');
PREPARE rb_stmt FROM @rb_sql;
EXECUTE rb_stmt;
DEALLOCATE PREPARE rb_stmt;

-- Fail-closed tail. The auto-updater ignores the return value of every
-- statement it runs, so a failed ALTER would otherwise be recorded as a
-- successful migration and the upsert would ship against an unkeyed table.
-- The CHECK on this temporary table turns that into a hard error at the point
-- the migration runs. STATISTICS holds one row per indexed column, so a
-- complete three-column unique key is three rows.
DROP TEMPORARY TABLE IF EXISTS `_rb_event_assert`;
CREATE TEMPORARY TABLE `_rb_event_assert` (
  `ok` tinyint(1) NOT NULL CHECK (`ok` = 1)
) ENGINE=InnoDB;

INSERT INTO `_rb_event_assert` (`ok`)
SELECT IF(
  (SELECT COUNT(*) FROM `information_schema`.`TABLES`
    WHERE `TABLE_SCHEMA` = DATABASE()
      AND `TABLE_NAME` = 'ai_playerbot_random_bots'
      AND `TABLE_TYPE` = 'BASE TABLE') = 0
  OR (
    (SELECT COUNT(*) FROM `information_schema`.`STATISTICS`
      WHERE `TABLE_SCHEMA` = DATABASE()
        AND `TABLE_NAME` = 'ai_playerbot_random_bots'
        AND `INDEX_NAME` = 'uq_owner_bot_event'
        AND `NON_UNIQUE` = 0) = 3
    AND
    (SELECT COUNT(*) FROM `information_schema`.`COLUMNS`
      WHERE `TABLE_SCHEMA` = DATABASE()
        AND `TABLE_NAME` = 'ai_playerbot_random_bots'
        AND `COLUMN_NAME` = 'event'
        AND `IS_NULLABLE` = 'NO') = 1
  ),
  1, 0
);

DROP TEMPORARY TABLE IF EXISTS `_rb_event_assert`;
