-- twow-repo#580 (WORLD-HOLIDAY-OFF-01), main train: holiday NPCs and world decoration off for now.
-- Owner 2026-10-11 ("Feiertags-NPCs und Welt-Deko erstmal aus dem Spiel nehmen"; list approved in the OB-40 chat:
-- "okay 4 disable event fairy moon stays" - Darkmoon Faire stays). Live was switched the same evening with the
-- console command `event disable <id>` (writes game_event.disabled = 1); this file brings every other DB (fresh
-- bootstrap, test realm) to the same state. The LITA capital decoration 110-115 is already disabled = 1 in the
-- base data; it is listed so the end-state CHECK guards it (no-op where already disabled).
--   1 Midsummer Fire Festival, 2 Feast of Winter Veil, 6 Fireworks, 9 / 28 Noblegarden, 10 Children's Week,
--   11 Harvest Festival, 12 Hallow's End, 21 Winter Veil: Gifts, 34 New Year's Eve, 38 Chinese New Year,
--   39 Post Firework Toasting Goblets, 41 July 4th Fireworks, 42 Peon Day,
--   110-115 Love is in the Air - Popularity Contest Winner (6 capitals).
-- Not touched: Darkmoon Faire 4 / 5 / 23 / 24, Call to Arms, fishing, Edge of Madness, Maul, AQ / Scourge / patch events.
-- Replay-safe: backup via INSERT IGNORE, UPDATE only where disabled = 0, end-state CHECK.
-- Rollback (exact): UPDATE `game_event` e JOIN `game_event_bak_580` b ON b.`entry` = e.`entry` SET e.`disabled` = b.`disabled`;
--   (per event at runtime: console `event enable <id>`)

CREATE TABLE IF NOT EXISTS `game_event_bak_580` LIKE `game_event`;
INSERT IGNORE INTO `game_event_bak_580`
SELECT * FROM `game_event`
 WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42, 110, 111, 112, 113, 114, 115) AND `disabled` = 0;

UPDATE `game_event` SET `disabled` = 1
 WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42, 110, 111, 112, 113, 114, 115) AND `disabled` = 0;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_580` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_580` (`ok`)
SELECT (SELECT COUNT(*) FROM `game_event`
         WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42, 110, 111, 112, 113, 114, 115) AND `disabled` = 0) = 0
   AND (SELECT COUNT(*) FROM `game_event` WHERE `entry` IN (4, 5, 23, 24) AND `disabled` = 1) = 0;
