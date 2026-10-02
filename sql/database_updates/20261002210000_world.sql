-- twow-repo#295 W1 riding ranks (main train 9), owner decisions 2026-10-02 (#295, CLI-295):
--   "Reiten ab Stufe 10/20/40/60 mit +60/100/140/180 %. Ausbildung kostet 50 s / 5 g / 50 g / 500 g."
--   Riding stages: level 10/20/40/60 -> riding skill 762 = 75/150/225/300; training
--   50s/5g/50g/500g (copper 5000/50000/500000/5000000). New ranks 225/300 need new spells.
--   Existing characters keep 75/150 (no character migration); 225/300 are trained.
-- Trainer template 1 (the ten racial riding trainers, one per race):
--   33389 Apprentice Riding (75)    900000 c, level 40 -> 5000 c, level 10
--   33392 Journeyman Riding (150)  9000000 c, level 60 -> 50000 c, level 20
--   61301 Expert Riding (225, new)  500000 c, level 40, needs riding 150
--   61303 Artisan Riding (300, new) 5000000 c, level 60, needs riding 225
-- New spells (block 61300-61399 is reserved for #295; 61310 follows in 20261002213000):
--   61300 Riding "Expert" / 61302 Riding "Artisan": clones of 33391 with SKILL 762 step 3 / 4
--   (effectBasePoints2 2 / 3, dice 1; SpellMgr: riding value = max = step * 75 = 225 / 300).
--   61301 / 61303: clones of 33392 with LEARN_SPELL 61300 / 61302 and SKILL_STEP 762 step 3 / 4.
--   spell_chain 33388 -> 33391 -> 61300 -> 61302 (ranks 3 and 4), so the trainer asks for the
--   previous rank; skill_line_ability 30300 / 30301 (copies of 5071, skill 762).
-- skill_race_class_info_mod 890 = SkillRaceClassInfo.dbc row of skill 762 (MinLevel 40 in the
-- DBC, live and client v6): without the override the trainer shows every riding rank red below
-- level 40. There was no row 890 before (01.10 dump), so there is nothing to back up.
-- The texts of 33389/33392 name the speeds; the core picks the speed (FunserverRidingStages.h,
-- Funserver.Riding.Stages.Enabled): family 1 +60/+100/+100/+100 %, family 2 +60/+100/+140/+180 %.
-- All IDs are below 65536 (16-bit client spell IDs, #455).
-- Coupled release: client patch v7 mirrors 61300-61303, the texts of 33389/33392, the SLA rows
-- 30300/30301 and SkillRaceClassInfo 890 MinLevel 10 (server DBC); rows listed in
-- builds/cli295-riding/evidence/manifest/client-rows-295.tsv.
-- Evidence: builds/cli295-riding (dryrun/ and logs/: hashes before, apply, replay, rollback).
-- Replay-safe: the backups take only the old rows (INSERT IGNORE), inserts are guarded by NOT
-- EXISTS and updates by the old values; the tail asserts the end state.
-- Rollback (exact SQL, in this order; the *_bak_295 tables stay for a later cleanup):
--   UPDATE `npc_trainer_template` t JOIN `npc_trainer_template_bak_295` b ON b.`entry` = t.`entry` AND b.`spell` = t.`spell`
--      SET t.`spellcost` = b.`spellcost`, t.`reqskill` = b.`reqskill`, t.`reqskillvalue` = b.`reqskillvalue`, t.`reqlevel` = b.`reqlevel`
--    WHERE t.`entry` = 1 AND t.`spell` IN (33389, 33392);
--   DELETE FROM `npc_trainer_template` WHERE `entry` = 1 AND `spell` IN (61301, 61303);
--   UPDATE `spell_template` s JOIN `spell_template_bak_295` b ON b.`entry` = s.`entry`
--      SET s.`description` = b.`description` WHERE s.`entry` IN (33389, 33392);
--   DELETE FROM `spell_template` WHERE `entry` IN (61300, 61301, 61302, 61303);
--   DELETE FROM `spell_chain` WHERE `spell_id` IN (61300, 61302);
--   DELETE FROM `skill_line_ability` WHERE `id` IN (30300, 30301);
--   DELETE FROM `skill_race_class_info_mod` WHERE `Id` = 890 AND `Comment` = 'twow-repo#295 riding from level 10';

CREATE TABLE IF NOT EXISTS `npc_trainer_template_bak_295` LIKE `npc_trainer_template`;
INSERT IGNORE INTO `npc_trainer_template_bak_295`
SELECT * FROM `npc_trainer_template`
 WHERE (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) IN
       ((1, 33389, 900000, 0, 0, 40), (1, 33392, 9000000, 762, 0, 60));

CREATE TABLE IF NOT EXISTS `spell_template_bak_295` LIKE `spell_template`;
INSERT IGNORE INTO `spell_template_bak_295`
SELECT * FROM `spell_template`
 WHERE (`entry`, `description`) IN
       ((33389, 'Allows the player to ride basic ground mounts that require a riding skill of 75.'),
        (33392, 'Allows the player to ride swift ground mounts that require a riding skill of 150.'));

-- 61300 Riding "Expert" (225) = 33391 with effectBasePoints2 2 (SKILL step 3) and nameSubtext.
INSERT INTO `spell_template` (`entry`, `school`, `category`, `castUI`, `dispel`, `mechanic`,
  `attributes`, `attributesEx`, `attributesEx2`, `attributesEx3`, `attributesEx4`, `stances`,
  `stancesNot`, `targets`, `targetCreatureType`, `requiresSpellFocus`, `casterAuraState`,
  `targetAuraState`, `castingTimeIndex`, `recoveryTime`, `categoryRecoveryTime`, `interruptFlags`,
  `auraInterruptFlags`, `channelInterruptFlags`, `procFlags`, `procChance`, `procCharges`,
  `maxLevel`, `baseLevel`, `spellLevel`, `durationIndex`, `powerType`, `manaCost`,
  `manCostPerLevel`, `manaPerSecond`, `manaPerSecondPerLevel`, `rangeIndex`, `speed`,
  `modelNextSpell`, `stackAmount`, `totem1`, `totem2`, `reagent1`, `reagent2`, `reagent3`,
  `reagent4`, `reagent5`, `reagent6`, `reagent7`, `reagent8`, `reagentCount1`, `reagentCount2`,
  `reagentCount3`, `reagentCount4`, `reagentCount5`, `reagentCount6`, `reagentCount7`,
  `reagentCount8`, `equippedItemClass`, `equippedItemSubClassMask`,
  `equippedItemInventoryTypeMask`, `effect1`, `effect2`, `effect3`, `effectDieSides1`,
  `effectDieSides2`, `effectDieSides3`, `effectBaseDice1`, `effectBaseDice2`, `effectBaseDice3`,
  `effectDicePerLevel1`, `effectDicePerLevel2`, `effectDicePerLevel3`, `effectRealPointsPerLevel1`,
  `effectRealPointsPerLevel2`, `effectRealPointsPerLevel3`, `effectBasePoints1`,
  `effectBasePoints2`, `effectBasePoints3`, `effectBonusCoefficient1`, `effectBonusCoefficient2`,
  `effectBonusCoefficient3`, `effectMechanic1`, `effectMechanic2`, `effectMechanic3`,
  `effectImplicitTargetA1`, `effectImplicitTargetA2`, `effectImplicitTargetA3`,
  `effectImplicitTargetB1`, `effectImplicitTargetB2`, `effectImplicitTargetB3`,
  `effectRadiusIndex1`, `effectRadiusIndex2`, `effectRadiusIndex3`, `effectApplyAuraName1`,
  `effectApplyAuraName2`, `effectApplyAuraName3`, `effectAmplitude1`, `effectAmplitude2`,
  `effectAmplitude3`, `effectMultipleValue1`, `effectMultipleValue2`, `effectMultipleValue3`,
  `effectChainTarget1`, `effectChainTarget2`, `effectChainTarget3`, `effectItemType1`,
  `effectItemType2`, `effectItemType3`, `effectMiscValue1`, `effectMiscValue2`, `effectMiscValue3`,
  `effectTriggerSpell1`, `effectTriggerSpell2`, `effectTriggerSpell3`,
  `effectPointsPerComboPoint1`, `effectPointsPerComboPoint2`, `effectPointsPerComboPoint3`,
  `spellVisual1`, `spellVisual2`, `spellIconId`, `activeIconId`, `spellPriority`, `name`,
  `nameFlags`, `nameSubtext`, `nameSubtextFlags`, `description`, `descriptionFlags`,
  `auraDescription`, `auraDescriptionFlags`, `manaCostPercentage`, `startRecoveryCategory`,
  `startRecoveryTime`, `minTargetLevel`, `maxTargetLevel`, `spellFamilyName`, `spellFamilyFlags`,
  `maxAffectedTargets`, `dmgClass`, `preventionType`, `stanceBarOrder`, `dmgMultiplier1`,
  `dmgMultiplier2`, `dmgMultiplier3`, `minFactionId`, `minReputation`, `requiredAuraVision`,
  `customFlags`, `script_name`)
SELECT 61300, d.`school`, d.`category`, d.`castUI`, d.`dispel`, d.`mechanic`, d.`attributes`,
  d.`attributesEx`, d.`attributesEx2`, d.`attributesEx3`, d.`attributesEx4`, d.`stances`,
  d.`stancesNot`, d.`targets`, d.`targetCreatureType`, d.`requiresSpellFocus`, d.`casterAuraState`,
  d.`targetAuraState`, d.`castingTimeIndex`, d.`recoveryTime`, d.`categoryRecoveryTime`,
  d.`interruptFlags`, d.`auraInterruptFlags`, d.`channelInterruptFlags`, d.`procFlags`,
  d.`procChance`, d.`procCharges`, d.`maxLevel`, d.`baseLevel`, d.`spellLevel`, d.`durationIndex`,
  d.`powerType`, d.`manaCost`, d.`manCostPerLevel`, d.`manaPerSecond`, d.`manaPerSecondPerLevel`,
  d.`rangeIndex`, d.`speed`, d.`modelNextSpell`, d.`stackAmount`, d.`totem1`, d.`totem2`,
  d.`reagent1`, d.`reagent2`, d.`reagent3`, d.`reagent4`, d.`reagent5`, d.`reagent6`, d.`reagent7`,
  d.`reagent8`, d.`reagentCount1`, d.`reagentCount2`, d.`reagentCount3`, d.`reagentCount4`,
  d.`reagentCount5`, d.`reagentCount6`, d.`reagentCount7`, d.`reagentCount8`,
  d.`equippedItemClass`, d.`equippedItemSubClassMask`, d.`equippedItemInventoryTypeMask`,
  d.`effect1`, d.`effect2`, d.`effect3`, d.`effectDieSides1`, d.`effectDieSides2`,
  d.`effectDieSides3`, d.`effectBaseDice1`, d.`effectBaseDice2`, d.`effectBaseDice3`,
  d.`effectDicePerLevel1`, d.`effectDicePerLevel2`, d.`effectDicePerLevel3`,
  d.`effectRealPointsPerLevel1`, d.`effectRealPointsPerLevel2`, d.`effectRealPointsPerLevel3`,
  d.`effectBasePoints1`, 2, d.`effectBasePoints3`, d.`effectBonusCoefficient1`,
  d.`effectBonusCoefficient2`, d.`effectBonusCoefficient3`, d.`effectMechanic1`,
  d.`effectMechanic2`, d.`effectMechanic3`, d.`effectImplicitTargetA1`, d.`effectImplicitTargetA2`,
  d.`effectImplicitTargetA3`, d.`effectImplicitTargetB1`, d.`effectImplicitTargetB2`,
  d.`effectImplicitTargetB3`, d.`effectRadiusIndex1`, d.`effectRadiusIndex2`,
  d.`effectRadiusIndex3`, d.`effectApplyAuraName1`, d.`effectApplyAuraName2`,
  d.`effectApplyAuraName3`, d.`effectAmplitude1`, d.`effectAmplitude2`, d.`effectAmplitude3`,
  d.`effectMultipleValue1`, d.`effectMultipleValue2`, d.`effectMultipleValue3`,
  d.`effectChainTarget1`, d.`effectChainTarget2`, d.`effectChainTarget3`, d.`effectItemType1`,
  d.`effectItemType2`, d.`effectItemType3`, d.`effectMiscValue1`, d.`effectMiscValue2`,
  d.`effectMiscValue3`, d.`effectTriggerSpell1`, d.`effectTriggerSpell2`, d.`effectTriggerSpell3`,
  d.`effectPointsPerComboPoint1`, d.`effectPointsPerComboPoint2`, d.`effectPointsPerComboPoint3`,
  d.`spellVisual1`, d.`spellVisual2`, d.`spellIconId`, d.`activeIconId`, d.`spellPriority`,
  d.`name`, d.`nameFlags`, 'Expert', d.`nameSubtextFlags`, d.`description`, d.`descriptionFlags`,
  d.`auraDescription`, d.`auraDescriptionFlags`, d.`manaCostPercentage`, d.`startRecoveryCategory`,
  d.`startRecoveryTime`, d.`minTargetLevel`, d.`maxTargetLevel`, d.`spellFamilyName`,
  d.`spellFamilyFlags`, d.`maxAffectedTargets`, d.`dmgClass`, d.`preventionType`,
  d.`stanceBarOrder`, d.`dmgMultiplier1`, d.`dmgMultiplier2`, d.`dmgMultiplier3`, d.`minFactionId`,
  d.`minReputation`, d.`requiredAuraVision`, d.`customFlags`, d.`script_name`
  FROM `spell_template` d
 WHERE d.`entry` = 33391
   AND NOT EXISTS (SELECT 1 FROM `spell_template` x WHERE x.`entry` = 61300);

-- 61302 Riding "Artisan" (300) = 33391 with effectBasePoints2 3 (SKILL step 4) and nameSubtext.
INSERT INTO `spell_template` (`entry`, `school`, `category`, `castUI`, `dispel`, `mechanic`,
  `attributes`, `attributesEx`, `attributesEx2`, `attributesEx3`, `attributesEx4`, `stances`,
  `stancesNot`, `targets`, `targetCreatureType`, `requiresSpellFocus`, `casterAuraState`,
  `targetAuraState`, `castingTimeIndex`, `recoveryTime`, `categoryRecoveryTime`, `interruptFlags`,
  `auraInterruptFlags`, `channelInterruptFlags`, `procFlags`, `procChance`, `procCharges`,
  `maxLevel`, `baseLevel`, `spellLevel`, `durationIndex`, `powerType`, `manaCost`,
  `manCostPerLevel`, `manaPerSecond`, `manaPerSecondPerLevel`, `rangeIndex`, `speed`,
  `modelNextSpell`, `stackAmount`, `totem1`, `totem2`, `reagent1`, `reagent2`, `reagent3`,
  `reagent4`, `reagent5`, `reagent6`, `reagent7`, `reagent8`, `reagentCount1`, `reagentCount2`,
  `reagentCount3`, `reagentCount4`, `reagentCount5`, `reagentCount6`, `reagentCount7`,
  `reagentCount8`, `equippedItemClass`, `equippedItemSubClassMask`,
  `equippedItemInventoryTypeMask`, `effect1`, `effect2`, `effect3`, `effectDieSides1`,
  `effectDieSides2`, `effectDieSides3`, `effectBaseDice1`, `effectBaseDice2`, `effectBaseDice3`,
  `effectDicePerLevel1`, `effectDicePerLevel2`, `effectDicePerLevel3`, `effectRealPointsPerLevel1`,
  `effectRealPointsPerLevel2`, `effectRealPointsPerLevel3`, `effectBasePoints1`,
  `effectBasePoints2`, `effectBasePoints3`, `effectBonusCoefficient1`, `effectBonusCoefficient2`,
  `effectBonusCoefficient3`, `effectMechanic1`, `effectMechanic2`, `effectMechanic3`,
  `effectImplicitTargetA1`, `effectImplicitTargetA2`, `effectImplicitTargetA3`,
  `effectImplicitTargetB1`, `effectImplicitTargetB2`, `effectImplicitTargetB3`,
  `effectRadiusIndex1`, `effectRadiusIndex2`, `effectRadiusIndex3`, `effectApplyAuraName1`,
  `effectApplyAuraName2`, `effectApplyAuraName3`, `effectAmplitude1`, `effectAmplitude2`,
  `effectAmplitude3`, `effectMultipleValue1`, `effectMultipleValue2`, `effectMultipleValue3`,
  `effectChainTarget1`, `effectChainTarget2`, `effectChainTarget3`, `effectItemType1`,
  `effectItemType2`, `effectItemType3`, `effectMiscValue1`, `effectMiscValue2`, `effectMiscValue3`,
  `effectTriggerSpell1`, `effectTriggerSpell2`, `effectTriggerSpell3`,
  `effectPointsPerComboPoint1`, `effectPointsPerComboPoint2`, `effectPointsPerComboPoint3`,
  `spellVisual1`, `spellVisual2`, `spellIconId`, `activeIconId`, `spellPriority`, `name`,
  `nameFlags`, `nameSubtext`, `nameSubtextFlags`, `description`, `descriptionFlags`,
  `auraDescription`, `auraDescriptionFlags`, `manaCostPercentage`, `startRecoveryCategory`,
  `startRecoveryTime`, `minTargetLevel`, `maxTargetLevel`, `spellFamilyName`, `spellFamilyFlags`,
  `maxAffectedTargets`, `dmgClass`, `preventionType`, `stanceBarOrder`, `dmgMultiplier1`,
  `dmgMultiplier2`, `dmgMultiplier3`, `minFactionId`, `minReputation`, `requiredAuraVision`,
  `customFlags`, `script_name`)
SELECT 61302, d.`school`, d.`category`, d.`castUI`, d.`dispel`, d.`mechanic`, d.`attributes`,
  d.`attributesEx`, d.`attributesEx2`, d.`attributesEx3`, d.`attributesEx4`, d.`stances`,
  d.`stancesNot`, d.`targets`, d.`targetCreatureType`, d.`requiresSpellFocus`, d.`casterAuraState`,
  d.`targetAuraState`, d.`castingTimeIndex`, d.`recoveryTime`, d.`categoryRecoveryTime`,
  d.`interruptFlags`, d.`auraInterruptFlags`, d.`channelInterruptFlags`, d.`procFlags`,
  d.`procChance`, d.`procCharges`, d.`maxLevel`, d.`baseLevel`, d.`spellLevel`, d.`durationIndex`,
  d.`powerType`, d.`manaCost`, d.`manCostPerLevel`, d.`manaPerSecond`, d.`manaPerSecondPerLevel`,
  d.`rangeIndex`, d.`speed`, d.`modelNextSpell`, d.`stackAmount`, d.`totem1`, d.`totem2`,
  d.`reagent1`, d.`reagent2`, d.`reagent3`, d.`reagent4`, d.`reagent5`, d.`reagent6`, d.`reagent7`,
  d.`reagent8`, d.`reagentCount1`, d.`reagentCount2`, d.`reagentCount3`, d.`reagentCount4`,
  d.`reagentCount5`, d.`reagentCount6`, d.`reagentCount7`, d.`reagentCount8`,
  d.`equippedItemClass`, d.`equippedItemSubClassMask`, d.`equippedItemInventoryTypeMask`,
  d.`effect1`, d.`effect2`, d.`effect3`, d.`effectDieSides1`, d.`effectDieSides2`,
  d.`effectDieSides3`, d.`effectBaseDice1`, d.`effectBaseDice2`, d.`effectBaseDice3`,
  d.`effectDicePerLevel1`, d.`effectDicePerLevel2`, d.`effectDicePerLevel3`,
  d.`effectRealPointsPerLevel1`, d.`effectRealPointsPerLevel2`, d.`effectRealPointsPerLevel3`,
  d.`effectBasePoints1`, 3, d.`effectBasePoints3`, d.`effectBonusCoefficient1`,
  d.`effectBonusCoefficient2`, d.`effectBonusCoefficient3`, d.`effectMechanic1`,
  d.`effectMechanic2`, d.`effectMechanic3`, d.`effectImplicitTargetA1`, d.`effectImplicitTargetA2`,
  d.`effectImplicitTargetA3`, d.`effectImplicitTargetB1`, d.`effectImplicitTargetB2`,
  d.`effectImplicitTargetB3`, d.`effectRadiusIndex1`, d.`effectRadiusIndex2`,
  d.`effectRadiusIndex3`, d.`effectApplyAuraName1`, d.`effectApplyAuraName2`,
  d.`effectApplyAuraName3`, d.`effectAmplitude1`, d.`effectAmplitude2`, d.`effectAmplitude3`,
  d.`effectMultipleValue1`, d.`effectMultipleValue2`, d.`effectMultipleValue3`,
  d.`effectChainTarget1`, d.`effectChainTarget2`, d.`effectChainTarget3`, d.`effectItemType1`,
  d.`effectItemType2`, d.`effectItemType3`, d.`effectMiscValue1`, d.`effectMiscValue2`,
  d.`effectMiscValue3`, d.`effectTriggerSpell1`, d.`effectTriggerSpell2`, d.`effectTriggerSpell3`,
  d.`effectPointsPerComboPoint1`, d.`effectPointsPerComboPoint2`, d.`effectPointsPerComboPoint3`,
  d.`spellVisual1`, d.`spellVisual2`, d.`spellIconId`, d.`activeIconId`, d.`spellPriority`,
  d.`name`, d.`nameFlags`, 'Artisan', d.`nameSubtextFlags`, d.`description`, d.`descriptionFlags`,
  d.`auraDescription`, d.`auraDescriptionFlags`, d.`manaCostPercentage`, d.`startRecoveryCategory`,
  d.`startRecoveryTime`, d.`minTargetLevel`, d.`maxTargetLevel`, d.`spellFamilyName`,
  d.`spellFamilyFlags`, d.`maxAffectedTargets`, d.`dmgClass`, d.`preventionType`,
  d.`stanceBarOrder`, d.`dmgMultiplier1`, d.`dmgMultiplier2`, d.`dmgMultiplier3`, d.`minFactionId`,
  d.`minReputation`, d.`requiredAuraVision`, d.`customFlags`, d.`script_name`
  FROM `spell_template` d
 WHERE d.`entry` = 33391
   AND NOT EXISTS (SELECT 1 FROM `spell_template` x WHERE x.`entry` = 61302);

-- 61301 Expert Riding = 33392 with LEARN_SPELL 61300, SKILL_STEP step 3, name and text.
INSERT INTO `spell_template` (`entry`, `school`, `category`, `castUI`, `dispel`, `mechanic`,
  `attributes`, `attributesEx`, `attributesEx2`, `attributesEx3`, `attributesEx4`, `stances`,
  `stancesNot`, `targets`, `targetCreatureType`, `requiresSpellFocus`, `casterAuraState`,
  `targetAuraState`, `castingTimeIndex`, `recoveryTime`, `categoryRecoveryTime`, `interruptFlags`,
  `auraInterruptFlags`, `channelInterruptFlags`, `procFlags`, `procChance`, `procCharges`,
  `maxLevel`, `baseLevel`, `spellLevel`, `durationIndex`, `powerType`, `manaCost`,
  `manCostPerLevel`, `manaPerSecond`, `manaPerSecondPerLevel`, `rangeIndex`, `speed`,
  `modelNextSpell`, `stackAmount`, `totem1`, `totem2`, `reagent1`, `reagent2`, `reagent3`,
  `reagent4`, `reagent5`, `reagent6`, `reagent7`, `reagent8`, `reagentCount1`, `reagentCount2`,
  `reagentCount3`, `reagentCount4`, `reagentCount5`, `reagentCount6`, `reagentCount7`,
  `reagentCount8`, `equippedItemClass`, `equippedItemSubClassMask`,
  `equippedItemInventoryTypeMask`, `effect1`, `effect2`, `effect3`, `effectDieSides1`,
  `effectDieSides2`, `effectDieSides3`, `effectBaseDice1`, `effectBaseDice2`, `effectBaseDice3`,
  `effectDicePerLevel1`, `effectDicePerLevel2`, `effectDicePerLevel3`, `effectRealPointsPerLevel1`,
  `effectRealPointsPerLevel2`, `effectRealPointsPerLevel3`, `effectBasePoints1`,
  `effectBasePoints2`, `effectBasePoints3`, `effectBonusCoefficient1`, `effectBonusCoefficient2`,
  `effectBonusCoefficient3`, `effectMechanic1`, `effectMechanic2`, `effectMechanic3`,
  `effectImplicitTargetA1`, `effectImplicitTargetA2`, `effectImplicitTargetA3`,
  `effectImplicitTargetB1`, `effectImplicitTargetB2`, `effectImplicitTargetB3`,
  `effectRadiusIndex1`, `effectRadiusIndex2`, `effectRadiusIndex3`, `effectApplyAuraName1`,
  `effectApplyAuraName2`, `effectApplyAuraName3`, `effectAmplitude1`, `effectAmplitude2`,
  `effectAmplitude3`, `effectMultipleValue1`, `effectMultipleValue2`, `effectMultipleValue3`,
  `effectChainTarget1`, `effectChainTarget2`, `effectChainTarget3`, `effectItemType1`,
  `effectItemType2`, `effectItemType3`, `effectMiscValue1`, `effectMiscValue2`, `effectMiscValue3`,
  `effectTriggerSpell1`, `effectTriggerSpell2`, `effectTriggerSpell3`,
  `effectPointsPerComboPoint1`, `effectPointsPerComboPoint2`, `effectPointsPerComboPoint3`,
  `spellVisual1`, `spellVisual2`, `spellIconId`, `activeIconId`, `spellPriority`, `name`,
  `nameFlags`, `nameSubtext`, `nameSubtextFlags`, `description`, `descriptionFlags`,
  `auraDescription`, `auraDescriptionFlags`, `manaCostPercentage`, `startRecoveryCategory`,
  `startRecoveryTime`, `minTargetLevel`, `maxTargetLevel`, `spellFamilyName`, `spellFamilyFlags`,
  `maxAffectedTargets`, `dmgClass`, `preventionType`, `stanceBarOrder`, `dmgMultiplier1`,
  `dmgMultiplier2`, `dmgMultiplier3`, `minFactionId`, `minReputation`, `requiredAuraVision`,
  `customFlags`, `script_name`)
SELECT 61301, d.`school`, d.`category`, d.`castUI`, d.`dispel`, d.`mechanic`, d.`attributes`,
  d.`attributesEx`, d.`attributesEx2`, d.`attributesEx3`, d.`attributesEx4`, d.`stances`,
  d.`stancesNot`, d.`targets`, d.`targetCreatureType`, d.`requiresSpellFocus`, d.`casterAuraState`,
  d.`targetAuraState`, d.`castingTimeIndex`, d.`recoveryTime`, d.`categoryRecoveryTime`,
  d.`interruptFlags`, d.`auraInterruptFlags`, d.`channelInterruptFlags`, d.`procFlags`,
  d.`procChance`, d.`procCharges`, d.`maxLevel`, d.`baseLevel`, d.`spellLevel`, d.`durationIndex`,
  d.`powerType`, d.`manaCost`, d.`manCostPerLevel`, d.`manaPerSecond`, d.`manaPerSecondPerLevel`,
  d.`rangeIndex`, d.`speed`, d.`modelNextSpell`, d.`stackAmount`, d.`totem1`, d.`totem2`,
  d.`reagent1`, d.`reagent2`, d.`reagent3`, d.`reagent4`, d.`reagent5`, d.`reagent6`, d.`reagent7`,
  d.`reagent8`, d.`reagentCount1`, d.`reagentCount2`, d.`reagentCount3`, d.`reagentCount4`,
  d.`reagentCount5`, d.`reagentCount6`, d.`reagentCount7`, d.`reagentCount8`,
  d.`equippedItemClass`, d.`equippedItemSubClassMask`, d.`equippedItemInventoryTypeMask`,
  d.`effect1`, d.`effect2`, d.`effect3`, d.`effectDieSides1`, d.`effectDieSides2`,
  d.`effectDieSides3`, d.`effectBaseDice1`, d.`effectBaseDice2`, d.`effectBaseDice3`,
  d.`effectDicePerLevel1`, d.`effectDicePerLevel2`, d.`effectDicePerLevel3`,
  d.`effectRealPointsPerLevel1`, d.`effectRealPointsPerLevel2`, d.`effectRealPointsPerLevel3`,
  d.`effectBasePoints1`, 2, d.`effectBasePoints3`, d.`effectBonusCoefficient1`,
  d.`effectBonusCoefficient2`, d.`effectBonusCoefficient3`, d.`effectMechanic1`,
  d.`effectMechanic2`, d.`effectMechanic3`, d.`effectImplicitTargetA1`, d.`effectImplicitTargetA2`,
  d.`effectImplicitTargetA3`, d.`effectImplicitTargetB1`, d.`effectImplicitTargetB2`,
  d.`effectImplicitTargetB3`, d.`effectRadiusIndex1`, d.`effectRadiusIndex2`,
  d.`effectRadiusIndex3`, d.`effectApplyAuraName1`, d.`effectApplyAuraName2`,
  d.`effectApplyAuraName3`, d.`effectAmplitude1`, d.`effectAmplitude2`, d.`effectAmplitude3`,
  d.`effectMultipleValue1`, d.`effectMultipleValue2`, d.`effectMultipleValue3`,
  d.`effectChainTarget1`, d.`effectChainTarget2`, d.`effectChainTarget3`, d.`effectItemType1`,
  d.`effectItemType2`, d.`effectItemType3`, d.`effectMiscValue1`, d.`effectMiscValue2`,
  d.`effectMiscValue3`, 61300, d.`effectTriggerSpell2`, d.`effectTriggerSpell3`,
  d.`effectPointsPerComboPoint1`, d.`effectPointsPerComboPoint2`, d.`effectPointsPerComboPoint3`,
  d.`spellVisual1`, d.`spellVisual2`, d.`spellIconId`, d.`activeIconId`, d.`spellPriority`,
  'Expert Riding', d.`nameFlags`, d.`nameSubtext`, d.`nameSubtextFlags`,
  'Allows the player to ride swift ground mounts that require a riding skill of 225. Swift mounts run 140% faster, other mounts stay at 100%.',
  d.`descriptionFlags`, d.`auraDescription`, d.`auraDescriptionFlags`, d.`manaCostPercentage`,
  d.`startRecoveryCategory`, d.`startRecoveryTime`, d.`minTargetLevel`, d.`maxTargetLevel`,
  d.`spellFamilyName`, d.`spellFamilyFlags`, d.`maxAffectedTargets`, d.`dmgClass`,
  d.`preventionType`, d.`stanceBarOrder`, d.`dmgMultiplier1`, d.`dmgMultiplier2`,
  d.`dmgMultiplier3`, d.`minFactionId`, d.`minReputation`, d.`requiredAuraVision`, d.`customFlags`,
  d.`script_name`
  FROM `spell_template` d
 WHERE d.`entry` = 33392
   AND NOT EXISTS (SELECT 1 FROM `spell_template` x WHERE x.`entry` = 61301);

-- 61303 Artisan Riding = 33392 with LEARN_SPELL 61302, SKILL_STEP step 4, name and text.
INSERT INTO `spell_template` (`entry`, `school`, `category`, `castUI`, `dispel`, `mechanic`,
  `attributes`, `attributesEx`, `attributesEx2`, `attributesEx3`, `attributesEx4`, `stances`,
  `stancesNot`, `targets`, `targetCreatureType`, `requiresSpellFocus`, `casterAuraState`,
  `targetAuraState`, `castingTimeIndex`, `recoveryTime`, `categoryRecoveryTime`, `interruptFlags`,
  `auraInterruptFlags`, `channelInterruptFlags`, `procFlags`, `procChance`, `procCharges`,
  `maxLevel`, `baseLevel`, `spellLevel`, `durationIndex`, `powerType`, `manaCost`,
  `manCostPerLevel`, `manaPerSecond`, `manaPerSecondPerLevel`, `rangeIndex`, `speed`,
  `modelNextSpell`, `stackAmount`, `totem1`, `totem2`, `reagent1`, `reagent2`, `reagent3`,
  `reagent4`, `reagent5`, `reagent6`, `reagent7`, `reagent8`, `reagentCount1`, `reagentCount2`,
  `reagentCount3`, `reagentCount4`, `reagentCount5`, `reagentCount6`, `reagentCount7`,
  `reagentCount8`, `equippedItemClass`, `equippedItemSubClassMask`,
  `equippedItemInventoryTypeMask`, `effect1`, `effect2`, `effect3`, `effectDieSides1`,
  `effectDieSides2`, `effectDieSides3`, `effectBaseDice1`, `effectBaseDice2`, `effectBaseDice3`,
  `effectDicePerLevel1`, `effectDicePerLevel2`, `effectDicePerLevel3`, `effectRealPointsPerLevel1`,
  `effectRealPointsPerLevel2`, `effectRealPointsPerLevel3`, `effectBasePoints1`,
  `effectBasePoints2`, `effectBasePoints3`, `effectBonusCoefficient1`, `effectBonusCoefficient2`,
  `effectBonusCoefficient3`, `effectMechanic1`, `effectMechanic2`, `effectMechanic3`,
  `effectImplicitTargetA1`, `effectImplicitTargetA2`, `effectImplicitTargetA3`,
  `effectImplicitTargetB1`, `effectImplicitTargetB2`, `effectImplicitTargetB3`,
  `effectRadiusIndex1`, `effectRadiusIndex2`, `effectRadiusIndex3`, `effectApplyAuraName1`,
  `effectApplyAuraName2`, `effectApplyAuraName3`, `effectAmplitude1`, `effectAmplitude2`,
  `effectAmplitude3`, `effectMultipleValue1`, `effectMultipleValue2`, `effectMultipleValue3`,
  `effectChainTarget1`, `effectChainTarget2`, `effectChainTarget3`, `effectItemType1`,
  `effectItemType2`, `effectItemType3`, `effectMiscValue1`, `effectMiscValue2`, `effectMiscValue3`,
  `effectTriggerSpell1`, `effectTriggerSpell2`, `effectTriggerSpell3`,
  `effectPointsPerComboPoint1`, `effectPointsPerComboPoint2`, `effectPointsPerComboPoint3`,
  `spellVisual1`, `spellVisual2`, `spellIconId`, `activeIconId`, `spellPriority`, `name`,
  `nameFlags`, `nameSubtext`, `nameSubtextFlags`, `description`, `descriptionFlags`,
  `auraDescription`, `auraDescriptionFlags`, `manaCostPercentage`, `startRecoveryCategory`,
  `startRecoveryTime`, `minTargetLevel`, `maxTargetLevel`, `spellFamilyName`, `spellFamilyFlags`,
  `maxAffectedTargets`, `dmgClass`, `preventionType`, `stanceBarOrder`, `dmgMultiplier1`,
  `dmgMultiplier2`, `dmgMultiplier3`, `minFactionId`, `minReputation`, `requiredAuraVision`,
  `customFlags`, `script_name`)
SELECT 61303, d.`school`, d.`category`, d.`castUI`, d.`dispel`, d.`mechanic`, d.`attributes`,
  d.`attributesEx`, d.`attributesEx2`, d.`attributesEx3`, d.`attributesEx4`, d.`stances`,
  d.`stancesNot`, d.`targets`, d.`targetCreatureType`, d.`requiresSpellFocus`, d.`casterAuraState`,
  d.`targetAuraState`, d.`castingTimeIndex`, d.`recoveryTime`, d.`categoryRecoveryTime`,
  d.`interruptFlags`, d.`auraInterruptFlags`, d.`channelInterruptFlags`, d.`procFlags`,
  d.`procChance`, d.`procCharges`, d.`maxLevel`, d.`baseLevel`, d.`spellLevel`, d.`durationIndex`,
  d.`powerType`, d.`manaCost`, d.`manCostPerLevel`, d.`manaPerSecond`, d.`manaPerSecondPerLevel`,
  d.`rangeIndex`, d.`speed`, d.`modelNextSpell`, d.`stackAmount`, d.`totem1`, d.`totem2`,
  d.`reagent1`, d.`reagent2`, d.`reagent3`, d.`reagent4`, d.`reagent5`, d.`reagent6`, d.`reagent7`,
  d.`reagent8`, d.`reagentCount1`, d.`reagentCount2`, d.`reagentCount3`, d.`reagentCount4`,
  d.`reagentCount5`, d.`reagentCount6`, d.`reagentCount7`, d.`reagentCount8`,
  d.`equippedItemClass`, d.`equippedItemSubClassMask`, d.`equippedItemInventoryTypeMask`,
  d.`effect1`, d.`effect2`, d.`effect3`, d.`effectDieSides1`, d.`effectDieSides2`,
  d.`effectDieSides3`, d.`effectBaseDice1`, d.`effectBaseDice2`, d.`effectBaseDice3`,
  d.`effectDicePerLevel1`, d.`effectDicePerLevel2`, d.`effectDicePerLevel3`,
  d.`effectRealPointsPerLevel1`, d.`effectRealPointsPerLevel2`, d.`effectRealPointsPerLevel3`,
  d.`effectBasePoints1`, 3, d.`effectBasePoints3`, d.`effectBonusCoefficient1`,
  d.`effectBonusCoefficient2`, d.`effectBonusCoefficient3`, d.`effectMechanic1`,
  d.`effectMechanic2`, d.`effectMechanic3`, d.`effectImplicitTargetA1`, d.`effectImplicitTargetA2`,
  d.`effectImplicitTargetA3`, d.`effectImplicitTargetB1`, d.`effectImplicitTargetB2`,
  d.`effectImplicitTargetB3`, d.`effectRadiusIndex1`, d.`effectRadiusIndex2`,
  d.`effectRadiusIndex3`, d.`effectApplyAuraName1`, d.`effectApplyAuraName2`,
  d.`effectApplyAuraName3`, d.`effectAmplitude1`, d.`effectAmplitude2`, d.`effectAmplitude3`,
  d.`effectMultipleValue1`, d.`effectMultipleValue2`, d.`effectMultipleValue3`,
  d.`effectChainTarget1`, d.`effectChainTarget2`, d.`effectChainTarget3`, d.`effectItemType1`,
  d.`effectItemType2`, d.`effectItemType3`, d.`effectMiscValue1`, d.`effectMiscValue2`,
  d.`effectMiscValue3`, 61302, d.`effectTriggerSpell2`, d.`effectTriggerSpell3`,
  d.`effectPointsPerComboPoint1`, d.`effectPointsPerComboPoint2`, d.`effectPointsPerComboPoint3`,
  d.`spellVisual1`, d.`spellVisual2`, d.`spellIconId`, d.`activeIconId`, d.`spellPriority`,
  'Artisan Riding', d.`nameFlags`, d.`nameSubtext`, d.`nameSubtextFlags`,
  'Raises the riding skill to 300. Swift mounts run 180% faster, other mounts stay at 100%.',
  d.`descriptionFlags`, d.`auraDescription`, d.`auraDescriptionFlags`, d.`manaCostPercentage`,
  d.`startRecoveryCategory`, d.`startRecoveryTime`, d.`minTargetLevel`, d.`maxTargetLevel`,
  d.`spellFamilyName`, d.`spellFamilyFlags`, d.`maxAffectedTargets`, d.`dmgClass`,
  d.`preventionType`, d.`stanceBarOrder`, d.`dmgMultiplier1`, d.`dmgMultiplier2`,
  d.`dmgMultiplier3`, d.`minFactionId`, d.`minReputation`, d.`requiredAuraVision`, d.`customFlags`,
  d.`script_name`
  FROM `spell_template` d
 WHERE d.`entry` = 33392
   AND NOT EXISTS (SELECT 1 FROM `spell_template` x WHERE x.`entry` = 61303);

INSERT INTO `spell_chain` (`spell_id`, `prev_spell`, `first_spell`, `rank`, `req_spell`)
SELECT 61300, 33391, 33388, 3, 0 FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `spell_chain` x WHERE x.`spell_id` = 61300);
INSERT INTO `spell_chain` (`spell_id`, `prev_spell`, `first_spell`, `rank`, `req_spell`)
SELECT 61302, 61300, 33388, 4, 0 FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `spell_chain` x WHERE x.`spell_id` = 61302);

