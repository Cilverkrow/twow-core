-- Issue twow-repo#367, follow-up to 20260929100000 (#219): skill_line_ability.id is
-- smallint unsigned (max 65535). #219 used the spell id (90140-90211) as row id with
-- INSERT IGNORE, so the first row (recipe 90208) was stored as id 65535 and the other seven
-- rows were dropped as duplicates without an error. The rows get id = spell - 60000
-- (30140-30211). This range is free on the server and in the client SkillLineAbility.dbc
-- (both end at 7210); the client delta (twow-repo#450) uses the same ids.
-- 20260929100000 stays unchanged (its hash is in the migration ledger).
--
-- Spell 90141 has no row on purpose: it is the second Spit spell, not a trainer spell.
-- Replay-safe: both deletes only match the rows written by #219 and by this file.

DELETE FROM `skill_line_ability` WHERE `id` = 65535 AND `spell_id` = 90208;
DELETE FROM `skill_line_ability` WHERE `id` IN (30140, 30142, 30143, 30144, 30208, 30209, 30210, 30211) AND `spell_id` = `id` + 60000;

INSERT INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES
(30208, 40, 90208, 0, 8, 1, 0, 0, 175, 125, 0),
(30209, 40, 90209, 0, 8, 1, 0, 0, 225, 175, 0),
(30210, 40, 90210, 0, 8, 1, 0, 0, 275, 225, 0),
(30211, 40, 90211, 0, 8, 1, 0, 0, 325, 275, 0),
(30140, 38, 90140, 0, 8, 1, 0, 0, 0, 0, 0),
(30142, 38, 90142, 0, 8, 1, 90143, 0, 0, 0, 0),
(30143, 38, 90143, 0, 8, 1, 90144, 0, 0, 0, 0),
(30144, 38, 90144, 0, 8, 1, 0, 0, 0, 0, 0);
