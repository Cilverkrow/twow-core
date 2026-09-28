-- Issue twow-repo#357 stage 2 (client patch pipeline twow-repo#409, OB-00 brief
-- issuecomment-5874183685 part B): server counterpart of the real Enhancement talents.
-- The talent rank spells are the phase-1 IDs 90100-90129 (core#187); this migration
-- only adds what those rows do not carry yet:
--   1. the weapon talent W "Ancestral Arms" (owner decisions O-16..O-20, twow-repo#357
--      issuecomment-5855808216), IDs 90130-90139 (reserved for W by OB-20):
--        90130       talent rank: learns One-Handed Swords 201, Two-Handed Swords 202
--                    and the hub 90131 (shape of Two-Handed Axes and Maces 16269)
--        90131       hub (passive, hidden): adds 90132, 90133, 90134 (aura 192)
--        90132       mace skill +5, two-handed mace skill +10, dagger skill +5
--                    (1 expertise = 1 weapon skill, O-18; aura 98 like 20864)
--        90133       hub: adds 90135, 90136, 90137
--        90134       hub: adds 90138, 90139
--        90135/90136 5 % / 10 % extra attack with a one-/two-handed sword (Sword Master 51668)
--        90137/90138 +4 % / +8 % crit with one-/two-handed axes (O-17, Axe Master 51663)
--        90139       +5 % crit with daggers (Axe Master shape, dagger mask)
--      A talent reset removes 201, 202 and 90131 again: LEARN_SPELL effects are dependent
--      spells (SpellMgr::LoadSpellLearnSpells), as for 16269.
--   2. sword skills for shamans: skill_race_class_info_mod rows for skills 43 and 55,
--      class shaman, flags 0x180, the pattern of Turtle's talent-gated shaman rows 701/702
--      (skills 172/160, SkillRaceClassInfo.dbc, twow-repo#357 issuecomment-5857352559).
--      skill_line_ability stays unchanged, so weapon masters keep refusing swords to
--      shamans (Player::IsSpellFitByClassAndRace); only the talent teaches them.
--   3. talent icons for 90100-90129 (existing SpellIcon IDs only), so the client
--      Spell.dbc rows built from spell_template show fitting icons.
--   4. Elemental Weapons tooltips: the Earthen Bulwark cap is code (spell_shaman.cpp,
--      GetEarthenBulwarkCap = build % x 4/3 = 13/27/40 %, core#182), the text still said
--      "20%". The client tooltip is built from this text in stage 2.
-- Nothing is trained, sold or granted here. Players get W only through the patched
-- Talent.dbc (coupled release); without it no one learns 90130.
-- Replay-safe: INSERT IGNORE and fixed-value UPDATEs.

-- W talent rank (learns swords + the hub)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16269;
UPDATE `tmp_spell` SET `entry` = 90130, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = 'Rank 1', `description` = 'Allows the use of One-Handed and Two-Handed Swords. Your melee attacks with a one-handed sword have a 5% chance and with a two-handed sword a 10% chance to grant an extra attack. Increases your critical strike chance with one-handed axes by 4%, with two-handed axes by 8% and with daggers by 5%. Increases your skill with maces by 5, with two-handed maces by 10 and with daggers by 5.',
    `spellIconId` = 1462, `effectTriggerSpell1` = 201, `effectTriggerSpell2` = 202,
    `effect3` = 36, `effectDieSides3` = `effectDieSides1`, `effectBaseDice3` = `effectBaseDice1`, `effectBasePoints3` = `effectBasePoints1`,
    `effectBonusCoefficient3` = `effectBonusCoefficient1`, `effectImplicitTargetA3` = `effectImplicitTargetA1`, `effectImplicitTargetB3` = `effectImplicitTargetB1`,
    `effectApplyAuraName3` = 0, `effectMiscValue3` = 0, `effectTriggerSpell3` = 90131, `dmgMultiplier3` = `dmgMultiplier1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W hub 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90131, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Ancestral Arms (weapon bonuses).',
    `attributes` = 464,
    `spellIconId` = 1462,
    `equippedItemClass` = -1,
    `equippedItemSubClassMask` = 0,
    `effect1` = 6, `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectDicePerLevel1` = 0, `effectRealPointsPerLevel1` = 0, `effectBasePoints1` = 0, `effectBonusCoefficient1` = -1, `effectMechanic1` = 0, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectRadiusIndex1` = 0, `effectApplyAuraName1` = 192, `effectAmplitude1` = 0, `effectMultipleValue1` = 0, `effectChainTarget1` = 0, `effectItemType1` = 0, `effectMiscValue1` = 0, `effectTriggerSpell1` = 90132, `effectPointsPerComboPoint1` = 0, `dmgMultiplier1` = 1,
    `effect2` = 6, `effectDieSides2` = 1, `effectBaseDice2` = 1, `effectDicePerLevel2` = 0, `effectRealPointsPerLevel2` = 0, `effectBasePoints2` = 0, `effectBonusCoefficient2` = -1, `effectMechanic2` = 0, `effectImplicitTargetA2` = 1, `effectImplicitTargetB2` = 0, `effectRadiusIndex2` = 0, `effectApplyAuraName2` = 192, `effectAmplitude2` = 0, `effectMultipleValue2` = 0, `effectChainTarget2` = 0, `effectItemType2` = 0, `effectMiscValue2` = 0, `effectTriggerSpell2` = 90133, `effectPointsPerComboPoint2` = 0, `dmgMultiplier2` = 1,
    `effect3` = 6, `effectDieSides3` = 1, `effectBaseDice3` = 1, `effectDicePerLevel3` = 0, `effectRealPointsPerLevel3` = 0, `effectBasePoints3` = 0, `effectBonusCoefficient3` = -1, `effectMechanic3` = 0, `effectImplicitTargetA3` = 1, `effectImplicitTargetB3` = 0, `effectRadiusIndex3` = 0, `effectApplyAuraName3` = 192, `effectAmplitude3` = 0, `effectMultipleValue3` = 0, `effectChainTarget3` = 0, `effectItemType3` = 0, `effectMiscValue3` = 0, `effectTriggerSpell3` = 90134, `effectPointsPerComboPoint3` = 0, `dmgMultiplier3` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W weapon skills (O-18: 1 expertise = 1 weapon skill)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90132, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Increases your skill with Maces by 5, Two-Handed Maces by 10 and Daggers by 5.',
    `attributes` = 464,
    `spellIconId` = 1663,
    `equippedItemClass` = -1,
    `equippedItemSubClassMask` = 0,
    `effect1` = 6, `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectDicePerLevel1` = 0, `effectRealPointsPerLevel1` = 0, `effectBasePoints1` = 4, `effectBonusCoefficient1` = -1, `effectMechanic1` = 0, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectRadiusIndex1` = 0, `effectApplyAuraName1` = 98, `effectAmplitude1` = 0, `effectMultipleValue1` = 0, `effectChainTarget1` = 0, `effectItemType1` = 0, `effectMiscValue1` = 54, `effectTriggerSpell1` = 0, `effectPointsPerComboPoint1` = 0, `dmgMultiplier1` = 1,
    `effect2` = 6, `effectDieSides2` = 1, `effectBaseDice2` = 1, `effectDicePerLevel2` = 0, `effectRealPointsPerLevel2` = 0, `effectBasePoints2` = 9, `effectBonusCoefficient2` = -1, `effectMechanic2` = 0, `effectImplicitTargetA2` = 1, `effectImplicitTargetB2` = 0, `effectRadiusIndex2` = 0, `effectApplyAuraName2` = 98, `effectAmplitude2` = 0, `effectMultipleValue2` = 0, `effectChainTarget2` = 0, `effectItemType2` = 0, `effectMiscValue2` = 160, `effectTriggerSpell2` = 0, `effectPointsPerComboPoint2` = 0, `dmgMultiplier2` = 1,
    `effect3` = 6, `effectDieSides3` = 1, `effectBaseDice3` = 1, `effectDicePerLevel3` = 0, `effectRealPointsPerLevel3` = 0, `effectBasePoints3` = 4, `effectBonusCoefficient3` = -1, `effectMechanic3` = 0, `effectImplicitTargetA3` = 1, `effectImplicitTargetB3` = 0, `effectRadiusIndex3` = 0, `effectApplyAuraName3` = 98, `effectAmplitude3` = 0, `effectMultipleValue3` = 0, `effectChainTarget3` = 0, `effectItemType3` = 0, `effectMiscValue3` = 173, `effectTriggerSpell3` = 0, `effectPointsPerComboPoint3` = 0, `dmgMultiplier3` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W hub 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90133, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Ancestral Arms (weapon bonuses).',
    `attributes` = 464,
    `spellIconId` = 1462,
    `equippedItemClass` = -1,
    `equippedItemSubClassMask` = 0,
    `effect1` = 6, `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectDicePerLevel1` = 0, `effectRealPointsPerLevel1` = 0, `effectBasePoints1` = 0, `effectBonusCoefficient1` = -1, `effectMechanic1` = 0, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectRadiusIndex1` = 0, `effectApplyAuraName1` = 192, `effectAmplitude1` = 0, `effectMultipleValue1` = 0, `effectChainTarget1` = 0, `effectItemType1` = 0, `effectMiscValue1` = 0, `effectTriggerSpell1` = 90135, `effectPointsPerComboPoint1` = 0, `dmgMultiplier1` = 1,
    `effect2` = 6, `effectDieSides2` = 1, `effectBaseDice2` = 1, `effectDicePerLevel2` = 0, `effectRealPointsPerLevel2` = 0, `effectBasePoints2` = 0, `effectBonusCoefficient2` = -1, `effectMechanic2` = 0, `effectImplicitTargetA2` = 1, `effectImplicitTargetB2` = 0, `effectRadiusIndex2` = 0, `effectApplyAuraName2` = 192, `effectAmplitude2` = 0, `effectMultipleValue2` = 0, `effectChainTarget2` = 0, `effectItemType2` = 0, `effectMiscValue2` = 0, `effectTriggerSpell2` = 90136, `effectPointsPerComboPoint2` = 0, `dmgMultiplier2` = 1,
    `effect3` = 6, `effectDieSides3` = 1, `effectBaseDice3` = 1, `effectDicePerLevel3` = 0, `effectRealPointsPerLevel3` = 0, `effectBasePoints3` = 0, `effectBonusCoefficient3` = -1, `effectMechanic3` = 0, `effectImplicitTargetA3` = 1, `effectImplicitTargetB3` = 0, `effectRadiusIndex3` = 0, `effectApplyAuraName3` = 192, `effectAmplitude3` = 0, `effectMultipleValue3` = 0, `effectChainTarget3` = 0, `effectItemType3` = 0, `effectMiscValue3` = 0, `effectTriggerSpell3` = 90137, `effectPointsPerComboPoint3` = 0, `dmgMultiplier3` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W hub 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90134, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Ancestral Arms (weapon bonuses).',
    `attributes` = 464,
    `spellIconId` = 1462,
    `equippedItemClass` = -1,
    `equippedItemSubClassMask` = 0,
    `effect1` = 6, `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectDicePerLevel1` = 0, `effectRealPointsPerLevel1` = 0, `effectBasePoints1` = 0, `effectBonusCoefficient1` = -1, `effectMechanic1` = 0, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectRadiusIndex1` = 0, `effectApplyAuraName1` = 192, `effectAmplitude1` = 0, `effectMultipleValue1` = 0, `effectChainTarget1` = 0, `effectItemType1` = 0, `effectMiscValue1` = 0, `effectTriggerSpell1` = 90138, `effectPointsPerComboPoint1` = 0, `dmgMultiplier1` = 1,
    `effect2` = 6, `effectDieSides2` = 1, `effectBaseDice2` = 1, `effectDicePerLevel2` = 0, `effectRealPointsPerLevel2` = 0, `effectBasePoints2` = 0, `effectBonusCoefficient2` = -1, `effectMechanic2` = 0, `effectImplicitTargetA2` = 1, `effectImplicitTargetB2` = 0, `effectRadiusIndex2` = 0, `effectApplyAuraName2` = 192, `effectAmplitude2` = 0, `effectMultipleValue2` = 0, `effectChainTarget2` = 0, `effectItemType2` = 0, `effectMiscValue2` = 0, `effectTriggerSpell2` = 90139, `effectPointsPerComboPoint2` = 0, `dmgMultiplier2` = 1,
    `effect3` = 0, `effectApplyAuraName3` = 0, `effectTriggerSpell3` = 0, `effectBasePoints3` = 0, `effectMiscValue3` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W one-handed sword: 5 % extra attack (16459)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51668;
UPDATE `tmp_spell` SET `entry` = 90135, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Gives your melee attacks with a One-Handed Sword a 5% chance to grant an extra attack.',
    `spellIconId` = 1462, `equippedItemSubClassMask` = 128, `procChance` = 5;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W two-handed sword: 10 % extra attack (16459)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51668;
UPDATE `tmp_spell` SET `entry` = 90136, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Gives your melee attacks with a Two-Handed Sword a 10% chance to grant an extra attack.',
    `spellIconId` = 1462, `equippedItemSubClassMask` = 256, `procChance` = 10;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W one-handed axe: +4 % crit
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90137, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Increases your chance to get a critical strike with One-Handed Axes by 4%.',
    `spellIconId` = 1474, `equippedItemSubClassMask` = 1, `effectBasePoints1` = 3;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W two-handed axe: +8 % crit
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90138, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Increases your chance to get a critical strike with Two-Handed Axes by 8%.',
    `spellIconId` = 1474, `equippedItemSubClassMask` = 2, `effectBasePoints1` = 7;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- W dagger: +5 % crit
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 51663;
UPDATE `tmp_spell` SET `entry` = 90139, `spellFamilyName` = 11, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ancestral Arms', `nameSubtext` = '', `description` = 'Increases your chance to get a critical strike with Daggers by 5%.',
    `spellIconId` = 1504, `equippedItemSubClassMask` = 32768, `effectBasePoints1` = 4;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Sword skills for shamans (flags 0x180 like the talent-gated shaman rows 701/702).
-- Ids 90043/90055 are new records (no SkillRaceClassInfo.dbc row; every field explicit,
-- SkillCostIndex -1 as the loader requires). The client patch adds the same two rows.
INSERT IGNORE INTO `skill_race_class_info_mod` (`Id`, `SkillLineDbcRecord`, `RaceMask`, `ClassMask`, `Flags`, `MinLevel`, `SkillTierId`, `SkillCostIndex`, `Comment`) VALUES
(90043, 43, 2047, 64, 384, 0, 0, -1, 'twow-repo#357 W: One-Handed Swords for shamans, talent only'),
(90055, 55, 2047, 64, 384, 0, 0, -1, 'twow-repo#357 W: Two-Handed Swords for shamans, talent only');

-- Talent icons (existing SpellIcon IDs): attack speed = Flurry, imbue mastery = Elemental
-- Weapons, charged Stormstrike = Stormstrike, storm wisdom = Lightning Bolt, chain storm =
-- Chain Lightning, shield constitution = Lightning Shield, shield ward = Ancestral Guardian.
-- Defense (229) and retaliation (1463) keep theirs.
UPDATE `spell_template` SET `spellIconId` = 108  WHERE `entry` BETWEEN 90100 AND 90104;
UPDATE `spell_template` SET `spellIconId` = 679  WHERE `entry` BETWEEN 90111 AND 90113;
UPDATE `spell_template` SET `spellIconId` = 2210 WHERE `entry` = 90117;
UPDATE `spell_template` SET `spellIconId` = 62   WHERE `entry` BETWEEN 90118 AND 90122;
UPDATE `spell_template` SET `spellIconId` = 165  WHERE `entry` = 90124;
UPDATE `spell_template` SET `spellIconId` = 19   WHERE `entry` BETWEEN 90126 AND 90128;
UPDATE `spell_template` SET `spellIconId` = 1465 WHERE `entry` = 90129;

-- Elemental Weapons ranks 1-3: Earthen Bulwark cap as the code applies it (13/27/40 %).
UPDATE `spell_template` SET `description` = REPLACE(`description`, 'cannot exceed 20% of maximum health', 'cannot exceed 13% of maximum health') WHERE `entry` = 16266;
UPDATE `spell_template` SET `description` = REPLACE(`description`, 'cannot exceed 20% of maximum health', 'cannot exceed 27% of maximum health') WHERE `entry` = 29079;
UPDATE `spell_template` SET `description` = REPLACE(`description`, 'cannot exceed 20% of maximum health', 'cannot exceed 40% of maximum health') WHERE `entry` = 29080;