INSERT INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`,
  `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`)
SELECT 30300, d.`skill_id`, 61300, d.`race_mask`, d.`class_mask`, d.`req_skill_value`,
  d.`superseded_by_spell`, d.`learn_on_get_skill`, d.`max_value`, d.`min_value`, d.`req_train_points`
  FROM `skill_line_ability` d
 WHERE d.`id` = 5071 AND d.`skill_id` = 762 AND d.`spell_id` = 33391
   AND NOT EXISTS (SELECT 1 FROM `skill_line_ability` x WHERE x.`id` = 30300 OR x.`spell_id` = 61300);
INSERT INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`,
  `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`)
SELECT 30301, d.`skill_id`, 61302, d.`race_mask`, d.`class_mask`, d.`req_skill_value`,
  d.`superseded_by_spell`, d.`learn_on_get_skill`, d.`max_value`, d.`min_value`, d.`req_train_points`
  FROM `skill_line_ability` d
 WHERE d.`id` = 5071 AND d.`skill_id` = 762 AND d.`spell_id` = 33391
   AND NOT EXISTS (SELECT 1 FROM `skill_line_ability` x WHERE x.`id` = 30301 OR x.`spell_id` = 61302);

INSERT INTO `skill_race_class_info_mod` (`Id`, `SkillLineDbcRecord`, `RaceMask`, `ClassMask`, `Flags`,
  `MinLevel`, `SkillTierId`, `SkillCostIndex`, `Comment`)
