-- twow-repo#295 W3 player speed (main train 9), owner decisions 2026-10-02 (#295, CLI-295):
--   "Tempo-Boni von Spieler-Zaubern werden verdoppelt." Doubled (+100 % of the bonus): class
--   spells, talents and racials; further doubling only for swim speed (Aquatic Form, swim
--   potions, swim items). Equipment, enchants, consumables (run) and pet abilities stay.
--   "Alle Tempo-Aenderungen gelten nur fuer Spieler und Bots, nie fuer NPCs": only IDs that no
--   NPC uses are changed (shared IDs would stay unchanged; the only shared one is Blink 1953).
--   Blink: 15 s -> 7.5 s for players and bots only, through a hidden mage passive; NPC 1953 stays.
-- Value: amount = effectBasePoints + effectBaseDice (dieSides <= 1, dice 1 on every row here,
-- no per-level points); doubled: new bp = 2 * (bp + 1) - 1.
--   spell                         eff aura  old -> new  source
--   2645  Ghost Wolf               2  31    40 -> 80 %  shaman
--   2983/8696/11305 Sprint 1-3     1  31    50/60/70 -> 100/120/140 %  rogue
--   1850/9821 Dash 1-2             1  31    50/60 -> 100/120 %  druid (cat)
--   5419  Travel Form (Passive)    1  31    40 -> 80 %  druid, applied by Travel Form 783
--   5118  Aspect of the Cheetah    1  31    30 -> 60 %  hunter
--   13159 Aspect of the Pack       1  31    30 -> 60 %  hunter
--   57108 Emerald Blessing         1  31    10 -> 20 %  druid (item 61445)
--   17002/24866 Feral Swiftness    1  31    15/30 -> 30/60 %  druid talent (cat form)
--   26022/26023 Pursuit of Justice 1  31    4/8 -> 8/16 %  paladin talent, run
--                                  2  172   4/8 -> 8/16 %  mounted
--   19559/19560 Pathfinding        1  107   +3/+6 -> +6/+12 (SPELLMOD_SPEED on the aspects)
--                                  2  31    15/30 -> 30/60 %  pet (owner aura)
--   46097/46098/46099 Ghostly      1  31    5/10/15 -> 10/20/30 %  rogue talent (Improved
--     Swiftness                    2  31    2/4/6 -> 4/8/12 %   Ghostly Strike 51970-51972)
--   51670 Thirst for Blood         1  129   10 -> 20 %  warrior (Bloodthirst)
--   52737/52739 Sinister Pursuit   1  129   5/10 -> 10/20 %  warlock talent (demon)
--   20582 Quickness                2  129   1 -> 2 %  night elf racial; the text names $s2 now
--   46240 Exit Strategy            1  31    40 -> 80 %  goblin racial (46018)
--   swim speed (aura 58):
--   5421  Aquatic Form (Passive)   1  58    50 -> 100 %  druid, applied by Aquatic Form 1066
--   8747  Swimming Speed           1  58    15 -> 30 %  items 7052, 40061, 56023, 60470, 80720, 83494
--   48015 Swimming Speed           1  58    5 -> 10 %  items 42101, 58079, 65028
--   7840  Swim Speed               1  58    100 -> 200 %  Swim Speed Potion 6372
--   49370 Fluidity Potion          1  58    100 -> 200 %  item 41961
--   24347 Master Angler            3  58    25 -> 50 %  item 19979
--   24090 Minor Movement Speed     2  58    8 -> 16 %  item 60860 (run speed, effect 1, stays 8 %)
--   24926 Hallow's End Candy       2  58    50 -> 100 %  item 20557 (24930 picks it at random)
--   Unchanged: 48011-48014 and 48016 (Swimming Speed 2-6 %): no item, spell, quest or script
--   grants them.
-- NPC check per ID (01.10 dump + 814): creature_template spell_id1-4, spawn_spell_id and
-- auras, creature_addon auras, creature_spells 1-8, the creature_ai, creature_movement,
-- creature_spells, event, gameobject, generic, gossip, quest_start, quest_end and spell
-- scripts (cast, add/remove aura, cooldown), gameobject traps/goobers/rituals/spellcasters,
-- pet_spell_data, petcreateinfo_spell: no hit for any changed ID, nor for the forms 783/1066
-- that apply 5419/5421. git grep (src, modules): only player and bot code (Ghost Wolf, Sprint,
-- the form passives) and unrelated numbers (coordinates, text and item IDs). Table:
-- builds/cli295-riding/evidence/manifest/npc-usage-295.tsv. No text names a literal speed
-- ($s1, $5419s1 ...); only Quickness said "by $s1%" for all four of its effects.
-- Blink: 61310 "Blink Cooldown" is a hidden passive mage spell mod, a clone of 51979
-- Accelerated Arcana (Part 2): aura 108 (ADD_PCT_MODIFIER), SPELLMOD_COOLDOWN (11), -50 %
-- (bp -51, dice 1), mage family (3), mask effectItemType1 0x10000 = 65536 (Blink's family bit,
-- as in 23025; 61310 has no spell_affect row, so its mask is effectItemType1). A spell mod only
-- changes its owner's own spells, so NPC Blinks keep their cooldowns; Blink 1953 itself stays
-- (NPC users: creature_ai_scripts 711503 Jaedenar Adept, creature_template 50529/50531 car
-- controllers). spell_learn_spell 1953 -> 61310 (active): Player::AddSpell learns it with
-- Blink, also when the spells load at login, so existing mages get it without a character
-- migration. 61310 halves Blink to 7.5 s; with the item spell 23025 (flat -1.5 s) it is 6.0 s
-- (15 s - 50 % - 1.5 s, Player::ApplySpellMod). Accelerated Arcana (Part 2) 51979 has
-- spell_affect mask 0 and affects nothing.
-- Coupled release: client patch v7 mirrors the EffectBasePoints above, the Quickness text and
-- the new row 61310 (builds/cli295-riding/evidence/manifest/client-rows-295.tsv).
-- Replay-safe: the backup takes only rows with the old values; every update checks aura, old
-- basepoints and dice; inserts are guarded by NOT EXISTS; the tail asserts the end state.
-- W1, W2a, W2b and W3 are rolled back together (newest file first); after go-live also with
-- the character step in the 20261002210000 header (riding capped at 150).
-- Rollback (exact SQL; spell_template_bak_295 stays for a later cleanup):
--   UPDATE `spell_template` s JOIN `spell_template_bak_295` b ON b.`entry` = s.`entry`
--      SET s.`effectBasePoints1` = b.`effectBasePoints1`, s.`effectBasePoints2` = b.`effectBasePoints2`,
--          s.`effectBasePoints3` = b.`effectBasePoints3`, s.`description` = b.`description`
--    WHERE s.`entry` IN (1850, 2645, 2983, 5118, 5419, 5421, 7840, 8696, 8747, 9821, 11305, 13159, 17002,
--          19559, 19560, 20582, 24090, 24347, 24866, 24926, 26022, 26023, 46097, 46098, 46099, 46240,
--          48015, 49370, 51670, 52737, 52739, 57108);
--   DELETE FROM `spell_learn_spell` WHERE `entry` = 1953 AND `SpellID` = 61310;
--   DELETE FROM `spell_template` WHERE `entry` = 61310;

