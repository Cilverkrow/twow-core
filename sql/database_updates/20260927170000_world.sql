-- Issue twow-repo#357 (shaman tank, design twow-repo#392), route B: bot-only talent auras.
-- Owner decisions O-2..O-19 (2026-09-27, #357 / #319). IDs 90100-90129 (O-14); 90130-90139
-- stay reserved for the weapon talent W. These spells are never trained: ClassGrant (#356)
-- grants them to bots on premade paths 7.1/7.3 (OB-10). Changes to existing spells: the
-- Stormstrike script (acts only with aura 90117) and Improved Ghost Wolf rank 2, which
-- makes Ghost Wolf instant for players and bots (owner, 2026-09-27, see the end).
-- Each spell is copied from a donor so every column is right; only what differs is set.
-- Replay-safe: INSERT IGNORE, a guarded UPDATE and a fixed-value UPDATE.
--
--   90100-90104 Attack speed    +2..10 % melee haste    (donor 8815 Haste, aura 138)
--   90105-90109 Defense         +6..30 defense skill    (donor 12297 Anticipation, aura 98)
--   90110       unused (Ghost Wolf instant comes from rank 2 of 16287 for everyone)
--   90111-90113 Imbue mastery   +3/6/9 % all effects    (aura 108 mod 8, imbue mask 0x1E00000)
--   90114-90116 Retaliation     30/60/90 %, once per s  (donor 12298, spell_shaman_retaliation)
--   90117       Stormstrike charges (marker for spell_shaman_stormstrike_charges)
--   90118-90122 Storm wisdom    20..100 % on melee crit (proc -> 90123 / 90125)
--   90123       Storm wisdom buff: LB -20 % cast time and cost per stack, 5 stacks, 30 s
--   90124       Chain storm (marker)
--   90125       Storm wisdom buff with Chain storm: LB + CL
--   90126-90128 Shield constitution +1/2/3 % stamina per Lightning Shield charge
--   90129       Shield ward     -2 % damage taken per Lightning Shield charge

-- Attack speed rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 8815;
UPDATE `tmp_spell` SET `entry` = 90100, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Attack Speed', `nameSubtext` = 'Rank 1', `description` = 'Increases your attack speed by 2%.', `attributes` = 464, `effectBasePoints1` = 1, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Attack speed rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 8815;
UPDATE `tmp_spell` SET `entry` = 90101, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Attack Speed', `nameSubtext` = 'Rank 2', `description` = 'Increases your attack speed by 4%.', `attributes` = 464, `effectBasePoints1` = 3, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Attack speed rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 8815;
UPDATE `tmp_spell` SET `entry` = 90102, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Attack Speed', `nameSubtext` = 'Rank 3', `description` = 'Increases your attack speed by 6%.', `attributes` = 464, `effectBasePoints1` = 5, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Attack speed rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 8815;
UPDATE `tmp_spell` SET `entry` = 90103, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Attack Speed', `nameSubtext` = 'Rank 4', `description` = 'Increases your attack speed by 8%.', `attributes` = 464, `effectBasePoints1` = 7, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Attack speed rank 5
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 8815;
UPDATE `tmp_spell` SET `entry` = 90104, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Attack Speed', `nameSubtext` = 'Rank 5', `description` = 'Increases your attack speed by 10%.', `attributes` = 464, `effectBasePoints1` = 9, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Defense rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90105, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Earthen Guard', `nameSubtext` = 'Rank 1', `description` = 'Increases your Defense skill by 6.', `effectBasePoints1` = 5;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Defense rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90106, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Earthen Guard', `nameSubtext` = 'Rank 2', `description` = 'Increases your Defense skill by 12.', `effectBasePoints1` = 11;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Defense rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90107, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Earthen Guard', `nameSubtext` = 'Rank 3', `description` = 'Increases your Defense skill by 18.', `effectBasePoints1` = 17;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Defense rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90108, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Earthen Guard', `nameSubtext` = 'Rank 4', `description` = 'Increases your Defense skill by 24.', `effectBasePoints1` = 23;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Defense rank 5
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90109, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Earthen Guard', `nameSubtext` = 'Rank 5', `description` = 'Increases your Defense skill by 30.', `effectBasePoints1` = 29;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Imbue mastery rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16287;
UPDATE `tmp_spell` SET `entry` = 90111, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Imbue Mastery', `nameSubtext` = 'Rank 1', `description` = 'Increases the effects of your weapon imbues by 3%.', `effectApplyAuraName1` = 108, `effectMiscValue1` = 8, `effectItemType1` = 31457280, `effectBasePoints1` = 2;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Imbue mastery rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16287;
UPDATE `tmp_spell` SET `entry` = 90112, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Imbue Mastery', `nameSubtext` = 'Rank 2', `description` = 'Increases the effects of your weapon imbues by 6%.', `effectApplyAuraName1` = 108, `effectMiscValue1` = 8, `effectItemType1` = 31457280, `effectBasePoints1` = 5;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Imbue mastery rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16287;
UPDATE `tmp_spell` SET `entry` = 90113, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Imbue Mastery', `nameSubtext` = 'Rank 3', `description` = 'Increases the effects of your weapon imbues by 9%.', `effectApplyAuraName1` = 108, `effectMiscValue1` = 8, `effectItemType1` = 31457280, `effectBasePoints1` = 8;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Retaliation rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90114, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_retaliation',
    `name` = 'Retaliation', `nameSubtext` = 'Rank 1', `description` = 'Dodging, parrying or blocking has a 30% chance to strike the attacker with your Lightning Shield without using a charge and to add one charge. Once per second.', `procChance` = 30, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Retaliation rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90115, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_retaliation',
    `name` = 'Retaliation', `nameSubtext` = 'Rank 2', `description` = 'Dodging, parrying or blocking has a 60% chance to strike the attacker with your Lightning Shield without using a charge and to add one charge. Once per second.', `procChance` = 60, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Retaliation rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90116, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_retaliation',
    `name` = 'Retaliation', `nameSubtext` = 'Rank 3', `description` = 'Dodging, parrying or blocking has a 90% chance to strike the attacker with your Lightning Shield without using a charge and to add one charge. Once per second.', `procChance` = 90, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Stormstrike charges (marker)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90117, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Charged Stormstrike', `nameSubtext` = '', `description` = 'Stormstrike consumes up to 3 Lightning Shield charges, increasing its damage by 10% per charge.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90118, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_storm_wisdom',
    `name` = 'Storm Wisdom', `nameSubtext` = 'Rank 1', `description` = 'Your melee critical strikes have a 20% chance to reduce the cast time and mana cost of your next Lightning Bolt by 20%. Stacks up to 5 times.', `procFlags` = 20, `procChance` = 20, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90119, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_storm_wisdom',
    `name` = 'Storm Wisdom', `nameSubtext` = 'Rank 2', `description` = 'Your melee critical strikes have a 40% chance to reduce the cast time and mana cost of your next Lightning Bolt by 20%. Stacks up to 5 times.', `procFlags` = 20, `procChance` = 40, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90120, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_storm_wisdom',
    `name` = 'Storm Wisdom', `nameSubtext` = 'Rank 3', `description` = 'Your melee critical strikes have a 60% chance to reduce the cast time and mana cost of your next Lightning Bolt by 20%. Stacks up to 5 times.', `procFlags` = 20, `procChance` = 60, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90121, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_storm_wisdom',
    `name` = 'Storm Wisdom', `nameSubtext` = 'Rank 4', `description` = 'Your melee critical strikes have a 80% chance to reduce the cast time and mana cost of your next Lightning Bolt by 20%. Stacks up to 5 times.', `procFlags` = 20, `procChance` = 80, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom rank 5
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90122, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_storm_wisdom',
    `name` = 'Storm Wisdom', `nameSubtext` = 'Rank 5', `description` = 'Your melee critical strikes have a 100% chance to reduce the cast time and mana cost of your next Lightning Bolt by 20%. Stacks up to 5 times.', `procFlags` = 20, `procChance` = 100, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom buff (Lightning Bolt)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16246;
UPDATE `tmp_spell` SET `entry` = 90123, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Storm Wisdom', `nameSubtext` = '', `description` = 'Cast time and mana cost of Lightning Bolt reduced by 20% per stack.', `procFlags` = 0, `procChance` = 101, `procCharges` = 1, `stackAmount` = 5, `durationIndex` = 9, `effectMiscValue1` = 10, `effectBasePoints1` = -21, `effectItemType1` = 1, `effect2` = 6, `effectApplyAuraName2` = 108, `effectMiscValue2` = 14, `effectBasePoints2` = -21, `effectItemType2` = 1, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Chain storm (marker)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90124, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Chain Storm', `nameSubtext` = '', `description` = 'Storm Wisdom also affects Chain Lightning.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Storm wisdom buff (Lightning Bolt + Chain Lightning)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16246;
UPDATE `tmp_spell` SET `entry` = 90125, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Storm Wisdom', `nameSubtext` = '', `description` = 'Cast time and mana cost of Lightning Bolt and Chain Lightning reduced by 20% per stack.', `procFlags` = 0, `procChance` = 101, `procCharges` = 1, `stackAmount` = 5, `durationIndex` = 9, `effectMiscValue1` = 10, `effectBasePoints1` = -21, `effectItemType1` = 3, `effect2` = 6, `effectApplyAuraName2` = 108, `effectMiscValue2` = 14, `effectBasePoints2` = -21, `effectItemType2` = 3, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shield constitution rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90126, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_shield_charge_scaling',
    `name` = 'Shield Constitution', `nameSubtext` = 'Rank 1', `description` = 'Increases your Stamina by 1% for each active Lightning Shield charge.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 2, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shield constitution rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90127, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_shield_charge_scaling',
    `name` = 'Shield Constitution', `nameSubtext` = 'Rank 2', `description` = 'Increases your Stamina by 2% for each active Lightning Shield charge.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 2, `effectBasePoints1` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shield constitution rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90128, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_shield_charge_scaling',
    `name` = 'Shield Constitution', `nameSubtext` = 'Rank 3', `description` = 'Increases your Stamina by 3% for each active Lightning Shield charge.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 2, `effectBasePoints1` = 2;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Shield ward
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90129, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = 'spell_shaman_shield_charge_scaling',
    `name` = 'Shield Ward', `nameSubtext` = '', `description` = 'Reduces all damage taken by 2% for each active Lightning Shield charge.', `effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -3;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Proc conditions: Retaliation on dodge/parry/block (0x70), cooldown 1 s; Storm wisdom on crit (0x2).
INSERT IGNORE INTO `spell_proc_event` (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`) VALUES
(90114, 0, 0, 0, 0, 0, 0, 112, 0, 0, 1),
(90115, 0, 0, 0, 0, 0, 0, 112, 0, 0, 1),
(90116, 0, 0, 0, 0, 0, 0, 112, 0, 0, 1),
(90118, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90119, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90120, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90121, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90122, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0);

-- Stormstrike gets the charge script; it only acts for casters with aura 90117 (bots).
UPDATE `spell_template` SET `script_name` = 'spell_shaman_stormstrike_charges'
WHERE `entry` = 17364 AND `script_name` = '';

-- Owner decision 2026-09-27 (#357 issuecomment-5857525343, confirmed by the owner in OB-20):
-- players get instant Ghost Wolf too. Improved Ghost Wolf 2/2 = -3000 ms (Ghost Wolf uses
-- castingTimeIndex 14 = 3000 ms), rank 1 16262 stays -1000 ms. The client tooltip keeps
-- "2 sec" until the phase-2 patch.
UPDATE `spell_template` SET `effectBasePoints1` = -3001 WHERE `entry` = 16287;