SELECT 890, -1, -1, -1, -1, 10, -1, -1, 'twow-repo#295 riding from level 10' FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `skill_race_class_info_mod` x WHERE x.`Id` = 890);

UPDATE `npc_trainer_template` SET `spellcost` = 5000, `reqlevel` = 10
 WHERE `entry` = 1 AND `spell` = 33389
   AND `spellcost` = 900000 AND `reqskill` = 0 AND `reqskillvalue` = 0 AND `reqlevel` = 40;
UPDATE `npc_trainer_template` SET `spellcost` = 50000, `reqlevel` = 20
 WHERE `entry` = 1 AND `spell` = 33392
   AND `spellcost` = 9000000 AND `reqskill` = 762 AND `reqskillvalue` = 0 AND `reqlevel` = 60;
INSERT INTO `npc_trainer_template` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`)
SELECT 1, 61301, 500000, 762, 150, 40 FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `npc_trainer_template` x WHERE x.`entry` = 1 AND x.`spell` = 61301);
INSERT INTO `npc_trainer_template` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`)
SELECT 1, 61303, 5000000, 762, 225, 60 FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `npc_trainer_template` x WHERE x.`entry` = 1 AND x.`spell` = 61303);

UPDATE `spell_template`
   SET `description` = 'Allows the player to ride basic ground mounts that require a riding skill of 75. Mounts run 60% faster.'
 WHERE `entry` = 33389 AND `description` = 'Allows the player to ride basic ground mounts that require a riding skill of 75.';
