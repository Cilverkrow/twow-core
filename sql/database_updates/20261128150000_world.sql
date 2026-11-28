-- twow-repo#579, main train (Ä15: quest data, not travel data): quest 42054 "Hallowed Note" (L1, Mulgore, Camp Narache) is
-- the priest class note of the Tauren start, but it has RequiredClasses 0, so every class can take it.
-- Evidence (OB-50 check 28.11.2026, quest-fallen-20261128/teil2):
--   * the sibling notes of the same giver Grull Hawkwind 2980 after 747 all carry a class mask: 3091 warrior (1),
--     3092 hunter (4), 3093 shaman (64), 3094 druid (1024);
--   * its turn-in Nuhlok Starstrider 63096 is a priest trainer (trainer_class 5);
--   * the Turtle wiki lists the quest as a priest quest (OctoWoW also shows 16, but probably shares our data origin).
-- Fix: RequiredClasses = 16 (priest), same as the other notes.
-- Idempotent: only changes the row while it still has 0; end-state CHECK.
-- Rollback (exact): UPDATE `quest_template` SET `RequiredClasses` = 0 WHERE `entry` = 42054 AND `RequiredClasses` = 16;

UPDATE `quest_template` SET `RequiredClasses` = 16 WHERE `entry` = 42054 AND `RequiredClasses` = 0;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_42054` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_42054` (`ok`)
SELECT (SELECT COUNT(*) FROM `quest_template` WHERE `entry` = 42054 AND `RequiredClasses` = 16) = 1;
