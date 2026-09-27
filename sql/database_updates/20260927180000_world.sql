-- Issue twow-repo#367 (rogue tank, design twow-repo#386), bots only for release train 7
-- (owner decision D-6, 2026-09-27; approval #319 issuecomment-5855818540).
-- IDs 90140-90146 (90147-90149 free, 90150-90199 reserved for the owner's rogue talent line).
-- Nothing here is trained, sold or dropped: no trainer, recipe, vendor or loot rows.
-- Bots get Spit (L12) and Shadow Dance I/II/III (L20/40/60) and the poison item 90140 from
-- the bot factory (OB-10). Replay-safe: INSERT IGNORE and guarded UPDATEs.
--
--   90140       Spit: taunt, 15 yd, 30 energy, 10 s cooldown, + 2 enemies within 8 yd (script)
--   90141       Spit (splash): the taunt on the extra targets, triggered only
--   90142-90144 Shadow Dance I/II/III: +50/90/130 threat per dodge and parry (script)
--   90145       Shadow Dance: +5 % dodge for 3 s (after a parry)
--   90146       Shadow Dance: +5 % parry for 3 s (after a dodge)
--   item 90140  Agitating Poison for bots: level 20, soulbound, coats with 45612 (enchant 3006)
--   45613       proc gets spell_rogue_agitating_poison: threat/damage by caster level band,
--               L20 +150 / L30 +210 / L40 +275 / L50 +335 / L60 +395 (unchanged)

-- Spit
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 355;
UPDATE `tmp_spell` SET `entry` = 90140, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_spit',
    `name` = 'Spit', `nameSubtext` = '', `description` = 'Spit at the enemy, forcing it and up to two enemies near it to attack you.', `stances` = 0, `category` = 0, `categoryRecoveryTime` = 0, `recoveryTime` = 10000, `powerType` = 3, `manaCost` = 30, `rangeIndex` = 11, `spellLevel` = 12, `baseLevel` = 12;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Spit (splash, triggered)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 355;
UPDATE `tmp_spell` SET `entry` = 90141, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Spit', `nameSubtext` = '', `description` = 'Spit at the enemy, forcing it and up to two enemies near it to attack you.', `stances` = 0, `category` = 0, `categoryRecoveryTime` = 0, `recoveryTime` = 0, `powerType` = 3, `manaCost` = 0, `rangeIndex` = 4, `spellLevel` = 12, `baseLevel` = 12;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance rank I
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 25780;
UPDATE `tmp_spell` SET `entry` = 90142, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_dance',
    `name` = 'Shadow Dance', `nameSubtext` = 'Rank 1', `description` = 'Parrying grants 5% dodge and dodging grants 5% parry for 3 sec. Each dodge and parry causes 50 threat.', `school` = 0, `procFlags` = 680, `procChance` = 100, `spellLevel` = 20, `baseLevel` = 20, `effectApplyAuraName1` = 42, `effectBasePoints1` = 49, `effectMiscValue1` = 0, `effectTriggerSpell1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance rank II
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 25780;
UPDATE `tmp_spell` SET `entry` = 90143, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_dance',
    `name` = 'Shadow Dance', `nameSubtext` = 'Rank 2', `description` = 'Parrying grants 5% dodge and dodging grants 5% parry for 3 sec. Each dodge and parry causes 90 threat.', `school` = 0, `procFlags` = 680, `procChance` = 100, `spellLevel` = 40, `baseLevel` = 40, `effectApplyAuraName1` = 42, `effectBasePoints1` = 89, `effectMiscValue1` = 0, `effectTriggerSpell1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance rank III
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 25780;
UPDATE `tmp_spell` SET `entry` = 90144, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_dance',
    `name` = 'Shadow Dance', `nameSubtext` = 'Rank 3', `description` = 'Parrying grants 5% dodge and dodging grants 5% parry for 3 sec. Each dodge and parry causes 130 threat.', `school` = 0, `procFlags` = 680, `procChance` = 100, `spellLevel` = 60, `baseLevel` = 60, `effectApplyAuraName1` = 42, `effectBasePoints1` = 129, `effectMiscValue1` = 0, `effectTriggerSpell1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance dodge buff
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 23547;
UPDATE `tmp_spell` SET `entry` = 90145, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Dodge chance increased by 5%.', `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `durationIndex` = 27, `effectApplyAuraName1` = 49, `effectBasePoints1` = 4;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance parry buff
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 23547;
UPDATE `tmp_spell` SET `entry` = 90146, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Parry chance increased by 5%.', `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `durationIndex` = 27, `effectApplyAuraName1` = 47, `effectBasePoints1` = 4;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shadow Dance procs on dodge (0x10) and parry (0x20) only.
INSERT IGNORE INTO `spell_proc_event` (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`) VALUES
(90142, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0),
(90143, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0),
(90144, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0);

-- Bot poison item: a soulbound copy of Agitating Poison 65032 usable from level 20. Players
-- have no source for it (no vendor, recipe or loot row) and cannot trade it.
CREATE TEMPORARY TABLE `tmp_item` LIKE `item_template`;
INSERT IGNORE INTO `tmp_item` SELECT * FROM `item_template` WHERE `entry` = 65032;
UPDATE `tmp_item` SET `entry` = 90140, `required_level` = 20, `bonding` = 1, `buy_price` = 0, `sell_price` = 0;
INSERT IGNORE INTO `item_template` SELECT * FROM `tmp_item`;
DROP TEMPORARY TABLE `tmp_item`;

-- The poison proc scales by the caster's level band; at level 60 it stays exactly rank V.
UPDATE `spell_template` SET `script_name` = 'spell_rogue_agitating_poison'
WHERE `entry` = 45613 AND `script_name` = '';
