DROP TABLE IF EXISTS `ai_playerbot_random_bots`;
CREATE TABLE `ai_playerbot_random_bots` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `owner` bigint(20) NOT NULL,
  `bot` bigint(20) NOT NULL,
  `time` bigint(20) NOT NULL,
  `validIn` bigint(20) DEFAULT NULL,
  `event` varchar(45) DEFAULT NULL,
  `value` bigint(20) DEFAULT NULL,
  `data` varchar(255) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `owner` (`owner`),
  KEY `bot` (`bot`),
  KEY `event` (`event`),
  -- Composite (owner, bot, event), matching the WHERE of
  -- RandomPlayerbotMgr::SetEventValue. Without it that DELETE resolves via a
  -- single-column secondary index and gap-locks a range under REPEATABLE
  -- READ, which deadlocks (ER_LOCK_DEADLOCK / 1213) during a mass bot logout.
  -- Declared here as well as in
  -- sql/database_updates/character/20260708055500_ai_playerbot_random_bots_index.sql
  -- on purpose: this file opens with DROP TABLE IF EXISTS, so a re-import would
  -- otherwise take the index away for good -- the auto-updater never replays a
  -- migration it has already recorded. A fresh install gets the index from the
  -- table definition; the migration is only for databases that predate it.
  KEY `idx_owner_bot_event` (`owner`, `bot`, `event`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8mb3_general_ci;