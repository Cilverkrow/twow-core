-- ==============================================
-- FILE: ai_playerbot_random_bots_index.sql
-- GENERATED: 20260708055500
-- ==============================================
-- ai_playerbot_random_bots only has single-column indexes on owner/bot/event
-- individually, so `DELETE FROM ai_playerbot_random_bots WHERE owner = ? AND
-- bot = ? AND event = ?` (RandomPlayerbotMgr::SetEventValue) resolves via a
-- non-unique secondary index and gap-locks a range under InnoDB's default
-- REPEATABLE READ. A mass bot logout (e.g. RandomBotLoginWithPlayer=1 kicking
-- ~1000+ bots at once, spread across CharacterDatabase.WorkerThreads
-- concurrent connections) hits overlapping ranges across many bot IDs and
-- deadlocks (ER_LOCK_DEADLOCK / 1213), which isn't retried and repeats every
-- tick. A composite index turns that DELETE into a precise point-lookup, so
-- concurrent deletes for different bot IDs no longer overlap.
--
-- WHY THIS FILE IS TABLE-CONDITIONAL, AND WHERE THE FRESH-INSTALL COPY LIVES.
--
-- This file used to sit in sql/character_updates/, which the auto-updater
-- never reads (Database.AutoUpdate.Path + CharUpdateName resolve to
-- sql/database_updates/character/), so it was applied by hand or not at all.
-- Moving it here makes the updater apply it -- and the updater runs it against
-- every character database, including one where the table does not exist yet.
--
-- ai_playerbot_random_bots is a MODULE table. It is created by
-- modules/mod-playerbots/sql/characters/ai_playerbot_random_bots.sql, a file
-- that opens with DROP TABLE IF EXISTS and is imported by an operator, not by
-- the updater (the updater's module path is modules/<name>/data/sql/<target>,
-- which does not exist in this tree). Three things follow:
--
--   1. On a fresh install, or on any deployment that runs the world server
--      before importing the playerbot module SQL, the table is absent when
--      this migration runs. A bare ALTER TABLE would fail, and because the
--      updater does not inspect statement results it would still record the
--      migration as applied -- so the index would be permanently skipped on
--      exactly the deployments that never got it by hand either.
--
--   2. The operator can re-import the module SQL at any time, and the
--      DROP TABLE IF EXISTS at the top of it takes the index with the table.
--      A migration recorded in the ledger is never replayed, so the index
--      would not come back.
--
--   3. Therefore the index is ALSO declared in the module's own CREATE TABLE,
--      as `KEY idx_owner_bot_event (owner, bot, event)`. That is the copy a
--      fresh install gets, and it cannot be dropped out from under the
--      migration ledger because it is part of the table definition.
--
-- This file exists for already-deployed databases whose table predates that
-- CREATE. It does nothing at all when the table is absent, and nothing when
-- the index is already there -- both of which are now the normal cases.
--
-- RELATIONSHIP TO 20260906120000_ai_playerbot_random_bots_unique_event_key.sql.
-- That later migration adds a UNIQUE key over the same three columns and
-- deliberately leaves idx_owner_bot_event in place. Under the shipped
-- Database.AutoUpdate.SortByName = 1 this file sorts first, so the ordering is
-- index-then-unique-key on a database that has neither.

SET @rbi_table_present := (
  SELECT COUNT(*) FROM `information_schema`.`TABLES`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'ai_playerbot_random_bots'
    AND `TABLE_TYPE` = 'BASE TABLE'
);

SET @rbi_index_present := (
  SELECT COUNT(*) FROM `information_schema`.`STATISTICS`
  WHERE `TABLE_SCHEMA` = DATABASE()
    AND `TABLE_NAME` = 'ai_playerbot_random_bots'
    AND `INDEX_NAME` = 'idx_owner_bot_event'
);

SET @rbi_sql := IF(@rbi_table_present = 1 AND @rbi_index_present = 0,
  'ALTER TABLE `ai_playerbot_random_bots` ADD INDEX `idx_owner_bot_event` (`owner`, `bot`, `event`)',
  'DO 0');
PREPARE rbi_stmt FROM @rbi_sql;
EXECUTE rbi_stmt;
DEALLOCATE PREPARE rbi_stmt;

-- Fail-closed tail, same reasoning as the unique-key migration: the
-- auto-updater ignores the return value of every statement it runs, so a
-- failed ALTER would otherwise be recorded as a successful migration and the
-- deadlock this file exists to stop would come back silently. The CHECK turns
-- that into a hard error at the point the migration runs. STATISTICS holds one
-- row per indexed column, so a complete three-column index is three rows. An
-- absent table is an accepted end state -- the module CREATE carries the index
-- and the ledger entry costs nothing.
DROP TEMPORARY TABLE IF EXISTS `_rbi_index_assert`;
CREATE TEMPORARY TABLE `_rbi_index_assert` (
  `ok` tinyint(1) NOT NULL CHECK (`ok` = 1)
) ENGINE=InnoDB;

INSERT INTO `_rbi_index_assert` (`ok`)
SELECT IF(
  (SELECT COUNT(*) FROM `information_schema`.`TABLES`
    WHERE `TABLE_SCHEMA` = DATABASE()
      AND `TABLE_NAME` = 'ai_playerbot_random_bots'
      AND `TABLE_TYPE` = 'BASE TABLE') = 0
  OR (SELECT COUNT(*) FROM `information_schema`.`STATISTICS`
    WHERE `TABLE_SCHEMA` = DATABASE()
      AND `TABLE_NAME` = 'ai_playerbot_random_bots'
      AND `INDEX_NAME` = 'idx_owner_bot_event') = 3,
  1, 0
);

DROP TEMPORARY TABLE IF EXISTS `_rbi_index_assert`;
