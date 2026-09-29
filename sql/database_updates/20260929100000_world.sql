-- Issue twow-repo#367, client patch stage 2 (twow-repo#409 issuecomment-5874183685, part B):
-- the server counterpart of the rogue talent delta in twow-repo
-- ops/clientpatch/changes/*/0367_*.csv. Replay-safe: guarded UPDATEs and INSERT IGNORE.
--
-- 1. Spells 90140-90193 (kit + talent line) get the tooltip text and icon the client
--    Spell.dbc rows carry, so spell_template stays the single source of truth for both.
--    No mechanic changes: no effect, aura, proc or value column is touched here.
-- 2. Player poison ranks I-IV of Agitating Poison (#386 D-1/D-7/D-8, rank V = 45611-45613,
--    item 65032, unchanged): proc 90200-90203, coating 90204-90207, item 90141-90144, each
--    coating on its own enchantment 90141-90144 (SpellItemEnchantment.dbc, server and client,
--    coupled release). No vendor or loot rows: players craft them (3.).
-- 3. Owner decisions P-1/P-2 (twow-repo#367, 2026-09-28): every rogue can learn at the rogue
--    trainer Spit (12) and Shadow Dance I/II/III (20/40/60), and the poison recipes I-IV
--    (Poisons, 20/30/40/50). Recipes use the reagents of Agitating Poison 45611: Maiden's
--    Anguish 2931 (1 for ranks I-II, 2 for III-IV) + Leaded Vial 3372, both already sold by
--    the poison vendors. Recipes 90208-90211, trainer spells 90212-90219, skill_line_ability
--    rows with the spell's ID, trainer rows for every trainer that teaches Agitating Poison
--    (47312). The client needs these spells too: this file belongs to the coupled release
--    (twow-repo docs/design/rogue-tank.md 7.3) and must not be pinned before it.

UPDATE `spell_template` SET `name` = 'Spit', `nameSubtext` = '', `description` = 'Spit at the enemy, forcing it and up to two enemies within 8 yards of it to attack you for $d.', `auraDescription` = '', `spellIconId` = 24 WHERE `entry` = 90140;
UPDATE `spell_template` SET `name` = 'Spit', `nameSubtext` = '', `description` = 'Spit at the enemy, forcing it and up to two enemies within 8 yards of it to attack you for $d.', `auraDescription` = '', `spellIconId` = 24 WHERE `entry` = 90141;
UPDATE `spell_template` SET `name` = 'Shadow Dance', `nameSubtext` = 'Rank 1', `description` = 'Parrying an attack increases your dodge chance by $90145s1% and dodging an attack increases your parry chance by $90146s1% for $90145d. Each dodge and parry causes $s1 threat.', `auraDescription` = '', `spellIconId` = 252 WHERE `entry` = 90142;
UPDATE `spell_template` SET `name` = 'Shadow Dance', `nameSubtext` = 'Rank 2', `description` = 'Parrying an attack increases your dodge chance by $90145s1% and dodging an attack increases your parry chance by $90146s1% for $90145d. Each dodge and parry causes $s1 threat.', `auraDescription` = '', `spellIconId` = 252 WHERE `entry` = 90143;
UPDATE `spell_template` SET `name` = 'Shadow Dance', `nameSubtext` = 'Rank 3', `description` = 'Parrying an attack increases your dodge chance by $90145s1% and dodging an attack increases your parry chance by $90146s1% for $90145d. Each dodge and parry causes $s1 threat.', `auraDescription` = '', `spellIconId` = 252 WHERE `entry` = 90144;
UPDATE `spell_template` SET `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Dodge chance increased by $s1%.', `auraDescription` = 'Dodge chance increased by $s1%.', `spellIconId` = 178 WHERE `entry` = 90145;
UPDATE `spell_template` SET `name` = 'Shadow Dance', `nameSubtext` = '', `description` = 'Parry chance increased by $s1%.', `auraDescription` = 'Parry chance increased by $s1%.', `spellIconId` = 558 WHERE `entry` = 90146;
UPDATE `spell_template` SET `name` = 'Blindside', `nameSubtext` = 'Rank 1', `description` = 'Your attacks deal $s1% more damage when you are behind the target.', `auraDescription` = '', `spellIconId` = 243 WHERE `entry` = 90150;
UPDATE `spell_template` SET `name` = 'Blindside', `nameSubtext` = 'Rank 2', `description` = 'Your attacks deal $s1% more damage when you are behind the target.', `auraDescription` = '', `spellIconId` = 243 WHERE `entry` = 90151;
UPDATE `spell_template` SET `name` = 'Blindside', `nameSubtext` = 'Rank 3', `description` = 'Your attacks deal $s1% more damage when you are behind the target.', `auraDescription` = '', `spellIconId` = 243 WHERE `entry` = 90152;
UPDATE `spell_template` SET `name` = 'Blindside', `nameSubtext` = 'Rank 4', `description` = 'Your attacks deal $s1% more damage when you are behind the target.', `auraDescription` = '', `spellIconId` = 243 WHERE `entry` = 90153;
UPDATE `spell_template` SET `name` = 'Flowing Blades', `nameSubtext` = 'Rank 1', `description` = 'While Slice and Dice is active, your melee critical strikes have a $h% chance to reduce the remaining cooldown of Flourish, Cold Blood, Adrenaline Rush, Preparation and Mark for Death by 1 sec.', `auraDescription` = '', `spellIconId` = 515 WHERE `entry` = 90154;
UPDATE `spell_template` SET `name` = 'Flowing Blades', `nameSubtext` = 'Rank 2', `description` = 'While Slice and Dice is active, your melee critical strikes have a $h% chance to reduce the remaining cooldown of Flourish, Cold Blood, Adrenaline Rush, Preparation and Mark for Death by 1 sec.', `auraDescription` = '', `spellIconId` = 515 WHERE `entry` = 90155;
UPDATE `spell_template` SET `name` = 'Frozen Blood', `nameSubtext` = '', `description` = 'Cold Blood also increases the damage of your next attack by $s1%.', `auraDescription` = '', `spellIconId` = 32 WHERE `entry` = 90156;
UPDATE `spell_template` SET `name` = 'Vigorous Fury', `nameSubtext` = '', `description` = 'Each time Vigor restores energy, your damage is increased by $90191s1% for $90191d. Stacks up to 10 times.', `auraDescription` = '', `spellIconId` = 691 WHERE `entry` = 90157;
UPDATE `spell_template` SET `name` = 'Fated Echo', `nameSubtext` = '', `description` = 'When Seal Fate grants you a combo point, you have a $s1% chance to gain another one.', `auraDescription` = '', `spellIconId` = 55 WHERE `entry` = 90158;
UPDATE `spell_template` SET `name` = 'Nimble Body', `nameSubtext` = 'Rank 1', `description` = 'Increases your Agility by $s1%.', `auraDescription` = '', `spellIconId` = 30 WHERE `entry` = 90159;
UPDATE `spell_template` SET `name` = 'Nimble Body', `nameSubtext` = 'Rank 2', `description` = 'Increases your Agility by $s1%.', `auraDescription` = '', `spellIconId` = 30 WHERE `entry` = 90160;
UPDATE `spell_template` SET `name` = 'Nimble Body', `nameSubtext` = 'Rank 3', `description` = 'Increases your Agility by $s1%.', `auraDescription` = '', `spellIconId` = 30 WHERE `entry` = 90161;
UPDATE `spell_template` SET `name` = 'Nimble Body', `nameSubtext` = 'Rank 4', `description` = 'Increases your Agility by $s1%.', `auraDescription` = '', `spellIconId` = 30 WHERE `entry` = 90162;
UPDATE `spell_template` SET `name` = 'Nimble Body', `nameSubtext` = 'Rank 5', `description` = 'Increases your Agility by $s1%.', `auraDescription` = '', `spellIconId` = 30 WHERE `entry` = 90163;
UPDATE `spell_template` SET `name` = 'Guarded Stance', `nameSubtext` = 'Rank 1', `description` = 'Increases your Defense skill by $s1.', `auraDescription` = '', `spellIconId` = 52 WHERE `entry` = 90164;
UPDATE `spell_template` SET `name` = 'Guarded Stance', `nameSubtext` = 'Rank 2', `description` = 'Increases your Defense skill by $s1.', `auraDescription` = '', `spellIconId` = 52 WHERE `entry` = 90165;
UPDATE `spell_template` SET `name` = 'Guarded Stance', `nameSubtext` = 'Rank 3', `description` = 'Increases your Defense skill by $s1.', `auraDescription` = '', `spellIconId` = 52 WHERE `entry` = 90166;
UPDATE `spell_template` SET `name` = 'Guarded Stance', `nameSubtext` = 'Rank 4', `description` = 'Increases your Defense skill by $s1.', `auraDescription` = '', `spellIconId` = 52 WHERE `entry` = 90167;
UPDATE `spell_template` SET `name` = 'Guarded Stance', `nameSubtext` = 'Rank 5', `description` = 'Increases your Defense skill by $s1.', `auraDescription` = '', `spellIconId` = 52 WHERE `entry` = 90168;
UPDATE `spell_template` SET `name` = 'Riposte Flow', `nameSubtext` = 'Rank 1', `description` = 'Parrying an attack gives you a $h% chance to strike with your off-hand weapon, and dodging an attack gives you a $h% chance to strike with your main-hand weapon. These strikes cause double threat and cannot occur more than once per second.', `auraDescription` = '', `spellIconId` = 278 WHERE `entry` = 90169;
UPDATE `spell_template` SET `name` = 'Riposte Flow', `nameSubtext` = 'Rank 2', `description` = 'Parrying an attack gives you a $h% chance to strike with your off-hand weapon, and dodging an attack gives you a $h% chance to strike with your main-hand weapon. These strikes cause double threat and cannot occur more than once per second.', `auraDescription` = '', `spellIconId` = 278 WHERE `entry` = 90170;
UPDATE `spell_template` SET `name` = 'Riposte Flow', `nameSubtext` = 'Rank 3', `description` = 'Parrying an attack gives you a $h% chance to strike with your off-hand weapon, and dodging an attack gives you a $h% chance to strike with your main-hand weapon. These strikes cause double threat and cannot occur more than once per second.', `auraDescription` = '', `spellIconId` = 278 WHERE `entry` = 90171;
UPDATE `spell_template` SET `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 1', `description` = 'Converts $s1% of your total dodge and parry chance into resistance to all schools of magic, $s2 $lpoint:points; per percent.', `auraDescription` = '', `spellIconId` = 178 WHERE `entry` = 90172;
UPDATE `spell_template` SET `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 2', `description` = 'Converts $s1% of your total dodge and parry chance into resistance to all schools of magic, $s2 $lpoint:points; per percent.', `auraDescription` = '', `spellIconId` = 178 WHERE `entry` = 90173;
UPDATE `spell_template` SET `name` = 'Arcane Evasion', `nameSubtext` = 'Rank 3', `description` = 'Converts $s1% of your total dodge and parry chance into resistance to all schools of magic, $s2 $lpoint:points; per percent.', `auraDescription` = '', `spellIconId` = 178 WHERE `entry` = 90174;
UPDATE `spell_template` SET `name` = 'Coup de Grace', `nameSubtext` = 'Rank 1', `description` = 'Your Backstab and Sinister Strike deal $s1% more damage against targets below 35% health.', `auraDescription` = '', `spellIconId` = 514 WHERE `entry` = 90175;
UPDATE `spell_template` SET `name` = 'Coup de Grace', `nameSubtext` = 'Rank 2', `description` = 'Your Backstab and Sinister Strike deal $s1% more damage against targets below 35% health.', `auraDescription` = '', `spellIconId` = 514 WHERE `entry` = 90176;
UPDATE `spell_template` SET `name` = 'Coup de Grace', `nameSubtext` = 'Rank 3', `description` = 'Your Backstab and Sinister Strike deal $s1% more damage against targets below 35% health.', `auraDescription` = '', `spellIconId` = 514 WHERE `entry` = 90177;
UPDATE `spell_template` SET `name` = 'Brazen Strike', `nameSubtext` = '', `description` = 'Backstab can be used from the front against targets below $s1% health.', `auraDescription` = '', `spellIconId` = 856 WHERE `entry` = 90178;
UPDATE `spell_template` SET `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 1', `description` = 'Reduces all damage taken by $s1% and increases your armor by $s2%.', `auraDescription` = '', `spellIconId` = 563 WHERE `entry` = 90179;
UPDATE `spell_template` SET `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 2', `description` = 'Reduces all damage taken by $s1% and increases your armor by $s2%.', `auraDescription` = '', `spellIconId` = 563 WHERE `entry` = 90180;
UPDATE `spell_template` SET `name` = 'Hardened Shadows', `nameSubtext` = 'Rank 3', `description` = 'Reduces all damage taken by $s1% and increases your armor by $s2%.', `auraDescription` = '', `spellIconId` = 563 WHERE `entry` = 90181;
UPDATE `spell_template` SET `name` = 'Ghostly Evasion', `nameSubtext` = '', `description` = 'While Ghostly Strike is active, you can also dodge harmful spells.', `auraDescription` = '', `spellIconId` = 596 WHERE `entry` = 90182;
UPDATE `spell_template` SET `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 1', `description` = 'Your attacks deal $s1% more damage while you are stealthed. Openers that break stealth count.', `auraDescription` = '', `spellIconId` = 250 WHERE `entry` = 90183;
UPDATE `spell_template` SET `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 2', `description` = 'Your attacks deal $s1% more damage while you are stealthed. Openers that break stealth count.', `auraDescription` = '', `spellIconId` = 250 WHERE `entry` = 90184;
UPDATE `spell_template` SET `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 3', `description` = 'Your attacks deal $s1% more damage while you are stealthed. Openers that break stealth count.', `auraDescription` = '', `spellIconId` = 250 WHERE `entry` = 90185;
UPDATE `spell_template` SET `name` = 'Shadow Strikes', `nameSubtext` = 'Rank 4', `description` = 'Your attacks deal $s1% more damage while you are stealthed. Openers that break stealth count.', `auraDescription` = '', `spellIconId` = 250 WHERE `entry` = 90186;
UPDATE `spell_template` SET `name` = 'Deep Wounds', `nameSubtext` = '', `description` = 'The bonus damage of your Hemorrhage stacks up to 5 times and lasts 15 sec.', `auraDescription` = '', `spellIconId` = 153 WHERE `entry` = 90187;
UPDATE `spell_template` SET `name` = 'Shadow Edge', `nameSubtext` = 'Rank 1', `description` = 'Your melee attacks deal an extra $s1% of their damage as Shadow damage.', `auraDescription` = '', `spellIconId` = 98 WHERE `entry` = 90188;
UPDATE `spell_template` SET `name` = 'Shadow Edge', `nameSubtext` = 'Rank 2', `description` = 'Your melee attacks deal an extra $s1% of their damage as Shadow damage.', `auraDescription` = '', `spellIconId` = 98 WHERE `entry` = 90189;
UPDATE `spell_template` SET `name` = 'Shadow Edge', `nameSubtext` = 'Rank 3', `description` = 'Your melee attacks deal an extra $s1% of their damage as Shadow damage.', `auraDescription` = '', `spellIconId` = 98 WHERE `entry` = 90190;
UPDATE `spell_template` SET `name` = 'Vigorous Fury', `nameSubtext` = '', `description` = 'Damage increased by $s1% per stack.', `auraDescription` = 'Damage increased by $s1% per stack.', `spellIconId` = 691 WHERE `entry` = 90191;
UPDATE `spell_template` SET `name` = 'Shadow Edge', `nameSubtext` = '', `description` = 'Shadow damage.', `auraDescription` = '', `spellIconId` = 98 WHERE `entry` = 90192;
UPDATE `spell_template` SET `name` = 'Hemorrhage', `nameSubtext` = '', `description` = 'Physical damage taken increased by $s1% per stack.', `auraDescription` = 'Physical damage taken increased by $s1% per stack.', `spellIconId` = 153 WHERE `entry` = 90193;

-- Agitating Poison rank 1 (level 20): 25-32 Nature damage, +150 threat
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45613;
UPDATE `tmp_spell` SET `entry` = 90200, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 1', `spellLevel` = 20, `baseLevel` = 20, `effectBasePoints1` = 24, `effectDieSides1` = 8, `effectBasePoints2` = 149, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45612;
UPDATE `tmp_spell` SET `entry` = 90204, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 1', `description` = 'Coats a weapon with poison that lasts for 30 minutes.\nEach strike has a $h% chance of poisoning the enemy which instantly inflicts $90200s1 Nature damage and causes $90200s2 additional threat.  115 charges.', `spellLevel` = 20, `baseLevel` = 20, `effectMiscValue1` = 90141, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_item` LIKE `item_template`;
INSERT IGNORE INTO `tmp_item` SELECT * FROM `item_template` WHERE `entry` = 65032;
UPDATE `tmp_item` SET `entry` = 90141, `name` = 'Agitating Poison I', `spellid_1` = 90204, `item_level` = 20, `required_level` = 20, `buy_price` = 300, `sell_price` = 25;
INSERT IGNORE INTO `item_template` SELECT * FROM `tmp_item`;
DROP TEMPORARY TABLE `tmp_item`;

-- Agitating Poison rank 2 (level 30): 36-45 Nature damage, +210 threat
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45613;
UPDATE `tmp_spell` SET `entry` = 90201, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 2', `spellLevel` = 30, `baseLevel` = 30, `effectBasePoints1` = 35, `effectDieSides1` = 10, `effectBasePoints2` = 209, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45612;
UPDATE `tmp_spell` SET `entry` = 90205, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 2', `description` = 'Coats a weapon with poison that lasts for 30 minutes.\nEach strike has a $h% chance of poisoning the enemy which instantly inflicts $90201s1 Nature damage and causes $90201s2 additional threat.  115 charges.', `spellLevel` = 30, `baseLevel` = 30, `effectMiscValue1` = 90142, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_item` LIKE `item_template`;
INSERT IGNORE INTO `tmp_item` SELECT * FROM `item_template` WHERE `entry` = 65032;
UPDATE `tmp_item` SET `entry` = 90142, `name` = 'Agitating Poison II', `spellid_1` = 90205, `item_level` = 30, `required_level` = 30, `buy_price` = 500, `sell_price` = 40;
INSERT IGNORE INTO `item_template` SELECT * FROM `tmp_item`;
DROP TEMPORARY TABLE `tmp_item`;

-- Agitating Poison rank 3 (level 40): 47-59 Nature damage, +275 threat
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45613;
UPDATE `tmp_spell` SET `entry` = 90202, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 3', `spellLevel` = 40, `baseLevel` = 40, `effectBasePoints1` = 46, `effectDieSides1` = 13, `effectBasePoints2` = 274, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45612;
UPDATE `tmp_spell` SET `entry` = 90206, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 3', `description` = 'Coats a weapon with poison that lasts for 30 minutes.\nEach strike has a $h% chance of poisoning the enemy which instantly inflicts $90202s1 Nature damage and causes $90202s2 additional threat.  115 charges.', `spellLevel` = 40, `baseLevel` = 40, `effectMiscValue1` = 90143, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_item` LIKE `item_template`;
INSERT IGNORE INTO `tmp_item` SELECT * FROM `item_template` WHERE `entry` = 65032;
UPDATE `tmp_item` SET `entry` = 90143, `name` = 'Agitating Poison III', `spellid_1` = 90206, `item_level` = 40, `required_level` = 40, `buy_price` = 700, `sell_price` = 60;
INSERT IGNORE INTO `item_template` SELECT * FROM `tmp_item`;
DROP TEMPORARY TABLE `tmp_item`;

-- Agitating Poison rank 4 (level 50): 57-72 Nature damage, +335 threat
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45613;
UPDATE `tmp_spell` SET `entry` = 90203, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 4', `spellLevel` = 50, `baseLevel` = 50, `effectBasePoints1` = 56, `effectDieSides1` = 16, `effectBasePoints2` = 334, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45612;
UPDATE `tmp_spell` SET `entry` = 90207, `script_name` = '', `name` = 'Agitating Poison', `nameSubtext` = 'Rank 4', `description` = 'Coats a weapon with poison that lasts for 30 minutes.\nEach strike has a $h% chance of poisoning the enemy which instantly inflicts $90203s1 Nature damage and causes $90203s2 additional threat.  115 charges.', `spellLevel` = 50, `baseLevel` = 50, `effectMiscValue1` = 90144, `spellIconId` = 110;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_item` LIKE `item_template`;
INSERT IGNORE INTO `tmp_item` SELECT * FROM `item_template` WHERE `entry` = 65032;
UPDATE `tmp_item` SET `entry` = 90144, `name` = 'Agitating Poison IV', `spellid_1` = 90207, `item_level` = 50, `required_level` = 50, `buy_price` = 900, `sell_price` = 75;
INSERT IGNORE INTO `item_template` SELECT * FROM `tmp_item`;
DROP TEMPORARY TABLE `tmp_item`;

-- Agitating Poison rank 1: recipe 90208, trainer spell 90212 (Poisons 1, level 20)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45611;
UPDATE `tmp_spell` SET `entry` = 90208, `nameSubtext` = 'Rank 1', `spellLevel` = 20, `effectItemType1` = 90141, `reagent1` = 2931, `reagentCount1` = 1, `reagent2` = 3372, `reagentCount2` = 1, `description` = 'Creates Agitating Poison I.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90212, `nameSubtext` = 'Rank 1', `effectTriggerSpell1` = 90208;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90208, 40, 90208, 0, 8, 1, 0, 0, 175, 125, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90212, 2700, 40, 1, 20 FROM `npc_trainer` WHERE `spell` = 47312;

-- Agitating Poison rank 2: recipe 90209, trainer spell 90213 (Poisons 130, level 30)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45611;
UPDATE `tmp_spell` SET `entry` = 90209, `nameSubtext` = 'Rank 2', `spellLevel` = 30, `effectItemType1` = 90142, `reagent1` = 2931, `reagentCount1` = 1, `reagent2` = 3372, `reagentCount2` = 1, `description` = 'Creates Agitating Poison II.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90213, `nameSubtext` = 'Rank 2', `effectTriggerSpell1` = 90209;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90209, 40, 90209, 0, 8, 1, 0, 0, 225, 175, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90213, 9000, 40, 130, 30 FROM `npc_trainer` WHERE `spell` = 47312;

-- Agitating Poison rank 3: recipe 90210, trainer spell 90214 (Poisons 180, level 40)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45611;
UPDATE `tmp_spell` SET `entry` = 90210, `nameSubtext` = 'Rank 3', `spellLevel` = 40, `effectItemType1` = 90143, `reagent1` = 2931, `reagentCount1` = 2, `reagent2` = 3372, `reagentCount2` = 1, `description` = 'Creates Agitating Poison III.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90214, `nameSubtext` = 'Rank 3', `effectTriggerSpell1` = 90210;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90210, 40, 90210, 0, 8, 1, 0, 0, 275, 225, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90214, 18000, 40, 180, 40 FROM `npc_trainer` WHERE `spell` = 47312;

-- Agitating Poison rank 4: recipe 90211, trainer spell 90215 (Poisons 230, level 50)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 45611;
UPDATE `tmp_spell` SET `entry` = 90211, `nameSubtext` = 'Rank 4', `spellLevel` = 50, `effectItemType1` = 90144, `reagent1` = 2931, `reagentCount1` = 2, `reagent2` = 3372, `reagentCount2` = 1, `description` = 'Creates Agitating Poison IV.';
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90215, `nameSubtext` = 'Rank 4', `effectTriggerSpell1` = 90211;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90211, 40, 90211, 0, 8, 1, 0, 0, 325, 275, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90215, 31500, 40, 230, 50 FROM `npc_trainer` WHERE `spell` = 47312;

-- Spit at the rogue trainer: trainer spell 90216 (level 12)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90216, `name` = 'Spit', `nameSubtext` = '', `description` = '', `effectTriggerSpell1` = 90140, `spellIconId` = 24;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90140, 38, 90140, 0, 8, 1, 0, 0, 0, 0, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90216, 720, 0, 0, 12 FROM `npc_trainer` WHERE `spell` = 47312;

-- Shadow Dance Rank 1 at the rogue trainer: trainer spell 90217 (level 20)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90217, `name` = 'Shadow Dance', `nameSubtext` = 'Rank 1', `description` = '', `effectTriggerSpell1` = 90142, `spellIconId` = 252;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90142, 38, 90142, 0, 8, 1, 90143, 0, 0, 0, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90217, 2700, 0, 0, 20 FROM `npc_trainer` WHERE `spell` = 47312;

-- Shadow Dance Rank 2 at the rogue trainer: trainer spell 90218 (level 40)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90218, `name` = 'Shadow Dance', `nameSubtext` = 'Rank 2', `description` = '', `effectTriggerSpell1` = 90143, `spellIconId` = 252;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90143, 38, 90143, 0, 8, 1, 90144, 0, 0, 0, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90218, 18000, 0, 0, 40 FROM `npc_trainer` WHERE `spell` = 47312;

-- Shadow Dance Rank 3 at the rogue trainer: trainer spell 90219 (level 60)
CREATE TEMPORARY TABLE `tmp_spell` LIKE `spell_template`;
INSERT IGNORE INTO `tmp_spell` SELECT * FROM `spell_template` WHERE `entry` = 47312;
UPDATE `tmp_spell` SET `entry` = 90219, `name` = 'Shadow Dance', `nameSubtext` = 'Rank 3', `description` = '', `effectTriggerSpell1` = 90144, `spellIconId` = 252;
INSERT IGNORE INTO `spell_template` SELECT * FROM `tmp_spell`;
DROP TEMPORARY TABLE `tmp_spell`;
INSERT IGNORE INTO `skill_line_ability` (`id`, `skill_id`, `spell_id`, `race_mask`, `class_mask`, `req_skill_value`, `superseded_by_spell`, `learn_on_get_skill`, `max_value`, `min_value`, `req_train_points`) VALUES (90144, 38, 90144, 0, 8, 1, 0, 0, 0, 0, 0);
INSERT IGNORE INTO `npc_trainer` (`entry`, `spell`, `spellcost`, `reqskill`, `reqskillvalue`, `reqlevel`) SELECT `entry`, 90219, 48600, 0, 0, 60 FROM `npc_trainer` WHERE `spell` = 47312;
