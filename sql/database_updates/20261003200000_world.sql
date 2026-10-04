-- twow-repo#484 train 9 (Zug 9), rogue part. Owner tests after hotfix 8.10 (#484 issuecomment-5972156860)
-- and the owner's train-9 list in the #484 assignment (points 2, 3, 5, 6, 7, 10). Analysis: CLI-484 B-rogue.md / D-cross.md.
--   1. Deep Wounds (talent 9187, helper 61194): the helper was a copy of Hemorrhage 16511 with the same icon (153),
--      visual (5119) and family, so SpellMgr's generic no-stack rule made 16511 and 61194 remove each other on every
--      Hemorrhage - the stack restarted at 1 (owner: "no visible stacks"). Visual 0 breaks that rule; renamed to
--      'Deep Wounds', up to 5 stacks (owner design), no energy cost / GCD for the triggered helper. Aura 87 stays
--      exclusive: the effect is max(Hemorrhage 2 %, n x 2 %) = 10 % at 5 stacks. The script is also bound to the
--      deprecated Hemorrhage ranks 17347/17348 (robustness only, players have 16511).
--   2. Shadow Dance 61143-61145: SPELL_ATTR_PASSIVE (0x40 = 64) in the data. The 8.10 server list
--      (FunserverPassiveSpells.h) stays as a safeguard.
--   3. Trainer teaching spells 61213-61220 (clones of Turtle 47312): visual 107 and interruptFlags 0 like the 2859
--      vanilla class-trainer teaching spells. They leave the 8.9 direct-teaching bypass (visual 222 only) and use
--      the normal trainer cast, which completes since 8.10 (MAX_SPELL_ID 65535).
--   4. Brazen Strike (talent 9178): the 1.12 client predicts "behind the target" from AttributesExB 0x100000
--      (1048576) and never sends a frontal Backstab. The bit leaves the 9 Backstab ranks; the server keeps the rule
--      via customFlags 0x40 (64, SPELL_CUSTOM_BEHIND_TARGET -> SpellEntry::IsFromBehindOnlySpell) and the 60 %
--      exception (IsFunserverFrontalBackstab). spell_extra gets the same flag (only read by the DBC load path).
--   5. Riposte Flow (talent 9169, 61170-61172 unchanged): named strikes 61221 (main hand, after a dodge) and 61222
--      (off hand, after a parry; attributesEx3 0x01000000 REQUIRES_OFFHAND_WEAPON) instead of a white
--      extra swing, so the combat log shows "Riposte Flow". Clone of Riposte 14251 without its disarm effect,
--      aura state, energy cost, cooldown, category and family flags (no Riposte spell mods); 100 % weapon damage
--      (effect 31, bp 99), melee damage class kept (dodge/parry/crit like a melee ability). Double threat via
--      spell_threat multiplier 2 (Unit::DealDamage). Script: spell_rogue.cpp spell_rogue_riposte_flow.
--      Owner 2026-10-04 (#484 issuecomment-5977848252): the strikes trigger procs (poisons, Shadow Edge):
--      attributesEx3 0x200 = 512 SPELL_ATTR_EX3_NOT_A_PROC (Spell.cpp m_canTrigger); the script refuses a
--      Riposte Flow proc caused by a Riposte Flow strike (no recursion).
--   6. Shadow Dance dodge buff 61146 (CLI-484 audit N1): same effects, aura, misc value and family (8) as Evasion
--      5277/15087, so SpellMgr's generic no-stack rule ("identical effects + same family") made a parry during
--      Evasion replace Evasion with the 3 s +5 % buff (and Evasion remove the buff). Family 0 ends that rule; the
--      buff has no spell mods and no script by family. 61147 (parry buff) has no partner and stays.
-- IDs (CLI-484 spec): 61221/61222 Riposte Flow; 61223-61225 Charged Stormstrike R2-R4 (shaman migration
-- 20261003200500); 61226-61229 reserve #484; free from 61230.
-- Coupling: client patch 8 (twow-repo, with riding #488) mirrors 61194, 61143-61145, 61213-61220 and the new
-- 61221/61222 from this table (sql: export) and gets AttributesExB of the 9 Backstab ranks from it; deploy both
-- in the same train-9 window. Server Talent.dbc is unchanged by this file.
-- Replay-safe: backups via INSERT IGNORE with old-value guards, every UPDATE guarded by its old values, clones via
-- INSERT IGNORE, spell_threat via INSERT IGNORE. The end-state CHECK at the bottom fails the file if any step
-- did not reach its target (the updater ignores statement results).
-- Rollback (exact, in this order):
--   UPDATE `spell_template` s JOIN `spell_template_bak_484_rogue` b ON b.`entry` = s.`entry`
--      SET s.`name` = b.`name`, s.`nameSubtext` = b.`nameSubtext`, s.`description` = b.`description`,
--          s.`auraDescription` = b.`auraDescription`, s.`stackAmount` = b.`stackAmount`,
--          s.`spellVisual1` = b.`spellVisual1`, s.`powerType` = b.`powerType`, s.`manaCost` = b.`manaCost`,
--          s.`startRecoveryCategory` = b.`startRecoveryCategory`, s.`startRecoveryTime` = b.`startRecoveryTime`,
--          s.`attributes` = b.`attributes`, s.`interruptFlags` = b.`interruptFlags`,
--          s.`attributesEx2` = b.`attributesEx2`, s.`customFlags` = b.`customFlags`, s.`script_name` = b.`script_name`,
--          s.`spellFamilyName` = b.`spellFamilyName`;
--   UPDATE `spell_extra` e JOIN `spell_extra_bak_484_rogue` b ON b.`entry` = e.`entry` SET e.`customFlags` = b.`customFlags`;
--   DELETE FROM `spell_threat` WHERE `entry` IN (61221, 61222);
--   DELETE FROM `spell_template` WHERE `entry` IN (61221, 61222);
--   (keep the two backup tables until the client patch 8 rollback is done; then DROP TABLE them)

CREATE TABLE IF NOT EXISTS `spell_template_bak_484_rogue` LIKE `spell_template`;
INSERT IGNORE INTO `spell_template_bak_484_rogue`
SELECT * FROM `spell_template`
 WHERE (`entry` = 61194 AND `name` = 'Hemorrhage' AND `stackAmount` = 4 AND `spellVisual1` = 5119)
    OR (`entry` IN (17347, 17348) AND `script_name` = '')
    OR (`entry` BETWEEN 61143 AND 61145 AND `attributes` = 327680)
    OR (`entry` BETWEEN 61213 AND 61220 AND `effect1` = 36 AND `spellVisual1` = 222 AND `interruptFlags` = 15)
    OR (`entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND `attributesEx2` = 1048576)
    OR (`entry` = 61146 AND `spellFamilyName` = 8 AND `effectApplyAuraName1` = 49);

CREATE TABLE IF NOT EXISTS `spell_extra_bak_484_rogue` LIKE `spell_extra`;
INSERT IGNORE INTO `spell_extra_bak_484_rogue`
SELECT * FROM `spell_extra`
 WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND (`customFlags` & 64) = 0;

-- 1. Deep Wounds
UPDATE `spell_template`
   SET `name` = 'Deep Wounds', `nameSubtext` = '',
       `description` = 'Physical damage taken increased by $s1% per stack.',
       `auraDescription` = 'Physical damage taken increased by $s1% per stack.',
       `stackAmount` = 5, `spellVisual1` = 0, `powerType` = 0, `manaCost` = 0,
       `startRecoveryCategory` = 0, `startRecoveryTime` = 0
 WHERE `entry` = 61194 AND `name` = 'Hemorrhage' AND `stackAmount` = 4 AND `spellVisual1` = 5119
   AND `powerType` = 3 AND `manaCost` = 40 AND `startRecoveryCategory` = 133 AND `startRecoveryTime` = 1000;
UPDATE `spell_template` SET `script_name` = 'spell_rogue_hemorrhage_stacks'
 WHERE `entry` IN (17347, 17348) AND `script_name` = '';

-- 2. Shadow Dance passive (327680 = 0x50000 -> 327744 = 0x50040)
UPDATE `spell_template` SET `attributes` = `attributes` | 64
 WHERE `entry` BETWEEN 61143 AND 61145 AND `attributes` = 327680;

-- 3. Trainer teaching spells
UPDATE `spell_template` SET `spellVisual1` = 107, `interruptFlags` = 0
 WHERE `entry` BETWEEN 61213 AND 61220 AND `effect1` = 36 AND `spellVisual1` = 222 AND `interruptFlags` = 15;

-- 4. Brazen Strike: Backstab ranks
UPDATE `spell_template` SET `attributesEx2` = `attributesEx2` & ~1048576, `customFlags` = `customFlags` | 64
 WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300)
   AND `spellFamilyName` = 8 AND `attributesEx2` = 1048576 AND (`customFlags` & 64) = 0;
UPDATE `spell_extra` SET `customFlags` = `customFlags` | 64
 WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND (`customFlags` & 64) = 0;

-- 5. Riposte Flow strikes (donor Riposte 14251)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template`
 WHERE `entry` = 14251 AND `name` = 'Riposte' AND `effect1` = 31 AND `dmgClass` = 2;
UPDATE `tmp_spell` SET `entry` = 61221, `name` = 'Riposte Flow', `nameSubtext` = '',
       `description` = 'A counterstrike after you dodge: deals $s1% main-hand weapon damage and causes double threat.',
       `auraDescription` = '', `category` = 0, `casterAuraState` = 0, `recoveryTime` = 0, `categoryRecoveryTime` = 0,
       `startRecoveryCategory` = 0, `startRecoveryTime` = 0, `durationIndex` = 0, `powerType` = 0, `manaCost` = 0,
       `attributesEx3` = 512, `attributesEx4` = 0, `spellFamilyFlags` = 0, `script_name` = '',
       `effectBasePoints1` = 99,
       `effect2` = 0, `effectApplyAuraName2` = 0, `effectMechanic2` = 0, `effectImplicitTargetA2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template`
 WHERE `entry` = 14251 AND `name` = 'Riposte' AND `effect1` = 31 AND `dmgClass` = 2;
UPDATE `tmp_spell` SET `entry` = 61222, `name` = 'Riposte Flow', `nameSubtext` = '',
       `description` = 'A counterstrike after you parry: deals $s1% off-hand weapon damage and causes double threat.',
       `auraDescription` = '', `category` = 0, `casterAuraState` = 0, `recoveryTime` = 0, `categoryRecoveryTime` = 0,
       `startRecoveryCategory` = 0, `startRecoveryTime` = 0, `durationIndex` = 0, `powerType` = 0, `manaCost` = 0,
       `attributesEx3` = 16777728, `attributesEx4` = 0, `spellFamilyFlags` = 0, `script_name` = '',
       `effectBasePoints1` = 99,
       `effect2` = 0, `effectApplyAuraName2` = 0, `effectMechanic2` = 0, `effectImplicitTargetA2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

INSERT IGNORE INTO `spell_threat` (`entry`, `Threat`, `multiplier`, `ap_bonus`) VALUES
  (61221, 0, 2, 0),
  (61222, 0, 2, 0);

-- 6. Shadow Dance dodge buff: family 0 (no generic no-stack with Evasion)
UPDATE `spell_template` SET `spellFamilyName` = 0
 WHERE `entry` = 61146 AND `spellFamilyName` = 8 AND `effectApplyAuraName1` = 49;

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_484_rogue` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_484_rogue` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` = 61194 AND `name` = 'Deep Wounds' AND `nameSubtext` = '' AND `stackAmount` = 5
           AND `spellVisual1` = 0 AND `spellIconId` = 153 AND `powerType` = 0 AND `manaCost` = 0
           AND `startRecoveryCategory` = 0 AND `startRecoveryTime` = 0 AND `effectApplyAuraName1` = 87
           AND `description` = 'Physical damage taken increased by $s1% per stack.'
           AND `auraDescription` = 'Physical damage taken increased by $s1% per stack.') = 1
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` IN (16511, 17347, 17348) AND `script_name` = 'spell_rogue_hemorrhage_stacks') = 3
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` BETWEEN 61143 AND 61145 AND `attributes` = 327744) = 3
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` BETWEEN 61213 AND 61220 AND `effect1` = 36 AND `spellVisual1` = 107 AND `interruptFlags` = 0) = 8
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300)
           AND (`attributesEx2` & 1048576) = 0 AND (`customFlags` & 64) = 64) = 9
   AND (SELECT COUNT(*) FROM `spell_extra`
         WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300) AND (`customFlags` & 64) = 0) = 0
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `attributesEx3`, `description`) IN
               ((61221, 512, 'A counterstrike after you dodge: deals $s1% main-hand weapon damage and causes double threat.'),
                (61222, 16777728, 'A counterstrike after you parry: deals $s1% off-hand weapon damage and causes double threat.'))
           AND `name` = 'Riposte Flow' AND `effect1` = 31 AND `effectBasePoints1` = 99 AND `effectImplicitTargetA1` = 6
           AND `effect2` = 0 AND `effectApplyAuraName2` = 0 AND `effect3` = 0 AND `dmgClass` = 2
           AND `casterAuraState` = 0 AND `category` = 0 AND `recoveryTime` = 0 AND `categoryRecoveryTime` = 0
           AND `powerType` = 0 AND `manaCost` = 0 AND `durationIndex` = 0 AND `attributesEx4` = 0
           AND `spellFamilyName` = 8 AND `spellFamilyFlags` = 0 AND `script_name` = '') = 2
   AND (SELECT COUNT(*) FROM `spell_threat`
         WHERE `entry` IN (61221, 61222) AND `Threat` = 0 AND `multiplier` = 2 AND `ap_bonus` = 0) = 2
   AND (SELECT COUNT(*) FROM `spell_template` WHERE `entry` IN (61170, 61171, 61172)
           AND `script_name` = 'spell_rogue_riposte_flow' AND `procFlags` = 680) = 3
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` = 61146 AND `spellFamilyName` = 0 AND `effectApplyAuraName1` = 49) = 1
   AND (SELECT COUNT(*) FROM `spell_template_bak_484_rogue`) = 24
   AND (SELECT COUNT(*) FROM `spell_extra_bak_484_rogue`) =
       (SELECT COUNT(*) FROM `spell_extra` WHERE `entry` IN (53, 2589, 2590, 2591, 8721, 11279, 11280, 11281, 25300));
