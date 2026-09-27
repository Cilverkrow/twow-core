-- Issue twow-repo#379 (WS30-RACE-CLASS-01): dwarf shaman (race 3 / class 7) and undead paladin
-- (race 5 / class 2) as new race/class combinations, bots first. Prepared for train 7; a rule
-- change that ships only after the owner approves it (#379, cloud brief OB-00 2026-09-27).
--
-- How every row below was derived (tw_world as sql/base plus the later playercreateinfo
-- migrations world/20260504203644, world/20260517101017, 20260606120000, 20260620130000):
--   template   dwarf shaman from orc shaman (2,7); undead paladin from human paladin (1,2).
--   race swap  the rogue rows (class 4) are the reference, because every race pair involved
--              has a rogue row with a constant difference over all 60 levels:
--                spells     template minus the spells only the template race's rogue has,
--                           plus the spells only the new race's rogue has (racials, language)
--                levelstats template(L) + rogue(new race, L) - rogue(template race, L)
--                           dwarf - orc = (-1,-1,+1,+2,-4), undead - human = (-1,-2,+1,-2,+5)
--   position   the race's existing start position (Coldridge Valley, Deathknell).
--   actions    class buttons 0-2 and 10 from the template; the race's active racials on 3/4
--              (as dwarf paladin (3,2) and undead rogue (5,4)); the race's food on 11.
--   items      the template's class outfit (all allowable_race = -1, no faction lock) with
--              the template race's food swapped for the new race's food (4540 / 4604).
--
-- Order matters and is fail closed: ObjectMgr::LoadPlayerInfo exits the server when a loaded
-- race/class has no level 1 stats, so player_levelstats goes first and playercreateinfo last,
-- and playercreateinfo is only inserted when all 60 level rows of its pair exist.
-- Replay-safe: INSERT IGNORE on primary keys; playercreateinfo_item has no key, so its rows
-- go in only while the pair has no item rows at all.
-- Not included (#379 brief): client patch, player trainer NPCs, class quest auto-grant (#356).

-- 1) Level stats, 1..60 per pair.
INSERT IGNORE INTO `player_levelstats` (`race`, `class`, `level`, `str`, `agi`, `sta`, `inte`, `spi`)
VALUES
(3, 7, 1, 23, 16, 24, 20, 21),
(3, 7, 2, 24, 16, 25, 21, 22),
(3, 7, 3, 24, 17, 26, 22, 23),
(3, 7, 4, 25, 17, 27, 22, 24),
(3, 7, 5, 26, 18, 27, 23, 25),
(3, 7, 6, 27, 18, 28, 24, 26),
(3, 7, 7, 28, 18, 29, 25, 27),
(3, 7, 8, 28, 19, 30, 26, 27),
(3, 7, 9, 29, 19, 31, 27, 28),
(3, 7, 10, 30, 20, 32, 27, 29),
(3, 7, 11, 31, 20, 33, 28, 30),
(3, 7, 12, 32, 21, 34, 29, 31),
(3, 7, 13, 32, 21, 35, 30, 32),
(3, 7, 14, 33, 22, 36, 31, 33),
(3, 7, 15, 34, 22, 37, 32, 35),
(3, 7, 16, 35, 23, 38, 33, 36),
(3, 7, 17, 36, 23, 39, 34, 37),
(3, 7, 18, 37, 24, 40, 35, 38),
(3, 7, 19, 38, 24, 41, 36, 39),
(3, 7, 20, 39, 25, 42, 37, 40),
(3, 7, 21, 40, 25, 43, 38, 41),
(3, 7, 22, 40, 26, 44, 39, 42),
(3, 7, 23, 41, 26, 45, 40, 43),
(3, 7, 24, 42, 27, 46, 41, 45),
(3, 7, 25, 43, 27, 48, 42, 46),
(3, 7, 26, 44, 28, 49, 43, 47),
(3, 7, 27, 45, 28, 50, 44, 48),
(3, 7, 28, 46, 29, 51, 45, 49),
(3, 7, 29, 47, 29, 52, 46, 51),
(3, 7, 30, 48, 30, 53, 47, 52),
(3, 7, 31, 50, 30, 55, 49, 53),
(3, 7, 32, 51, 31, 56, 50, 55),
(3, 7, 33, 52, 32, 57, 51, 56),
(3, 7, 34, 53, 32, 58, 52, 57),
(3, 7, 35, 54, 33, 60, 53, 59),
(3, 7, 36, 55, 34, 61, 55, 60),
(3, 7, 37, 56, 34, 62, 56, 61),
(3, 7, 38, 57, 35, 64, 57, 63),
(3, 7, 39, 58, 35, 65, 58, 64),
(3, 7, 40, 60, 36, 66, 60, 66),
(3, 7, 41, 61, 37, 68, 61, 67),
(3, 7, 42, 62, 37, 69, 62, 69),
(3, 7, 43, 63, 38, 71, 63, 70),
(3, 7, 44, 65, 39, 72, 65, 72),
(3, 7, 45, 66, 39, 74, 66, 73),
(3, 7, 46, 67, 40, 75, 68, 75),
(3, 7, 47, 68, 41, 77, 69, 76),
(3, 7, 48, 70, 42, 78, 70, 78),
(3, 7, 49, 71, 42, 80, 72, 80),
(3, 7, 50, 72, 43, 81, 73, 81),
(3, 7, 51, 74, 44, 83, 75, 83),
(3, 7, 52, 75, 45, 84, 76, 85),
(3, 7, 53, 77, 45, 86, 78, 86),
(3, 7, 54, 78, 46, 88, 79, 88),
(3, 7, 55, 79, 47, 89, 81, 90),
(3, 7, 56, 81, 48, 91, 82, 92),
(3, 7, 57, 82, 49, 93, 84, 93),
(3, 7, 58, 84, 49, 94, 86, 95),
(3, 7, 59, 85, 50, 96, 87, 97),
(3, 7, 60, 87, 51, 98, 89, 99),
(5, 2, 1, 21, 18, 23, 18, 26),
(5, 2, 2, 22, 19, 24, 19, 27),
(5, 2, 3, 23, 19, 25, 19, 27),
(5, 2, 4, 24, 20, 26, 20, 28),
(5, 2, 5, 25, 20, 27, 20, 29),
(5, 2, 6, 26, 21, 28, 21, 29),
(5, 2, 7, 27, 21, 29, 22, 30),
(5, 2, 8, 28, 22, 29, 22, 30),
(5, 2, 9, 29, 22, 30, 23, 31),
(5, 2, 10, 30, 23, 31, 23, 32),
(5, 2, 11, 31, 23, 32, 24, 33),
(5, 2, 12, 32, 24, 33, 25, 33),
(5, 2, 13, 33, 25, 34, 25, 34),
(5, 2, 14, 34, 25, 35, 26, 35),
(5, 2, 15, 35, 26, 37, 27, 35),
(5, 2, 16, 37, 26, 38, 27, 36),
(5, 2, 17, 38, 27, 39, 28, 37),
(5, 2, 18, 39, 28, 40, 29, 38),
(5, 2, 19, 40, 28, 41, 29, 38),
(5, 2, 20, 41, 29, 42, 30, 39),
(5, 2, 21, 42, 30, 43, 31, 41),
(5, 2, 22, 44, 30, 44, 32, 42),
(5, 2, 23, 45, 31, 45, 32, 43),
(5, 2, 24, 46, 32, 47, 33, 43),
(5, 2, 25, 47, 32, 48, 34, 44),
(5, 2, 26, 49, 33, 49, 35, 45),
(5, 2, 27, 50, 34, 50, 35, 47),
(5, 2, 28, 51, 34, 51, 36, 48),
(5, 2, 29, 53, 35, 53, 37, 49),
(5, 2, 30, 54, 36, 54, 38, 49),
(5, 2, 31, 55, 37, 55, 39, 50),
(5, 2, 32, 57, 37, 57, 40, 51),
(5, 2, 33, 58, 38, 58, 40, 52),
(5, 2, 34, 60, 39, 59, 41, 53),
(5, 2, 35, 61, 40, 61, 42, 54),
(5, 2, 36, 63, 41, 62, 43, 55),
(5, 2, 37, 64, 41, 63, 44, 56),
(5, 2, 38, 66, 42, 65, 45, 57),
(5, 2, 39, 67, 43, 66, 46, 58),
(5, 2, 40, 69, 44, 68, 47, 59),
(5, 2, 41, 70, 45, 69, 48, 60),
(5, 2, 42, 72, 45, 71, 49, 61),
(5, 2, 43, 73, 46, 72, 50, 62),
(5, 2, 44, 75, 47, 74, 50, 63),
(5, 2, 45, 77, 48, 75, 51, 64),
(5, 2, 46, 78, 49, 77, 52, 65),
(5, 2, 47, 80, 50, 78, 54, 66),
(5, 2, 48, 82, 51, 80, 55, 68),
(5, 2, 49, 83, 52, 82, 56, 67),
(5, 2, 50, 85, 53, 83, 57, 68),
(5, 2, 51, 87, 54, 85, 58, 69),
(5, 2, 52, 89, 55, 87, 59, 70),
(5, 2, 53, 91, 56, 88, 60, 71),
(5, 2, 54, 92, 57, 90, 61, 72),
(5, 2, 55, 94, 58, 92, 62, 74),
(5, 2, 56, 96, 59, 94, 63, 75),
(5, 2, 57, 98, 60, 95, 64, 76),
(5, 2, 58, 100, 61, 97, 66, 77),
(5, 2, 59, 102, 62, 99, 67, 79),
(5, 2, 60, 104, 63, 101, 68, 80);

-- 2) Start spells.
INSERT IGNORE INTO `playercreateinfo_spell` (`race`, `class`, `spell`, `note`)
VALUES
(3, 7, 81, 'Dodge'),
(3, 7, 107, 'Block'),
(3, 7, 197, 'Two-Handed Axes'),
(3, 7, 198, 'One-Handed Maces'),
(3, 7, 199, 'Two-Handed Maces'),
(3, 7, 203, 'Unarmed'),
(3, 7, 204, 'Defense'),
(3, 7, 227, 'Staves'),
(3, 7, 331, 'Healing Wave'),
(3, 7, 403, 'Lightning Bolt'),
(3, 7, 522, 'SPELLDEFENSE (DND)'),
(3, 7, 668, 'Language Common'),
(3, 7, 672, 'Language Dwarven'),
(3, 7, 2382, 'Generic'),
(3, 7, 2479, 'Honorless Target'),
(3, 7, 2481, 'Find Treasure'),
(3, 7, 3050, 'Detect'),
(3, 7, 3365, 'Opening'),
(3, 7, 6233, 'Closing'),
(3, 7, 6246, 'Closing'),
(3, 7, 6247, 'Opening'),
(3, 7, 6477, 'Opening'),
(3, 7, 6478, 'Opening'),
(3, 7, 6603, 'Attack'),
(3, 7, 7266, 'Duel'),
(3, 7, 7267, 'Grovel'),
(3, 7, 7355, 'Stuck'),
(3, 7, 8386, 'Attacking'),
(3, 7, 9077, 'Leather'),
(3, 7, 9078, 'Cloth'),
(3, 7, 9116, 'Shield'),
(3, 7, 9125, 'Generic'),
(3, 7, 20594, 'Stoneform'),
(3, 7, 20595, 'Gun Specialization'),
(3, 7, 20596, 'Frost Resistance'),
(3, 7, 21651, 'Opening'),
(3, 7, 21652, 'Closing'),
(3, 7, 22027, 'Remove Insignia'),
(3, 7, 22810, 'Opening - No Text'),
(3, 7, 27763, 'Totem'),
(5, 2, 81, 'Dodge'),
(5, 2, 107, 'Block'),
(5, 2, 198, 'One-Handed Maces'),
(5, 2, 199, 'Two-Handed Maces'),
(5, 2, 203, 'Unarmed'),
(5, 2, 204, 'Defense'),
(5, 2, 522, 'SPELLDEFENSE (DND)'),
(5, 2, 635, 'Holy Light'),
(5, 2, 669, 'Language Orcish'),
(5, 2, 2382, 'Generic'),
(5, 2, 2479, 'Honorless Target'),
(5, 2, 3050, 'Detect'),
(5, 2, 3365, 'Opening'),
(5, 2, 5227, 'Underwater Breathing'),
(5, 2, 6233, 'Closing'),
(5, 2, 6246, 'Closing'),
(5, 2, 6247, 'Opening'),
(5, 2, 6477, 'Opening'),
(5, 2, 6478, 'Opening'),
(5, 2, 6603, 'Attack'),
(5, 2, 7266, 'Duel'),
(5, 2, 7267, 'Grovel'),
(5, 2, 7355, 'Stuck'),
(5, 2, 7744, 'Will of the Forsaken'),
(5, 2, 8386, 'Attacking'),
(5, 2, 8737, 'Mail'),
(5, 2, 9077, 'Leather'),
(5, 2, 9078, 'Cloth'),
(5, 2, 9116, 'Shield'),
(5, 2, 9125, 'Generic'),
(5, 2, 17737, 'Language Gutterspeak'),
(5, 2, 20577, 'Cannibalize'),
(5, 2, 21084, 'Seal of Righteousness'),
(5, 2, 21651, 'Opening'),
(5, 2, 21652, 'Closing'),
(5, 2, 22027, 'Remove Insignia'),
(5, 2, 22810, 'Opening - No Text'),
(5, 2, 27762, 'Libram'),
(5, 2, 52522, 'Vengeance');