UPDATE `spell_template`
   SET `description` = 'Raises the riding skill to 150. All mounts run 100% faster.'
 WHERE `entry` = 33392 AND `description` = 'Allows the player to ride swift ground mounts that require a riding skill of 150.';

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_295_w1` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_295_w1` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `effectBasePoints2`, `nameSubtext`) IN ((61300, 2, 'Expert'), (61302, 3, 'Artisan'))
           AND `name` = 'Riding' AND `effect2` = 118 AND `effectMiscValue2` = 762
           AND `effectBaseDice2` = 1 AND `effectDieSides2` = 1) = 2
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `effectTriggerSpell1`, `effectBasePoints2`, `name`) IN
               ((61301, 61300, 2, 'Expert Riding'), (61303, 61302, 3, 'Artisan Riding'))
           AND `effect1` = 36 AND `effect2` = 44 AND `effectMiscValue2` = 762
           AND `effectBaseDice2` = 1 AND `effectDieSides2` = 1 AND `spellVisual1` = 107) = 2
   AND (SELECT COUNT(*) FROM `spell_chain`
         WHERE (`spell_id`, `prev_spell`, `first_spell`, `rank`) IN ((61300, 33391, 33388, 3), (61302, 61300, 33388, 4))) = 2
   AND (SELECT COUNT(*) FROM `skill_line_ability`
         WHERE (`id`, `skill_id`, `spell_id`) IN ((30300, 762, 61300), (30301, 762, 61302))) = 2
   AND (SELECT COUNT(*) FROM `skill_line_ability` WHERE `spell_id` IN (61300, 61302)) = 2
   AND (SELECT COUNT(*) FROM `skill_race_class_info_mod`
         WHERE `Id` = 890 AND `MinLevel` = 10 AND `SkillLineDbcRecord` = -1 AND `SkillCostIndex` = -1) = 1
   AND (SELECT COUNT(*) FROM `npc_trainer_template`
         WHERE (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) IN
               ((1, 33389, 5000, 0, 0, 10), (1, 33392, 50000, 762, 0, 20),
                (1, 61301, 500000, 762, 150, 40), (1, 61303, 5000000, 762, 225, 60))) = 4
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `description`) IN
               ((33389, 'Allows the player to ride basic ground mounts that require a riding skill of 75. Mounts run 60% faster.'),
                (33392, 'Raises the riding skill to 150. All mounts run 100% faster.'))) = 2
   AND (SELECT COUNT(*) FROM `npc_trainer_template_bak_295` WHERE `entry` = 1 AND `spell` IN (33389, 33392)) = 2
   AND (SELECT COUNT(*) FROM `spell_template_bak_295` WHERE `entry` IN (33389, 33392)) = 2;
