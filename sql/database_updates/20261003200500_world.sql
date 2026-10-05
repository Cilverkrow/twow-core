-- twow-repo#484 train 9 (Zug 9), shaman part. Owner tests after hotfix 8.10 (#484 issuecomment-5972156860)
-- and the owner's train-9 list in the #484 assignment (points 1, 4, 8, 10). Analysis: CLI-484 C-shaman.md / D-cross.md.
--   1. Attack Speed 61101-61105 (talent 9001): effect 3 was a leftover of the donor 8815 (aura 65
--      MOD_CASTING_SPEED_NOT_STACK, +2 % spell haste in every rank, not in the tooltip). Effect 3 becomes an
--      empty slot (same columns as an unused slot; effectBonusCoefficient3 stays -1 = default).
--   2. Charged Stormstrike (talent 9005), owner 2026-10-01/03: 4 ranks, rank r consumes up to r Lightning
--      Shield charges for +10 % each (+10/20/30/40 %). Rank 1 stays 61118 (characters and bots keep their
--      point); ranks 2-4 are new: 61223, 61224, 61225 (clones of 61118, tmp_spell pattern for talentdelta --core).
--      Script: spell_shaman.cpp GetChargedStormstrikeRank.
--   3. Storm Wisdom buffs 61124 / 61126 (talents 9006/9008): the buff text and icon came from the donor
--      Clearcasting 16246 ("Your next elemental damage spell has its mana cost reduced by $s1%", icon 212), so
--      the buff looked like Clearcasting. New texts name Lightning Bolt (61126: or Chain Lightning) and the
--      "next cast" rule; icon 62 = the Storm Wisdom talent icon. The core now consumes the whole stack with the
--      next affected cast (FunserverStackedSpellMods.h, Aura::HandleAddModifier); before, StackAmount 5 gave
--      the spell mods no charges and the buff lasted 30 s on every Lightning Bolt.
--   4. Elemental Weapons / Rushing Winds (Windfury part), owner: 5 stacks wanted. stackAmount 52967/52968/52969
--      2/4/6 -> 3/4/5 (rank progression kept, 5 at 3/3 - decision point in the PR), +2 % per stack unchanged.
--      The rank 2/3 texts (29079/29080) said "Stacks up to $52967u times" (always rank 1); they now read
--      $52968u / $52969u.
--   5. Ancestral Arms (talent 9010, 61131): spell_learn_spell 61131 -> 201 / 202 / 61132 replaces the hotfix 8.4
--      workaround (FunserverTalentLearnSpells.h, removed in the same PR). DB rows are explicit dependent spells
--      (autoLearned = false): Player::AddSpell teaches them at learning and at character load, the talent reset
--      removes them. SpellMgr::LoadSpellLearnSpells logs one "record in `spell_learn_spell` is redundant" line per
--      row at startup (the DBC LEARN_SPELL pair is then not added; the DB row is kept) - expected, harmless.
--      The sword skill starts at 1 (Player::UpdateSpellTrainedSkills, AlwaysMaxSkillForLevel = 0) and rises with use.
--   6. Sword proficiency rows for shamans (#455 issuecomment-5978063722, #484 issuecomment-5978067665, OB-00/OB-15
--      2026-10-04): skill_line_ability 5 (skill 43 -> spell 201) and 7 (skill 55 -> spell 202) lacked the shaman
--      bit 0x40 (class_mask 399 = 0x18f, 7 = 0x7), so the client paired no weapon proficiency with the sword skill
--      and showed "Melee Attack 0" for Ancestral Arms shamans (server damage was fine). 463 = 0x1cf, 71 = 0x47,
--      like Turtle's own shaman talent weapons (197: 0x47). Client parity: twow-repo PR #514 (patch 8,
--      changes/SkillLineAbility/0484_shaman_sword_proficiency.csv, sql:skill_line_ability.class_mask).
-- IDs (CLI-484 spec): 61221/61222 Riposte Flow (rogue migration 20261003200000); 61223-61225 here;
-- 61226-61229 reserve #484; free from 61230. All below 65536 (16-bit client spell IDs, #455).
-- Coupling: client patch 8 (twow-repo, with riding #488) mirrors 61101-61105, 61118, the new 61223-61225,
-- 61124/61126 (AuraDescription, Description, SpellIconID), 52967-52969 (CumulativeAura) and 29079/29080
-- (Description) from this table. Server and client Talent.dbc: talent 9005 SpellRank[1..3] = 61223, 61224,
-- 61225 (same build). Until the server Talent.dbc has the 4 ranks, 9005 has rank 1 only (1 charge, +10 %).
-- Replay-safe: backup via INSERT IGNORE with old-value guards, every UPDATE guarded by its old values, clones
-- and spell_learn_spell via INSERT IGNORE. The end-state CHECK at the bottom fails the file if any step did
-- not reach its target (the updater ignores statement results).
-- Rollback (exact, in this order):
--   UPDATE `spell_template` s JOIN `spell_template_bak_484_shaman` b ON b.`entry` = s.`entry`
--      SET s.`effect3` = b.`effect3`, s.`effectApplyAuraName3` = b.`effectApplyAuraName3`,
--          s.`effectBasePoints3` = b.`effectBasePoints3`, s.`effectDieSides3` = b.`effectDieSides3`,
--          s.`effectBaseDice3` = b.`effectBaseDice3`, s.`effectImplicitTargetA3` = b.`effectImplicitTargetA3`,
--          s.`effectMultipleValue3` = b.`effectMultipleValue3`, s.`nameSubtext` = b.`nameSubtext`,
--          s.`description` = b.`description`, s.`auraDescription` = b.`auraDescription`,
--          s.`spellIconId` = b.`spellIconId`, s.`stackAmount` = b.`stackAmount`;
--   DELETE FROM `spell_template` WHERE `entry` IN (61223, 61224, 61225);
--   DELETE FROM `spell_learn_spell` WHERE `entry` = 61131 AND `SpellID` IN (201, 202, 61132);
--   UPDATE `skill_line_ability` s JOIN `bak_484_skill_line_ability` b ON b.`id` = s.`id` SET s.`class_mask` = b.`class_mask`;
--   (the core rollback brings FunserverTalentLearnSpells.h back; keep the backup table until the client patch 8
--   rollback is done, then DROP TABLE it)

CREATE TABLE IF NOT EXISTS `spell_template_bak_484_shaman` LIKE `spell_template`;
INSERT IGNORE INTO `spell_template_bak_484_shaman`
SELECT * FROM `spell_template`
 WHERE (`entry` BETWEEN 61101 AND 61105 AND `effect3` = 6 AND `effectApplyAuraName3` = 65)
    OR (`entry` = 61118 AND `nameSubtext` = ''
        AND `description` = 'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.')
    OR (`entry` IN (61124, 61126) AND `spellIconId` = 212
        AND `auraDescription` = 'Your next elemental damage spell has its mana cost reduced by $s1%.')
    OR ((`entry`, `stackAmount`) IN ((52967, 2), (52968, 4), (52969, 6)))
    OR (`entry` IN (29079, 29080) AND `description` LIKE '%Stacks up to $52967u times.%');

CREATE TABLE IF NOT EXISTS `bak_484_skill_line_ability` LIKE `skill_line_ability`;
INSERT IGNORE INTO `bak_484_skill_line_ability`
SELECT * FROM `skill_line_ability`
 WHERE (`id` = 5 AND `spell_id` = 201 AND `class_mask` = 399)
    OR (`id` = 7 AND `spell_id` = 202 AND `class_mask` = 7);

-- 1. Attack Speed: drop the +2 % spell haste leftover (effect 3).
UPDATE `spell_template`
   SET `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0, `effectDieSides3` = 0,
       `effectBaseDice3` = 0, `effectImplicitTargetA3` = 0, `effectMultipleValue3` = 0
 WHERE `entry` BETWEEN 61101 AND 61105 AND `effect3` = 6 AND `effectApplyAuraName3` = 65
   AND `effectBasePoints3` = 1 AND `effectMiscValue3` = 0;

-- 2. Charged Stormstrike: rank 1 text, ranks 2-4 as clones of 61118.
UPDATE `spell_template`
   SET `nameSubtext` = 'Rank 1',
       `description` = 'Stormstrike consumes 1 Lightning Shield charge, increasing its damage by 10%.'
 WHERE `entry` = 61118 AND `nameSubtext` = ''
   AND `description` = 'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.';

CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 61118 AND `attributes` = 464 AND `effectApplyAuraName1` = 4;
UPDATE `tmp_spell` SET `entry` = 61223, `nameSubtext` = 'Rank 2',
  `description` = 'Stormstrike consumes up to 2 Lightning Shield charges, increasing its damage by 10% per charge.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 61118 AND `attributes` = 464 AND `effectApplyAuraName1` = 4;
UPDATE `tmp_spell` SET `entry` = 61224, `nameSubtext` = 'Rank 3',
  `description` = 'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 61118 AND `attributes` = 464 AND `effectApplyAuraName1` = 4;
UPDATE `tmp_spell` SET `entry` = 61225, `nameSubtext` = 'Rank 4',
  `description` = 'Stormstrike consumes up to 4 Lightning Shield charges, increasing its damage by 10% per charge.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- 3. Storm Wisdom buffs: own text and the talent icon instead of Clearcasting's.
UPDATE `spell_template`
   SET `description` = 'Cast time and mana cost of your next Lightning Bolt reduced by 20% per stack.',
       `auraDescription` = 'Cast time and mana cost of your next Lightning Bolt reduced by 20% per stack.',
       `spellIconId` = 62
 WHERE `entry` = 61124 AND `spellIconId` = 212
   AND `auraDescription` = 'Your next elemental damage spell has its mana cost reduced by $s1%.';
UPDATE `spell_template`
   SET `description` = 'Cast time and mana cost of your next Lightning Bolt or Chain Lightning reduced by 20% per stack.',
       `auraDescription` = 'Cast time and mana cost of your next Lightning Bolt or Chain Lightning reduced by 20% per stack.',
       `spellIconId` = 62
 WHERE `entry` = 61126 AND `spellIconId` = 212
   AND `auraDescription` = 'Your next elemental damage spell has its mana cost reduced by $s1%.';

-- 4. Elemental Weapons / Rushing Winds: 3/4/5 stacks, rank texts name their own rank.
UPDATE `spell_template` SET `stackAmount` = 3 WHERE `entry` = 52967 AND `stackAmount` = 2;
UPDATE `spell_template` SET `stackAmount` = 4 WHERE `entry` = 52968 AND `stackAmount` = 4;
UPDATE `spell_template` SET `stackAmount` = 5 WHERE `entry` = 52969 AND `stackAmount` = 6;
UPDATE `spell_template`
   SET `description` = REPLACE(`description`, 'Stacks up to $52967u times.', 'Stacks up to $52968u times.')
 WHERE `entry` = 29079 AND `description` LIKE '%Stacks up to $52967u times.%';
UPDATE `spell_template`
   SET `description` = REPLACE(`description`, 'Stacks up to $52967u times.', 'Stacks up to $52969u times.')
 WHERE `entry` = 29080 AND `description` LIKE '%Stacks up to $52967u times.%';

-- 5. Ancestral Arms teaches One-Handed Swords, Two-Handed Swords and its hub spell.
INSERT IGNORE INTO `spell_learn_spell` (`entry`, `SpellID`, `Active`) VALUES
  (61131, 201, 1),
  (61131, 202, 1),
  (61131, 61132, 1);

-- 6. Sword proficiency rows also for shamans (weapon skill display "Melee Attack").
UPDATE `skill_line_ability` SET `class_mask` = 463 WHERE `id` = 5 AND `spell_id` = 201 AND `class_mask` = 399;
UPDATE `skill_line_ability` SET `class_mask` = 71  WHERE `id` = 7 AND `spell_id` = 202 AND `class_mask` = 7;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_shaman` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_484_shaman` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` BETWEEN 61101 AND 61105 AND `effect3` = 0 AND `effectApplyAuraName3` = 0
           AND `effectBasePoints3` = 0 AND `effectApplyAuraName1` = 138) = 5
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `nameSubtext`, `description`) IN
               ((61118, 'Rank 1', 'Stormstrike consumes 1 Lightning Shield charge, increasing its damage by 10%.'),
                (61223, 'Rank 2', 'Stormstrike consumes up to 2 Lightning Shield charges, increasing its damage by 10% per charge.'),
                (61224, 'Rank 3', 'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.'),
                (61225, 'Rank 4', 'Stormstrike consumes up to 4 Lightning Shield charges, increasing its damage by 10% per charge.'))
           AND `name` = 'Charged Stormstrike' AND `attributes` = 464 AND `effectApplyAuraName1` = 4
           AND `spellFamilyName` = 11) = 4
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `auraDescription`) IN
               ((61124, 'Cast time and mana cost of your next Lightning Bolt reduced by 20% per stack.'),
                (61126, 'Cast time and mana cost of your next Lightning Bolt or Chain Lightning reduced by 20% per stack.'))
           AND `description` = `auraDescription` AND `spellIconId` = 62 AND `stackAmount` = 5
           AND `procCharges` = 1 AND `effectApplyAuraName1` = 108 AND `effectApplyAuraName2` = 108) = 2
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `stackAmount`) IN ((52967, 3), (52968, 4), (52969, 5))) = 3
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE ((`entry` = 29079 AND `description` LIKE '%Stacks up to $52968u times.%')
             OR (`entry` = 29080 AND `description` LIKE '%Stacks up to $52969u times.%'))
           AND `description` NOT LIKE '%$52967u%') = 2
   AND (SELECT COUNT(*) FROM `spell_learn_spell`
         WHERE `entry` = 61131 AND `SpellID` IN (201, 202, 61132) AND `Active` = 1) = 3
   AND (SELECT COUNT(*) FROM `spell_template_bak_484_shaman`
         WHERE `entry` IN (61101, 61102, 61103, 61104, 61105, 61118, 61124, 61126, 52967, 52968, 52969, 29079, 29080)) = 13
   AND (SELECT COUNT(*) FROM `skill_line_ability`
         WHERE (`id`, `spell_id`, `class_mask`) IN ((5, 201, 463), (7, 202, 71))) = 2
   AND (SELECT COUNT(*) FROM `bak_484_skill_line_ability` WHERE `id` IN (5, 7)) = 2;
