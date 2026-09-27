-- Issue twow-repo#367: the owner's rogue talent line (2026-09-27, #367 issuecomment-5858564596)
-- as bot-only auras 90150-90190 plus helpers 90191-90193 (90194-90199 reserve). Owner
-- decisions on the open points: #367 issuecomment-5858590523 / -5858621234. Never trained:
-- ClassGrant (OB-10) grants them on paths 4.0-4.3. Existing spells only get script bindings
-- (Vigor energy 52526, Seal Fate 14189, Hemorrhage 16511) that act solely with the bot auras.
-- Core hooks read the rank auras: damage from behind / in stealth / execute / Cold Blood
-- (MeleeDamageBonusDone), Backstab from the front (Spell::CheckCast), Ghostly Strike magic
-- dodge (MagicSpellHitResult). Replay-safe: INSERT IGNORE and guarded UPDATEs.

-- Assassination R1/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90150, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Blindside', `nameSubtext` = 'Rank 1', `description` = 'Your attacks deal 5% more damage when you are behind the target.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 4;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R1/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90151, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Blindside', `nameSubtext` = 'Rank 2', `description` = 'Your attacks deal 10% more damage when you are behind the target.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 9;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R1/C4 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90152, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Blindside', `nameSubtext` = 'Rank 3', `description` = 'Your attacks deal 15% more damage when you are behind the target.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 14;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R1/C4 rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90153, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Blindside', `nameSubtext` = 'Rank 4', `description` = 'Your attacks deal 20% more damage when you are behind the target.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 19;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R3/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90154, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_cooldown_flow',
    `name` = 'Flowing Blades', `nameSubtext` = 'Rank 1', `description` = 'While Slice and Dice is active, your melee critical strikes have a 25% chance to reduce the remaining cooldown of Flourish, Cold Blood, Adrenaline Rush, Preparation and Mark for Death by 1 sec.', `procFlags` = 20, `procChance` = 25, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R3/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90155, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_cooldown_flow',
    `name` = 'Flowing Blades', `nameSubtext` = 'Rank 2', `description` = 'While Slice and Dice is active, your melee critical strikes have a 50% chance to reduce the remaining cooldown of Flourish, Cold Blood, Adrenaline Rush, Preparation and Mark for Death by 1 sec.', `procFlags` = 20, `procChance` = 50, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R5/C4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90156, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Frozen Blood', `nameSubtext` = '', `description` = 'Cold Blood also increases the damage of your next attack by 30%.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 29;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R7/C1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90157, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Vigorous Fury', `nameSubtext` = '', `description` = 'Each time Vigor restores energy, your damage is increased by 2% for 8 sec. Stacks up to 10 times.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Assassination R7/C3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90158, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Fated Echo', `nameSubtext` = '', `description` = 'When Seal Fate grants you a combo point, you have a 33% chance to gain another one.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 32;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C1 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90159, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Nimble Body', `nameSubtext` = 'Rank 1', `description` = 'Increases your Agility by 2%.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C1 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90160, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Nimble Body', `nameSubtext` = 'Rank 2', `description` = 'Increases your Agility by 4%.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 3;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C1 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90161, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Nimble Body', `nameSubtext` = 'Rank 3', `description` = 'Increases your Agility by 6%.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 5;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C1 rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90162, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Nimble Body', `nameSubtext` = 'Rank 4', `description` = 'Increases your Agility by 8%.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 7;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C1 rank 5
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90163, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Nimble Body', `nameSubtext` = 'Rank 5', `description` = 'Increases your Agility by 10%.', `effectApplyAuraName1` = 137, `effectMiscValue1` = 1, `effectBasePoints1` = 9;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90164, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Guarded Stance', `nameSubtext` = 'Rank 1', `description` = 'Increases your Defense skill by 6.', `effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 5;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90165, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Guarded Stance', `nameSubtext` = 'Rank 2', `description` = 'Increases your Defense skill by 12.', `effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 11;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C4 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90166, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Guarded Stance', `nameSubtext` = 'Rank 3', `description` = 'Increases your Defense skill by 18.', `effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 17;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C4 rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90167, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Guarded Stance', `nameSubtext` = 'Rank 4', `description` = 'Increases your Defense skill by 24.', `effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 23;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R1/C4 rank 5
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90168, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Guarded Stance', `nameSubtext` = 'Rank 5', `description` = 'Increases your Defense skill by 30.', `effectApplyAuraName1` = 98, `effectMiscValue1` = 95, `effectBasePoints1` = 29;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R2/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90169, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_riposte_flow',
    `name` = 'Riposte Flow', `nameSubtext` = 'Rank 1', `description` = 'Parrying has a 12% chance to strike with your off-hand weapon, dodging a 12% chance to strike with your main-hand weapon, at most once per second. These attacks cause double threat.', `procFlags` = 680, `procChance` = 12, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R2/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90170, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_riposte_flow',
    `name` = 'Riposte Flow', `nameSubtext` = 'Rank 2', `description` = 'Parrying has a 24% chance to strike with your off-hand weapon, dodging a 24% chance to strike with your main-hand weapon, at most once per second. These attacks cause double threat.', `procFlags` = 680, `procChance` = 24, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R2/C4 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90171, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_riposte_flow',
    `name` = 'Riposte Flow', `nameSubtext` = 'Rank 3', `description` = 'Parrying has a 36% chance to strike with your off-hand weapon, dodging a 36% chance to strike with your main-hand weapon, at most once per second. These attacks cause double threat.', `procFlags` = 680, `procChance` = 36, `effectApplyAuraName1` = 42, `effectBasePoints1` = 0, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R4/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90172, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_arcane_evasion',
    `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 1', `description` = 'Converts 15% of your dodge and parry chance into resistance to all schools of magic, 1 per percent.', `effectApplyAuraName1` = 22, `effectMiscValue1` = 126, `effectBasePoints1` = 14, `effect2` = 6, `effectApplyAuraName2` = 23, `effectMiscValue2` = 0, `effectBasePoints2` = 0, `effectAmplitude2` = 1000, `effectTriggerSpell2` = 0, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R4/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90173, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_arcane_evasion',
    `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 2', `description` = 'Converts 30% of your dodge and parry chance into resistance to all schools of magic, 2 per percent.', `effectApplyAuraName1` = 22, `effectMiscValue1` = 126, `effectBasePoints1` = 29, `effect2` = 6, `effectApplyAuraName2` = 23, `effectMiscValue2` = 0, `effectBasePoints2` = 1, `effectAmplitude2` = 1000, `effectTriggerSpell2` = 0, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R4/C4 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90174, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_arcane_evasion',
    `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 3', `description` = 'Converts 45% of your dodge and parry chance into resistance to all schools of magic, 3 per percent.', `effectApplyAuraName1` = 22, `effectMiscValue1` = 126, `effectBasePoints1` = 44, `effect2` = 6, `effectApplyAuraName2` = 23, `effectMiscValue2` = 0, `effectBasePoints2` = 2, `effectAmplitude2` = 1000, `effectTriggerSpell2` = 0, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C1 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90175, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Coup de Grace', `nameSubtext` = 'Rank 1', `description` = 'Backstab and Sinister Strike deal 10% more damage to targets below 35% health.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 9;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C1 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90176, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Coup de Grace', `nameSubtext` = 'Rank 2', `description` = 'Backstab and Sinister Strike deal 20% more damage to targets below 35% health.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 19;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C1 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90177, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Coup de Grace', `nameSubtext` = 'Rank 3', `description` = 'Backstab and Sinister Strike deal 30% more damage to targets below 35% health.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 29;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R7/C1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90178, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Brazen Strike', `nameSubtext` = '', `description` = 'Backstab can be used from any side against targets below 60% health.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 59;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C4 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90179, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 1', `description` = 'Reduces all damage taken by 4% and increases your armor by 4%.', `effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -5, `effect2` = 6, `effectApplyAuraName2` = 142, `effectMiscValue2` = 1, `effectBasePoints2` = 3, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C4 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90180, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 2', `description` = 'Reduces all damage taken by 8% and increases your armor by 8%.', `effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -9, `effect2` = 6, `effectApplyAuraName2` = 142, `effectMiscValue2` = 1, `effectBasePoints2` = 7, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R6/C4 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90181, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 3', `description` = 'Reduces all damage taken by 12% and increases your armor by 12%.', `effectApplyAuraName1` = 87, `effectMiscValue1` = 127, `effectBasePoints1` = -13, `effect2` = 6, `effectApplyAuraName2` = 142, `effectMiscValue2` = 1, `effectBasePoints2` = 11, `effectDieSides2` = `effectDieSides1`, `effectBaseDice2` = `effectBaseDice1`, `effectImplicitTargetA2` = `effectImplicitTargetA1`, `effectImplicitTargetB2` = `effectImplicitTargetB1`;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Combat R7/C4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90182, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Ghostly Evasion', `nameSubtext` = '', `description` = 'While Ghostly Strike is active, you can dodge hostile spells.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R1/C1 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90183, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 1', `description` = 'Your attacks deal 5% more damage while stealthed, including the opener that breaks stealth.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 4;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R1/C1 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90184, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 2', `description` = 'Your attacks deal 10% more damage while stealthed, including the opener that breaks stealth.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 9;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R1/C1 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90185, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 3', `description` = 'Your attacks deal 15% more damage while stealthed, including the opener that breaks stealth.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 14;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R1/C1 rank 4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90186, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 4', `description` = 'Your attacks deal 20% more damage while stealthed, including the opener that breaks stealth.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 19;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R6/C4
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12297;
UPDATE `tmp_spell` SET `entry` = 90187, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Deep Wounds', `nameSubtext` = '', `description` = 'Hemorrhage stacks up to 5 times for 15 sec.', `effectApplyAuraName1` = 4, `effectMiscValue1` = 0, `effectBasePoints1` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R7/C3 rank 1
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90188, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_edge',
    `name` = 'Shadow Edge', `nameSubtext` = 'Rank 1', `description` = 'Your melee attacks deal an additional 8% of their damage as Shadow damage.', `procFlags` = 20, `procChance` = 100, `effectApplyAuraName1` = 42, `effectBasePoints1` = 7, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R7/C3 rank 2
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90189, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_edge',
    `name` = 'Shadow Edge', `nameSubtext` = 'Rank 2', `description` = 'Your melee attacks deal an additional 16% of their damage as Shadow damage.', `procFlags` = 20, `procChance` = 100, `effectApplyAuraName1` = 42, `effectBasePoints1` = 15, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Subtlety R7/C3 rank 3
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 12298;
UPDATE `tmp_spell` SET `entry` = 90190, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = 'spell_rogue_shadow_edge',
    `name` = 'Shadow Edge', `nameSubtext` = 'Rank 3', `description` = 'Your melee attacks deal an additional 24% of their damage as Shadow damage.', `procFlags` = 20, `procChance` = 100, `effectApplyAuraName1` = 42, `effectBasePoints1` = 23, `effectTriggerSpell1` = 0, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Helper: Vigorous Fury buff
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 23547;
UPDATE `tmp_spell` SET `entry` = 90191, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Vigorous Fury', `nameSubtext` = '', `description` = 'Damage increased by 2% per stack.', `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `stackAmount` = 10, `durationIndex` = 31, `effectApplyAuraName1` = 79, `effectMiscValue1` = 127, `effectBasePoints1` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Helper: Shadow Edge damage
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45613;
UPDATE `tmp_spell` SET `entry` = 90192, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Edge', `nameSubtext` = '', `description` = 'Shadow damage.', `school` = 5, `dmgClass` = 0, `effectBasePoints1` = 0, `effectDieSides1` = 1, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Helper: Hemorrhage stack
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 16511;
UPDATE `tmp_spell` SET `entry` = 90193, `spellFamilyName` = 8, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Hemorrhage', `nameSubtext` = '', `description` = 'Physical damage taken increased by 2% per stack.', `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `stackAmount` = 4, `durationIndex` = 8, `dmgClass` = 0, `equippedItemClass` = -1, `equippedItemSubClassMask` = 0, `effect1` = 6, `effectApplyAuraName1` = 87, `effectMiscValue1` = 1, `effectBasePoints1` = 1, `effectImplicitTargetA1` = 6, `effect2` = 0, `effectApplyAuraName2` = 0, `effectBasePoints2` = 0, `effect3` = 0, `effectApplyAuraName3` = 0, `effectBasePoints3` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- Proc conditions: Flowing Blades on crit (0x2); Riposte Flow on dodge/parry (0x30);
-- Shadow Edge on normal and critical hits (0x3).
INSERT IGNORE INTO `spell_proc_event` (`entry`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `procFlags`, `procEx`, `ppmRate`, `CustomChance`, `Cooldown`) VALUES
(90154, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90155, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
(90169, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0),
(90170, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0),
(90171, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0),
(90188, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0),
(90189, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0),
(90190, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0);

-- Script bindings on existing spells; each script acts only for casters with the bot aura.
UPDATE `spell_template` SET `script_name` = 'spell_rogue_vigor_energy' WHERE `entry` = 52526 AND `script_name` = '';
UPDATE `spell_template` SET `script_name` = 'spell_rogue_seal_fate_echo' WHERE `entry` = 14189 AND `script_name` = '';
UPDATE `spell_template` SET `script_name` = 'spell_rogue_hemorrhage_stacks' WHERE `entry` = 16511 AND `script_name` = '';
