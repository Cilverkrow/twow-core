-- twow-repo#338 (train 8), owner decision 2026-09-30 (relayed by OB-00, #338 issuecomment-5914653880 follow-up):
-- "Deswegen gebe ich die Empfehlung, auf jeden Fall frei" - clean up #330 registry class B.
--   1. 48 entries leave creature_loot_bonus_registry: the 47 class-B trash elites and the friendly
--      NPC Sorcerer Ashcrombe. They drop their original loot tables again (e.g. Flamewaker keeps
--      the MC T1 bracers/belts at 0.15-0.56 %); no loot row is touched.
--   2. The other 89 class-B entries are real (mini-)bosses and become class A (note only).
-- Evidence and full list: evidence ws-30/338-classb (classb-entries.tsv, classb-items.tsv).
-- Reversible and replay-safe: removed rows are kept once in creature_loot_bonus_registry_bak_338.
-- Rollback:
--   INSERT IGNORE INTO creature_loot_bonus_registry (creature_entry, map_id, category, note)
--     SELECT creature_entry, map_id, category, note FROM creature_loot_bonus_registry_bak_338;
--   UPDATE creature_loot_bonus_registry SET note = REPLACE(note, '#338 owner 2026-09-30 class A (was #330 class B)', '#330 owner-approved class B')
--    WHERE note LIKE '#338 owner 2026-09-30 class A (was #330 class B)%';
-- Removed (entry/map name):
--   3850/33 Sorcerer Ashcrombe
--   3866/33 Vile Bat
--   3868/33 Blood Seeker
--   4427/47 Ward Guardian
--   4435/47 Razorfen Warrior
--   4438/47 Razorfen Spearhide
--   4515/47 Death's Head Acolyte
--   4531/47 Razorfen Beast Trainer
--   4848/70 Shadowforge Darkcaster
--   4849/70 Shadowforge Archaeologist
--   7290/70 Shadowforge Sharpshooter
--   5269/109 Atal'ai Priest
--   5273/109 Atal'ai High Priest
--   5708/109 Spawn of Hakkar
--   7345/129 Splinterbone Captain
--   4297/189 Scarlet Conjuror
--   8120/209 Sul'lithuz Abomination
--   10762/229 Blackhand Thug
--   10814/229 Chromatic Elite Guard
--   65105/269 Infinite Rift-Lord
--   10486/289 Risen Warrior
--   11387/309 Sandfury Speaker
--   11388/309 Witherbark Speaker
--   11389/309 Bloodscalp Speaker
--   11391/309 Vilebranch Speaker
--   10409/329 Rockwing Screecher
--   10422/329 Crimson Sorcerer
--   11661/409 Flamewaker
--   12467/469 Death Talon Captain
--   15978/533 Crypt Reaver
--   15979/533 Tomb Horror
--   16216/533 Unholy Swords
--   91913/800 Forlorn Shrieker
--   91922/800 Crypt Fearfeaster
--   60744/807 Sanctum Wyrm
--   61932/814 Vampiric Gloomwing
--   61940/814 Manascale Drake
--   61949/814 Disrupted Arcane Elemental
--   61950/814 Arcane Anomaly
--   61954/814 Lingering Magus
--   61955/814 Lingering Arcanist
--   61956/814 Lingering Astrologist
--   61957/814 Lingering Enchanter
--   62020/814 Outcast Souleater
--   62022/814 Draenei Worshipper
--   62025/814 Draenei Waterseeker
--   62076/816 Forgotten Ancestor
--   62771/820 Blackwind Bloodguard

CREATE TABLE IF NOT EXISTS `creature_loot_bonus_registry_bak_338` LIKE `creature_loot_bonus_registry`;

INSERT IGNORE INTO `creature_loot_bonus_registry_bak_338`
SELECT * FROM `creature_loot_bonus_registry`
WHERE (`creature_entry`, `map_id`) IN ((3850,33),(3866,33),(3868,33),(4427,47),(4435,47),(4438,47),(4515,47),(4531,47),(4848,70),(4849,70),(7290,70),(5269,109),(5273,109),(5708,109),(7345,129),(4297,189),(8120,209),(10762,229),(10814,229),(65105,269),(10486,289),(11387,309),(11388,309),(11389,309),(11391,309),(10409,329),(10422,329),(11661,409),(12467,469),(15978,533),(15979,533),(16216,533),(91913,800),(91922,800),(60744,807),(61932,814),(61940,814),(61949,814),(61950,814),(61954,814),(61955,814),(61956,814),(61957,814),(62020,814),(62022,814),(62025,814),(62076,816),(62771,820));

DELETE FROM `creature_loot_bonus_registry`
WHERE (`creature_entry`, `map_id`) IN ((3850,33),(3866,33),(3868,33),(4427,47),(4435,47),(4438,47),(4515,47),(4531,47),(4848,70),(4849,70),(7290,70),(5269,109),(5273,109),(5708,109),(7345,129),(4297,189),(8120,209),(10762,229),(10814,229),(65105,269),(10486,289),(11387,309),(11388,309),(11389,309),(11391,309),(10409,329),(10422,329),(11661,409),(12467,469),(15978,533),(15979,533),(16216,533),(91913,800),(91922,800),(60744,807),(61932,814),(61940,814),(61949,814),(61950,814),(61954,814),(61955,814),(61956,814),(61957,814),(62020,814),(62022,814),(62025,814),(62076,816),(62771,820));

UPDATE `creature_loot_bonus_registry`
   SET `note` = REPLACE(`note`, '#330 owner-approved class B', '#338 owner 2026-09-30 class A (was #330 class B)')
 WHERE (`creature_entry`, `map_id`) IN ((14682,33),(1720,34),(4420,47),(4422,47),(4425,47),(4428,47),(6168,47),(62503,47),(7079,90),(5713,109),(5714,109),(5716,109),(5717,109),(7354,129),(8567,129),(62679,129),(3976,189),(3977,189),(61972,189),(61982,189),(61983,189),(7272,209),(7274,209),(7605,209),(7606,209),(7608,209),(7795,209),(7797,209),(10082,209),(62495,209),(62498,209),(9217,229),(9219,229),(9718,229),(9736,229),(10376,229),(10509,229),(10899,229),(16080,229),(8923,230),(9025,230),(9041,230),(9042,230),(9056,230),(9319,230),(9502,230),(9543,230),(61316,269),(61575,269),(65122,269),(65124,269),(16118,289),(10393,329),(10809,329),(11032,329),(14684,329),(16101,329),(16102,329),(12225,349),(13596,349),(14690,429),(16097,429),(15385,509),(15386,509),(15388,509),(15389,509),(15390,509),(15391,509),(61319,532),(91919,800),(92935,800),(92107,802),(92108,802),(92109,802),(92110,802),(92111,802),(92133,802),(61418,815),(61421,815),(61422,815),(61423,815),(61605,815),(62066,816),(62670,818),(62757,820),(62779,820),(62781,820),(62783,820),(62784,820))
   AND `note` LIKE '#330 owner-approved class B%';
