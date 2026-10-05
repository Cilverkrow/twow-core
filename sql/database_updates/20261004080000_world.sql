-- twow-repo#511 train 9 (Zug 9): Alterac event item spells 61002-61007 (core 20260802200000, renumbered in 8b)
-- carried leftovers of their donor spells (CLI-484 audit N4). Owner decision 2026-10-04, #484
-- issuecomment-5977848252 ("aufraeumen"):
--   61002 Take Cover (item 55023) and 61007 Fiery Temper buff (proc of 61006): effect 3 was aura 65
--      MOD_CASTING_SPEED_NOT_STACK +6 % from the donor Potion of Quickness 45425 - not in the tooltip, and on 61007
--      it targeted the caster (the healer got haste). Effect 3 emptied. Visual 0: with icon 30 + visual 6 +
--      family 0 the generic no-stack rule made them remove (and be removed by) Haste potions 65/839/1018/45425.
--      Aura texts match the remaining effects.
--   61004 Rage of Alterac (15 s buff of item 55030) and 61006 Fiery Temper (equip proc of item 55032):
--      procCharges 1 from the donor Touch of Weakness 2652 ended them after the first proc; 0 = the whole
--      duration (61004: "your attacks heal you ... with every attack for 15 sec"; 61006: permanent equip
--      effect). Aura texts were Touch of Weakness'. Family 0 (was 6, priest): the generic no-stack rule
--      ("identical effects + same family") paired them with Touch of Weakness R1-R6, Feedback, Shadowguard.
-- Coupling: client patch 8 mirrors these rows from spell_template (twow-repo ops/clientpatch
-- changes/Spell/0455_alterac_item_spells.csv, sql: values); no delta edit, only the new export.
-- Replay-safe: backup via INSERT IGNORE with old-value guards, every UPDATE guarded by its old values, end-state
-- CHECK at the bottom (fails the file if a step did not reach its target).
-- Rollback (exact):
--   UPDATE `spell_template` s JOIN `spell_template_bak_511` b ON b.`entry` = s.`entry`
--      SET s.`effect3` = b.`effect3`, s.`effectApplyAuraName3` = b.`effectApplyAuraName3`,
--          s.`effectBasePoints3` = b.`effectBasePoints3`, s.`effectDieSides3` = b.`effectDieSides3`,
--          s.`effectBaseDice3` = b.`effectBaseDice3`, s.`effectImplicitTargetA3` = b.`effectImplicitTargetA3`,
--          s.`effectMultipleValue3` = b.`effectMultipleValue3`, s.`spellVisual1` = b.`spellVisual1`,
--          s.`auraDescription` = b.`auraDescription`, s.`procCharges` = b.`procCharges`,
--          s.`spellFamilyName` = b.`spellFamilyName`;
--   (keep spell_template_bak_511 until the client patch 8 rollback is done; then DROP TABLE it)

CREATE TABLE IF NOT EXISTS `spell_template_bak_511` LIKE `spell_template`;
INSERT IGNORE INTO `spell_template_bak_511`
SELECT * FROM `spell_template`
 WHERE (`entry` IN (61002, 61007) AND `effect3` = 6 AND `effectApplyAuraName3` = 65 AND `effectBasePoints3` = 5)
    OR (`entry` IN (61004, 61006) AND `procCharges` = 1 AND `spellFamilyName` = 6);

-- 61002 / 61007: no haste leftover, own visual-free buff
UPDATE `spell_template`
   SET `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0, `effectDieSides3` = 0,
       `effectBaseDice3` = 0, `effectImplicitTargetA3` = 0, `effectMultipleValue3` = 0, `spellVisual1` = 0,
       `auraDescription` = 'Damage taken reduced by 10%. Movement speed reduced by 80%.'
 WHERE `entry` = 61002 AND `effect3` = 6 AND `effectApplyAuraName3` = 65 AND `effectBasePoints3` = 5
   AND `spellVisual1` = 6;
UPDATE `spell_template`
   SET `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0, `effectDieSides3` = 0,
       `effectBaseDice3` = 0, `effectImplicitTargetA3` = 0, `effectMultipleValue3` = 0, `spellVisual1` = 0,
       `auraDescription` = 'Attack and casting speed increased by $s1%.'
 WHERE `entry` = 61007 AND `effect3` = 6 AND `effectApplyAuraName3` = 65 AND `effectBasePoints3` = 5
   AND `spellVisual1` = 6;

-- 61004 / 61006: active for the whole duration, own texts, no priest family
UPDATE `spell_template`
   SET `procCharges` = 0, `spellFamilyName` = 0,
       `auraDescription` = 'Your attacks heal you for $61005s1 health.'
 WHERE `entry` = 61004 AND `procCharges` = 1 AND `spellFamilyName` = 6 AND `effectTriggerSpell1` = 61005;
UPDATE `spell_template`
   SET `procCharges` = 0, `spellFamilyName` = 0,
       `auraDescription` = 'Your healing spells have a chance to imbue the target with a fiery temper.'
 WHERE `entry` = 61006 AND `procCharges` = 1 AND `spellFamilyName` = 6 AND `effectTriggerSpell1` = 61007;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_511` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_511` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` IN (61002, 61007) AND `effect3` = 0 AND `effectApplyAuraName3` = 0
           AND `effectBasePoints3` = 0 AND `spellVisual1` = 0) = 2
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` IN (61004, 61006) AND `procCharges` = 0 AND `spellFamilyName` = 0) = 2
   AND (SELECT COUNT(*) FROM `spell_template` WHERE `entry` IN (61002, 61004, 61006, 61007)
           AND `auraDescription` LIKE 'The next damaging melee attack%') = 0
   AND (SELECT COUNT(*) FROM `spell_template_bak_511`) = 4;
