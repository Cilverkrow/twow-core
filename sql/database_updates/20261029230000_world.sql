-- twow-repo#654 (WS10-ROGUE-TANK-STANCE-01, parent #367; owner 29.10.2026 via OB-00: "Als Tank-Haltung", "Alles nur in Haltung",
-- name "Shadow Dance", "ja aber als schalter"): the spells of the Shadow Dance rogue tank stance. World data, main train only (Ae15);
-- the code is the twin pair #800 (release/9.x) / #801 (main), which treats these rows as missing until they exist (both switches
-- act as 0 then). Nothing is trained, sold or dropped here: the core (Funserver.Rogue.ShadowDanceStance 1) or the bot code
-- (AiPlayerbot.Rotation.RogueTankStance with core switch 2) teaches 61230; 61231 / 61232 are only cast by the Shadow Dance script.
--
--   61230  Shadow Dance (the stance): self buff, unlimited, cancelable, not passive, energy 0, rank-free (fixed values).
--          effect 1: aura 79 MOD_DAMAGE_PERCENT_DONE, all schools (misc 127), -40 % (bp -41 + 1 die) - Riposte Flow hits included.
--          effect 2: aura 87 MOD_DAMAGE_PERCENT_TAKEN, all schools (misc 127), -25 % (bp -26 + 1 die).
--          script spell_rogue_shadow_dance_stance (refused while the switch does not cover the caster; ends rogue Stealth).
--          Clone of Righteous Fury 25780 (the same template as the ranks 61143-61145), family 0, school physical, icon 252.
--   61231  Shadow Dance: energy regeneration -10 % for 3 s (aura 110 MOD_POWER_REGEN_PERCENT, misc 3 = energy, bp -11 + 1 die),
--          next to the dodge buff 61146 (after a parry).
--   61232  the same next to the parry buff 61147 (after a dodge). Two separate spells: at most two at once, multiplied (0.81).
--          Clones of 23547 like 61146 / 61147 (3 s = durationIndex 27), family 0, icon 1 (SpellMgr's generic no-stack rule
--          skips icon 1, so the helpers never replace each other or a buff), SPELL_ATTR_CANT_CANCEL (0x80000000): a penalty
--          the player cannot right-click away.
-- Client: these spells need the client patch (Spell.dbc) before players see them (Funserver.Rogue.ShadowDanceStance 1); bots
-- need none (switch 2). Replay-safe: clones via INSERT IGNORE into free ids (61230+ was free, 20261003200000). The end-state
-- CHECK at the bottom fails the file if a row did not reach its target (the updater ignores statement results).
-- Rollback: DELETE FROM `spell_template` WHERE `entry` IN (61230, 61231, 61232); with both switches at 0 nothing references them.

-- 61230 the stance
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 25780;
UPDATE `tmp_spell` SET `entry` = 61230, `school` = 0, `spellFamilyName` = 0, `spellFamilyFlags` = 0,
    `script_name` = 'spell_rogue_shadow_dance_stance',
    `name` = 'Shadow Dance', `nameSubtext` = '',
    `description` = 'A tank stance. Damage done reduced by 40% and damage taken reduced by 25%. While it lasts, parrying grants 5% dodge and dodging grants 5% parry for 3 sec, each reducing energy regeneration by 10%. Stealth and Vanish cannot be used.',
    `auraDescription` = 'Damage done reduced by 40%, damage taken reduced by 25%. Cannot stealth.',
    `spellLevel` = 20, `baseLevel` = 20, `maxLevel` = 0, `durationIndex` = 21, `powerType` = 3, `manaCost` = 0,
    `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `stackAmount` = 0, `spellIconId` = 252, `activeIconId` = 0,
    `effect1` = 6, `effectApplyAuraName1` = 79, `effectMiscValue1` = 127, `effectBasePoints1` = -41,
    `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectImplicitTargetA1` = 1, `effectImplicitTargetB1` = 0, `effectTriggerSpell1` = 0,
    `effect2` = 6, `effectApplyAuraName2` = 87, `effectMiscValue2` = 127, `effectBasePoints2` = -26,
    `effectDieSides2` = 1, `effectBaseDice2` = 1, `effectImplicitTargetA2` = 1, `effectImplicitTargetB2` = 0, `effectTriggerSpell2` = 0,
    `effect3` = 0, `effectApplyAuraName3` = 0, `effectMiscValue3` = 0, `effectBasePoints3` = 0, `effectTriggerSpell3` = 0;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- 61231 energy helper after a parry (with the dodge buff 61146)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 23547;
UPDATE `tmp_spell` SET `entry` = 61231, `spellFamilyName` = 0, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Energy regeneration reduced by 10%.',
    `auraDescription` = 'Energy regeneration reduced by 10%.', `attributes` = 2147483648,
    `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `durationIndex` = 27, `spellIconId` = 1, `spellVisual1` = 0,
    `effect1` = 6, `effectApplyAuraName1` = 110, `effectMiscValue1` = 3, `effectBasePoints1` = -11,
    `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectImplicitTargetA1` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- 61232 energy helper after a dodge (with the parry buff 61147)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 23547;
UPDATE `tmp_spell` SET `entry` = 61232, `spellFamilyName` = 0, `spellFamilyFlags` = 0, `script_name` = '',
    `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Energy regeneration reduced by 10%.',
    `auraDescription` = 'Energy regeneration reduced by 10%.', `attributes` = 2147483648,
    `procFlags` = 0, `procChance` = 101, `procCharges` = 0, `durationIndex` = 27, `spellIconId` = 1, `spellVisual1` = 0,
    `effect1` = 6, `effectApplyAuraName1` = 110, `effectMiscValue1` = 3, `effectBasePoints1` = -11,
    `effectDieSides1` = 1, `effectBaseDice1` = 1, `effectImplicitTargetA1` = 1;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;

-- End state (the updater ignores statement results; the CHECK turns a wrong end state into an error).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_654_stance` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_654_stance` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` = 61230 AND `name` = 'Shadow Dance' AND `script_name` = 'spell_rogue_shadow_dance_stance'
           AND (`attributes` & 64) = 0 AND `durationIndex` = 21 AND `spellFamilyName` = 0 AND `spellLevel` = 20
           AND `effect1` = 6 AND `effectApplyAuraName1` = 79 AND `effectMiscValue1` = 127 AND `effectBasePoints1` = -41
           AND `effectDieSides1` = 1 AND `effectImplicitTargetA1` = 1
           AND `effect2` = 6 AND `effectApplyAuraName2` = 87 AND `effectMiscValue2` = 127 AND `effectBasePoints2` = -26
           AND `effectDieSides2` = 1 AND `effectImplicitTargetA2` = 1 AND `effect3` = 0 AND `procFlags` = 0) = 1
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` IN (61231, 61232) AND `name` = 'Shadow Dance' AND `script_name` = '' AND `attributes` = 2147483648
           AND `durationIndex` = 27 AND `spellFamilyName` = 0 AND `spellIconId` = 1
           AND `effect1` = 6 AND `effectApplyAuraName1` = 110 AND `effectMiscValue1` = 3 AND `effectBasePoints1` = -11
           AND `effectDieSides1` = 1 AND `effectImplicitTargetA1` = 1 AND `effect2` = 0 AND `effect3` = 0) = 2;
DROP TEMPORARY TABLE `tmp_check_654_stance`;
