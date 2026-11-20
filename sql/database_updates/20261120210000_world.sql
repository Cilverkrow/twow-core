-- twow-repo#580 (WORLD-HOLIDAY-OFF-01), main train (Ae15: DB change). Owner 11.11.2026 (relayed by OB-00):
-- "generell event quests erstmal rausnehmen wie winter-herbst-valentinstag-etc"; Darkmoon Faire stays ("Jahrmarkt bleibt").
-- 1) Link 150 holiday quests that are not yet in game_event_quest to their holiday event. Linked quests start
--    inactive (GameEventMgr load: SetQuestActiveState(false)) and are only active while the event runs; inactive
--    quests are hidden in quest menus and cannot be accepted, also not from items (Player::CanSeeStartQuest /
--    CanTakeQuest check Quest::IsActive). Classification: evidence ws-30/580-event-quests/klassifikation-eventquests-580.tsv.
--    Lunar Festival 7: 73, Winter Veil 2: 21, Winter Veil gifts 21: 6, Love is in the Air 8: 17, Midsummer 1: 16,
--    Children's Week 10: 12, Harvest Festival 11: 4, Noblegarden 9: 1.
-- 2) Switch off the holiday events that are still enabled: 1 Midsummer, 2/21 Winter Veil, 9/28 Noblegarden,
--    10 Children's Week, 11 Harvest Festival, 12 Hallow's End, and the calendar events 6 Fireworks, 34 New Year's Eve,
--    38 Chinese New Year, 39 Post Firework Toasting Goblets, 41 July 4th Fireworks, 42 Peon Day (owner 20.11.2026)
--    (7 Lunar and 8/110-115 Love are already disabled). 6 and 39 are hardcoded events; Update skips them when disabled.
--    Disabled events never start (GameEventMgr::Update skips them), so their NPCs, decoration and quests stay off.
--    Darkmoon Faire (4/5/23/24) and all other events are not touched.
-- Nothing is deleted. Marker table game_event_quest_580 holds exactly the pairs this file adds; backup table
-- game_event_bak_580 holds the previous `disabled` values. Idempotent; end-state CHECK.
-- Rollback (exact):
--   DELETE gq FROM `game_event_quest` gq JOIN `game_event_quest_580` m ON m.`quest` = gq.`quest` AND m.`event` = gq.`event`;
--   UPDATE `game_event` g JOIN `game_event_bak_580` b ON b.`entry` = g.`entry` SET g.`disabled` = b.`disabled`;
--   DROP TABLE `game_event_quest_580`, `game_event_bak_580`;

CREATE TABLE IF NOT EXISTS `game_event_quest_580` (
  `quest` MEDIUMINT UNSIGNED NOT NULL, `event` SMALLINT UNSIGNED NOT NULL, PRIMARY KEY (`quest`, `event`));
CREATE TABLE IF NOT EXISTS `game_event_bak_580` (
  `entry` SMALLINT UNSIGNED NOT NULL PRIMARY KEY, `disabled` TINYINT UNSIGNED NOT NULL);

DROP TEMPORARY TABLE IF EXISTS `tmp_link_580`;
CREATE TEMPORARY TABLE `tmp_link_580` (
  `quest` MEDIUMINT UNSIGNED NOT NULL PRIMARY KEY, `event` SMALLINT UNSIGNED NOT NULL);