CREATE TABLE IF NOT EXISTS `spell_template_bak_295` LIKE `spell_template`;
INSERT IGNORE INTO `spell_template_bak_295`
SELECT * FROM `spell_template`
 WHERE (`entry`, `effectBasePoints1`) IN ((2983, 49), (8696, 59), (11305, 69), (1850, 49), (9821, 59),
         (5419, 39), (5118, 29), (13159, 29), (57108, 9), (17002, 14), (24866, 29), (26022, 3),
         (26023, 7), (19559, 2), (19560, 5), (46097, 4), (46098, 9), (46099, 14), (51670, 9),
         (52737, 4), (52739, 9), (46240, 39), (5421, 49), (8747, 14), (48015, 4), (7840, 99),
         (49370, 99))
    OR (`entry`, `effectBasePoints2`) IN ((2645, 39), (20582, 0), (24090, 7), (24926, 49))
    OR (`entry`, `effectBasePoints3`) IN ((24347, 24));

-- Class spells.
UPDATE `spell_template` SET `effectBasePoints2` = 79
 WHERE `entry` = 2645 AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 39, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 99
 WHERE `entry` = 2983 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 49, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 119
 WHERE `entry` = 8696 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 59, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 139
 WHERE `entry` = 11305 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 69, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 99
 WHERE `entry` = 1850 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 49, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 119
 WHERE `entry` = 9821 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 59, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 79
 WHERE `entry` = 5419 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 39, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 59
 WHERE `entry` = 5118 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 29, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 59
 WHERE `entry` = 13159 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 29, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 19
 WHERE `entry` = 57108 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 9, 1, 1);