-- 3) Action bar.
INSERT IGNORE INTO `playercreateinfo_action` (`race`, `class`, `button`, `action`, `type`)
VALUES
(3, 7, 0, 6603, 0),
(3, 7, 1, 403, 0),
(3, 7, 2, 331, 0),
(3, 7, 3, 20594, 0),
(3, 7, 4, 2481, 0),
(3, 7, 10, 159, 128),
(3, 7, 11, 4540, 128),
(5, 2, 0, 6603, 0),
(5, 2, 1, 21084, 0),
(5, 2, 2, 635, 0),
(5, 2, 3, 20577, 0),
(5, 2, 10, 159, 128),
(5, 2, 11, 4604, 128);

-- 4) Start items (no primary key: insert a pair's items only while it has none).
INSERT INTO `playercreateinfo_item` (`race`, `class`, `itemid`, `amount`)
SELECT v.`race`, v.`class`, v.`itemid`, v.`amount` FROM (
    SELECT 3 AS `race`, 7 AS `class`, 36 AS `itemid`, 1 AS `amount`
    UNION ALL SELECT 3, 7, 153, 1
    UNION ALL SELECT 3, 7, 154, 1
    UNION ALL SELECT 3, 7, 159, 2
    UNION ALL SELECT 3, 7, 4540, 4
    UNION ALL SELECT 3, 7, 6948, 1
) v
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo_item` i WHERE i.`race` = 3 AND i.`class` = 7);

INSERT INTO `playercreateinfo_item` (`race`, `class`, `itemid`, `amount`)
SELECT v.`race`, v.`class`, v.`itemid`, v.`amount` FROM (
    SELECT 5 AS `race`, 2 AS `class`, 43 AS `itemid`, 1 AS `amount`
    UNION ALL SELECT 5, 2, 44, 1
    UNION ALL SELECT 5, 2, 45, 1
    UNION ALL SELECT 5, 2, 159, 2
    UNION ALL SELECT 5, 2, 2361, 1
    UNION ALL SELECT 5, 2, 4604, 4
    UNION ALL SELECT 5, 2, 6948, 1
) v
WHERE NOT EXISTS (SELECT 1 FROM `playercreateinfo_item` i WHERE i.`race` = 5 AND i.`class` = 2);

-- 5) The pairs themselves, last, and only with complete level stats.
INSERT IGNORE INTO `playercreateinfo` (`race`, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`)
SELECT 3, 7, 0, 1, -6240.32, 331.033, 382.758, 6.17716 FROM DUAL
WHERE (SELECT COUNT(*) FROM `player_levelstats` WHERE `race` = 3 AND `class` = 7 AND `level` BETWEEN 1 AND 60) = 60;