INSERT INTO `tmp_link_580` (`quest`, `event`) VALUES
    (50227, 9), (9319, 1), (9322, 1), (9323, 1), (9324, 1), (9325, 1), (9326, 1), (9330, 1),
    (9331, 1), (9332, 1), (9339, 1), (9365, 1), (9367, 1), (9368, 1), (9386, 1), (9388, 1),
    (9389, 1), (8619, 7), (8635, 7), (8636, 7), (8642, 7), (8643, 7), (8644, 7), (8645, 7),
    (8646, 7), (8647, 7), (8648, 7), (8649, 7), (8650, 7), (8651, 7), (8652, 7), (8653, 7),
    (8654, 7), (8670, 7), (8671, 7), (8672, 7), (8673, 7), (8674, 7), (8675, 7), (8676, 7),
    (8677, 7), (8678, 7), (8679, 7), (8680, 7), (8681, 7), (8682, 7), (8683, 7), (8684, 7),
    (8685, 7), (8686, 7), (8688, 7), (8713, 7), (8714, 7), (8715, 7), (8716, 7), (8717, 7),
    (8718, 7), (8719, 7), (8720, 7), (8721, 7), (8722, 7), (8723, 7), (8724, 7), (8725, 7),
    (8726, 7), (8727, 7), (8862, 7), (8863, 7), (8864, 7), (8865, 7), (8866, 7), (8867, 7),
    (8870, 7), (8871, 7), (8872, 7), (8873, 7), (8874, 7), (8875, 7), (8876, 7), (8877, 7),
    (8878, 7), (8879, 7), (8880, 7), (8881, 7), (8882, 7), (8883, 7), (171, 10), (558, 10),
    (910, 10), (911, 10), (915, 10), (925, 10), (1479, 10), (1558, 10), (1687, 10), (1800, 10),
    (4822, 10), (5502, 10), (39978, 11), (39979, 11), (8868, 7), (41653, 7), (41654, 7), (41655, 7),
    (6961, 2), (6962, 2), (6963, 2), (6964, 2), (6983, 2), (6984, 2), (7021, 2), (7022, 2),
    (7023, 2), (7024, 2), (7025, 2), (7042, 2), (7043, 2), (7045, 2), (7061, 2), (7062, 2),
    (7063, 2), (8149, 11), (8150, 11), (8744, 21), (8746, 2), (8762, 2), (8763, 2), (8767, 21),
    (8768, 21), (8769, 21), (8788, 21), (8799, 2), (8803, 21), (8897, 8), (8898, 8), (8899, 8),
    (8900, 8), (8901, 8), (8902, 8), (8903, 8), (8904, 8), (8979, 8), (8981, 8), (8982, 8),
    (8984, 8), (8993, 8), (9024, 8), (9026, 8), (9028, 8), (9029, 8);

-- Record only pairs that are new (first run); a replay finds them already linked and records nothing.
INSERT IGNORE INTO `game_event_quest_580` (`quest`, `event`)
SELECT t.`quest`, t.`event` FROM `tmp_link_580` t
 WHERE EXISTS (SELECT 1 FROM `quest_template` q WHERE q.`entry` = t.`quest`)
   AND EXISTS (SELECT 1 FROM `game_event` e WHERE e.`entry` = t.`event`)
   AND NOT EXISTS (SELECT 1 FROM `game_event_quest` g WHERE g.`quest` = t.`quest`);
INSERT IGNORE INTO `game_event_quest` (`quest`, `event`)
SELECT m.`quest`, m.`event` FROM `game_event_quest_580` m;

-- Keep the first-seen `disabled` values, then switch the holiday events off.
INSERT IGNORE INTO `game_event_bak_580` (`entry`, `disabled`)
SELECT `entry`, `disabled` FROM `game_event` WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42);
UPDATE `game_event` SET `disabled` = 1 WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42);

-- End state. The updater ignores statement results, so a wrong end state must fail here.
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_580` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_580` (`ok`)
SELECT (SELECT COUNT(*) FROM `tmp_link_580` t
         WHERE EXISTS (SELECT 1 FROM `game_event_quest` g WHERE g.`quest` = t.`quest` AND g.`event` = t.`event`)) = 150
   AND (SELECT COUNT(*) FROM `game_event` WHERE `entry` IN (1, 2, 6, 9, 10, 11, 12, 21, 28, 34, 38, 39, 41, 42) AND `disabled` = 1) = 14;