-- Talents.
UPDATE `spell_template` SET `effectBasePoints1` = 29
 WHERE `entry` = 17002 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 14, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 59
 WHERE `entry` = 24866 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 29, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 7, `effectBasePoints2` = 7
 WHERE `entry` = 26022 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 3, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (172, 3, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 15, `effectBasePoints2` = 15
 WHERE `entry` = 26023 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 7, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (172, 7, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 5, `effectBasePoints2` = 29
 WHERE `entry` = 19559 AND (`effectApplyAuraName1`, `effectMiscValue1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (107, 12, 2, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 14, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 11, `effectBasePoints2` = 59
 WHERE `entry` = 19560 AND (`effectApplyAuraName1`, `effectMiscValue1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (107, 12, 5, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 29, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 9, `effectBasePoints2` = 3
 WHERE `entry` = 46097 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 4, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 1, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 19, `effectBasePoints2` = 7
 WHERE `entry` = 46098 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 9, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 3, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 29, `effectBasePoints2` = 11
 WHERE `entry` = 46099 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 14, 1, 1)
   AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (31, 5, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 19
 WHERE `entry` = 51670 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (129, 9, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 9
 WHERE `entry` = 52737 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (129, 4, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 19
 WHERE `entry` = 52739 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (129, 9, 1, 1);

-- Racials. Quickness names its movement part ($s2) on its own now.
UPDATE `spell_template` SET `effectBasePoints2` = 1
 WHERE `entry` = 20582 AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (129, 0, 1, 1);
UPDATE `spell_template`
   SET `description` = 'Increases your attack speed, casting speed and dodge chance by $s1% and your movement speed by $s2%.'
 WHERE `entry` = 20582 AND `description` = 'Increases your attack speed, casting speed, movement speed and dodge chance by $s1%.'
   AND `entry` IN (SELECT b.`entry` FROM `spell_template_bak_295` b
                    WHERE b.`entry` = 20582 AND b.`description` = 'Increases your attack speed, casting speed, movement speed and dodge chance by $s1%.');
UPDATE `spell_template` SET `effectBasePoints1` = 79
 WHERE `entry` = 46240 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (31, 39, 1, 1);

-- Swim speed.
UPDATE `spell_template` SET `effectBasePoints1` = 99
 WHERE `entry` = 5421 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (58, 49, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 29
 WHERE `entry` = 8747 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (58, 14, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 9
 WHERE `entry` = 48015 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (58, 4, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 199
 WHERE `entry` = 7840 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (58, 99, 1, 1);
UPDATE `spell_template` SET `effectBasePoints1` = 199
 WHERE `entry` = 49370 AND (`effectApplyAuraName1`, `effectBasePoints1`, `effectBaseDice1`, `effectDieSides1`) = (58, 99, 1, 1);
UPDATE `spell_template` SET `effectBasePoints3` = 49
 WHERE `entry` = 24347 AND (`effectApplyAuraName3`, `effectBasePoints3`, `effectBaseDice3`, `effectDieSides3`) = (58, 24, 1, 1);
UPDATE `spell_template` SET `effectBasePoints2` = 15
 WHERE `entry` = 24090 AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (58, 7, 1, 1);
UPDATE `spell_template` SET `effectBasePoints2` = 99
 WHERE `entry` = 24926 AND (`effectApplyAuraName2`, `effectBasePoints2`, `effectBaseDice2`, `effectDieSides2`) = (58, 49, 1, 1);

-- 61310 Blink Cooldown = 51979 with name, text, -50 %, Blink's mask, passive and hidden.
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
SELECT 61310, d.`school`, d.`category`, d.`castUI`, d.`dispel`, d.`mechanic`, d.`attributes` | 192,
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
  d.`effect1`, d.`effect2`, d.`effect3`, 1, d.`effectDieSides2`, d.`effectDieSides3`, 1,
  d.`effectBaseDice2`, d.`effectBaseDice3`, d.`effectDicePerLevel1`, d.`effectDicePerLevel2`,
  d.`effectDicePerLevel3`, d.`effectRealPointsPerLevel1`, d.`effectRealPointsPerLevel2`,
  d.`effectRealPointsPerLevel3`, -51, d.`effectBasePoints2`, d.`effectBasePoints3`,
  d.`effectBonusCoefficient1`, d.`effectBonusCoefficient2`, d.`effectBonusCoefficient3`,
  d.`effectMechanic1`, d.`effectMechanic2`, d.`effectMechanic3`, d.`effectImplicitTargetA1`,
  d.`effectImplicitTargetA2`, d.`effectImplicitTargetA3`, d.`effectImplicitTargetB1`,
  d.`effectImplicitTargetB2`, d.`effectImplicitTargetB3`, d.`effectRadiusIndex1`,
  d.`effectRadiusIndex2`, d.`effectRadiusIndex3`, 108, d.`effectApplyAuraName2`,
  d.`effectApplyAuraName3`, d.`effectAmplitude1`, d.`effectAmplitude2`, d.`effectAmplitude3`,
  d.`effectMultipleValue1`, d.`effectMultipleValue2`, d.`effectMultipleValue3`,
  d.`effectChainTarget1`, d.`effectChainTarget2`, d.`effectChainTarget3`, 65536,
  d.`effectItemType2`, d.`effectItemType3`, 11, d.`effectMiscValue2`, d.`effectMiscValue3`,
  d.`effectTriggerSpell1`, d.`effectTriggerSpell2`, d.`effectTriggerSpell3`,
  d.`effectPointsPerComboPoint1`, d.`effectPointsPerComboPoint2`, d.`effectPointsPerComboPoint3`,
  d.`spellVisual1`, d.`spellVisual2`, d.`spellIconId`, d.`activeIconId`, d.`spellPriority`,
  'Blink Cooldown', d.`nameFlags`, '', d.`nameSubtextFlags`,
  'Reduces the cooldown of your Blink spell by $s1%.', d.`descriptionFlags`, '',
  d.`auraDescriptionFlags`, d.`manaCostPercentage`, d.`startRecoveryCategory`,
  d.`startRecoveryTime`, d.`minTargetLevel`, d.`maxTargetLevel`, 3, 0, d.`maxAffectedTargets`,
  d.`dmgClass`, d.`preventionType`, d.`stanceBarOrder`, d.`dmgMultiplier1`, d.`dmgMultiplier2`,
  d.`dmgMultiplier3`, d.`minFactionId`, d.`minReputation`, d.`requiredAuraVision`, d.`customFlags`,
  d.`script_name`
  FROM `spell_template` d
 WHERE d.`entry` = 51979
   AND NOT EXISTS (SELECT 1 FROM `spell_template` x WHERE x.`entry` = 61310);

INSERT INTO `spell_learn_spell` (`entry`, `SpellID`, `Active`)
SELECT 1953, 61310, 1 FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `spell_learn_spell` x WHERE x.`entry` = 1953 AND x.`SpellID` = 61310);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_295_w3` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_295_w3` (`ok`)
SELECT (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `effectBasePoints1`) IN ((2983, 99), (8696, 119), (11305, 139), (1850, 99), (9821, 119),
                 (5419, 79), (5118, 59), (13159, 59), (57108, 19), (17002, 29), (24866, 59), (26022, 7),
                 (26023, 15), (19559, 5), (19560, 11), (46097, 9), (46098, 19), (46099, 29), (51670, 19),
                 (52737, 9), (52739, 19), (46240, 79), (5421, 99), (8747, 29), (48015, 9), (7840, 199),
                 (49370, 199))) = 27
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE (`entry`, `effectBasePoints2`) IN ((2645, 79), (26022, 7), (26023, 15), (19559, 29), (19560, 59),
                 (46097, 3), (46098, 7), (46099, 11), (20582, 1), (24090, 15), (24926, 99))) = 11
   AND (SELECT COUNT(*) FROM `spell_template` WHERE (`entry`, `effectBasePoints3`) IN ((24347, 49))) = 1
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` = 20582
           AND `description` = 'Increases your attack speed, casting speed and dodge chance by $s1% and your movement speed by $s2%.') = 1
   AND (SELECT COUNT(*) FROM `spell_template`
         WHERE `entry` = 61310 AND `name` = 'Blink Cooldown' AND (`attributes` & 192) = 192
           AND `effect1` = 6 AND `effectApplyAuraName1` = 108 AND `effectMiscValue1` = 11
           AND `effectBasePoints1` = -51 AND `effectBaseDice1` = 1 AND `effectDieSides1` = 1
           AND `effectItemType1` = 65536 AND `spellFamilyName` = 3) = 1
   AND (SELECT COUNT(*) FROM `spell_learn_spell` WHERE `entry` = 1953 AND `SpellID` = 61310 AND `Active` = 1) = 1
   AND (SELECT COUNT(*) FROM `spell_template_bak_295` WHERE `entry` IN (
  1850, 2645, 2983, 5118, 5419, 5421, 7840, 8696, 8747, 9821, 11305, 13159, 17002, 19559, 19560,
  20582, 24090, 24347, 24866, 24926, 26022, 26023, 46097, 46098, 46099, 46240, 48015, 49370, 51670,
  52737, 52739, 57108
         )) = 32;
