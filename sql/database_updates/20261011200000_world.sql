-- twow-repo#578 (QUEST-REWSPELL-01), main train: four Turtle quests reward a teaching spell only as `RewSpell`
-- (RewSpellCast = 0). The turn-in cast did not teach the owner's rogue (live v46.2, 11.10.2026), and
-- Player::LearnQuestRewardedSpells() - the login safety net that re-teaches missing quest spells - only looks at
-- `RewSpellCast`, so nothing ever repairs it. Owner decision 2026-10-11 (#578, relayed by OB-00): "ja".
--   50002 The Human Anatomy  (rogue, Horde)    RewSpell 5670 Disguise -> teaches 5170 Disguise + 5668 Peasant Disguise
--   50004 The Orcish Anatomy (rogue, Alliance) RewSpell 5671 Disguise -> teaches 5170 Disguise + 5669 Peon Disguise
--   60060 / 60061 You Reap What You Sow        RewSpell 46063 Simple Wooden Planter -> teaches 46074
-- Fix: RewSpellCast = RewSpell (RewSpell stays as the display icon). Turn-in casts the same spell as before
-- (Player::RewardQuest: RewSpellCast ? RewSpellCast : RewSpell); new: every login re-teaches a missing learned
-- spell to players who already completed the quest, and bots take the LearnSpellFromSpell branch
-- (AutoLearnSpellAction::LearnQuestSpells) instead of storing the teacher spell itself.
-- Not touched on purpose: 60011/60012 "[CANCELLED] Piercing Light" (1888) and 80388 "[DEPRECATED] Stay Awhile and
-- Listen..." (50006 -> 50001 Hardcore) - the login net must never teach those.
-- No client-patch coupling (quest rewards come from the server).
-- Replay-safe: backup via INSERT IGNORE with old-value guards, UPDATE guarded by its old values, end-state CHECK.
-- Rollback (exact):
--   UPDATE `quest_template` q JOIN `quest_template_bak_578` b ON b.`entry` = q.`entry`
--      SET q.`RewSpellCast` = b.`RewSpellCast`;
--   (players keep spells they learned meanwhile; keep quest_template_bak_578 for a later cleanup)

CREATE TABLE IF NOT EXISTS `quest_template_bak_578` LIKE `quest_template`;
INSERT IGNORE INTO `quest_template_bak_578`
SELECT * FROM `quest_template`
 WHERE (`entry`, `RewSpell`) IN ((50002, 5670), (50004, 5671), (60060, 46063), (60061, 46063)) AND `RewSpellCast` = 0;

UPDATE `quest_template` SET `RewSpellCast` = `RewSpell`
 WHERE (`entry`, `RewSpell`) IN ((50002, 5670), (50004, 5671), (60060, 46063), (60061, 46063)) AND `RewSpellCast` = 0;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_578` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_578` (`ok`)
SELECT (SELECT COUNT(*) FROM `quest_template`
         WHERE (`entry`, `RewSpell`, `RewSpellCast`) IN ((50002, 5670, 5670), (50004, 5671, 5671),
                                                        (60060, 46063, 46063), (60061, 46063, 46063))) = 4
   AND (SELECT COUNT(*) FROM `quest_template` WHERE `entry` IN (60011, 60012, 80388) AND `RewSpellCast` <> 0) = 0;