INSERT IGNORE INTO `playercreateinfo` (`race`, `class`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `orientation`)
SELECT 5, 2, 0, 85, 1676.35, 1677.45, 121.67, 2.70526 FROM DUAL
WHERE (SELECT COUNT(*) FROM `player_levelstats` WHERE `race` = 5 AND `class` = 2 AND `level` BETWEEN 1 AND 60) = 60;

-- Fail-closed tail: the auto-updater records a migration as applied whatever its statements
-- returned, so assert the end state here and make a partial apply a hard error.
DROP TEMPORARY TABLE IF EXISTS `_tw_379_assert`;
CREATE TEMPORARY TABLE `_tw_379_assert` (
  `ok` tinyint(1) NOT NULL CHECK (`ok` = 1)
) ENGINE=InnoDB;

INSERT INTO `_tw_379_assert` (`ok`)
SELECT IF(
  (SELECT COUNT(*) FROM `playercreateinfo` WHERE (`race`, `class`) IN ((3, 7), (5, 2))) = 2
  AND (SELECT COUNT(*) FROM `player_levelstats` WHERE (`race`, `class`) IN ((3, 7), (5, 2)) AND `level` BETWEEN 1 AND 60) = 120
  AND (SELECT COUNT(*) FROM `playercreateinfo_spell` WHERE `race` = 3 AND `class` = 7) >= 40
  AND (SELECT COUNT(*) FROM `playercreateinfo_spell` WHERE `race` = 5 AND `class` = 2) >= 39
  AND (SELECT COUNT(*) FROM `playercreateinfo_action` WHERE (`race`, `class`) IN ((3, 7), (5, 2))) >= 13
  AND (SELECT COUNT(*) FROM `playercreateinfo_item` WHERE (`race`, `class`) IN ((3, 7), (5, 2))) >= 13,
  1, 0
);

DROP TEMPORARY TABLE IF EXISTS `_tw_379_assert`;
