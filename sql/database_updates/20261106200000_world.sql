-- twow-repo#579 follow-up, main train 10 (order OB-00 06.11.2026; classification OB-20 of the OB-50 quest comparison,
-- evidence/ws-10/cli-quest-organic/ob50-l*/ob20-einordnung-*.tsv): quest relations that make quests dead ends or
-- unreachable although every NPC/object involved is spawned. Only quest relations change; no spawn, no quest text.
--  1. 80206 "Burnt Wheels" (high elf, L3): Vyrin Swiftwind (1156, Farstrider Lodge) offers it, but its only turn-in NPC,
--     Kathy Wake (80202), has no spawn, no script/spell summon and no event spawn; no data source knows her position
--     (old high elf start, 80200/80204/80217 are [DEPRECATED]). Vyrin no longer offers 80206 (80207-80209 stay
--     unreachable as before). Skipped if Kathy Wake ever gets a spawn.
--  2. 3821 "Dreadmaul Rock" / 3822 "Krom'Grul" (Horde, L52): both point at the creature Sha'ni Proudtusk (9136, no spawn),
--     while the quest-giver object Sha'ni Proudtusk's Remains (GO 160445, type 2, spawned in Burning Steppes, guid 4523)
--     has no relation. As in vanilla, 3821 ends and 3822 starts at the remains. Relations move from 9136 to 160445.
--  3. 41799 "A Hydromancer's Curiosity" (L28): no giver and no item start. The Turtle wiki names Marine Hornby (60936,
--     spawned, offers nothing) as giver; the turn-in Hydromancer Finnigan (62460) is already set.
--  4. 41921 "A Familiar Disturbance" / 41922 "Homecoming" (L52, Azshara): 41921 has neither giver nor turn-in, 41922 no
--     giver. Turtle wiki: 41921 Akh Z'ador -> Elder Krasheen, 41922 Elder Krasheen -> Sanv K'la. Akh Z'ador 91782 (gives
--     the Rift Fatigue quests 41321/41322 that 41921 follows), Elder Krasheen 62863 and Sanv K'la 91781 (already the
--     turn-in of 41922) are spawned.
-- Idempotent: INSERT IGNORE / guarded DELETE; removed rows are kept in backup tables. End-state CHECK.
-- Rollback (exact):
--   INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`) SELECT `id`, `quest` FROM `creature_questrelation_bak_80206`;
--   INSERT IGNORE INTO `creature_involvedrelation` (`id`, `quest`) SELECT `id`, `quest` FROM `creature_involvedrelation_bak_80206`;
--   DELETE FROM `gameobject_involvedrelation` WHERE `id` = 160445 AND `quest` = 3821;
--   DELETE FROM `gameobject_questrelation` WHERE `id` = 160445 AND `quest` = 3822;
--   DELETE FROM `creature_questrelation` WHERE (`id`, `quest`) IN ((60936, 41799), (91782, 41921), (62863, 41922));
--   DELETE FROM `creature_involvedrelation` WHERE `id` = 62863 AND `quest` = 41921;

CREATE TABLE IF NOT EXISTS `creature_questrelation_bak_80206` LIKE `creature_questrelation`;
CREATE TABLE IF NOT EXISTS `creature_involvedrelation_bak_80206` LIKE `creature_involvedrelation`;

-- 1. 80206: Vyrin Swiftwind stops offering it while Kathy Wake has no spawn.
INSERT IGNORE INTO `creature_questrelation_bak_80206` (`id`, `quest`)
SELECT `id`, `quest` FROM `creature_questrelation`
 WHERE `id` = 1156 AND `quest` = 80206
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 80202 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));
DELETE FROM `creature_questrelation`
 WHERE `id` = 1156 AND `quest` = 80206
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 80202 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));

-- 2. 3821/3822: from the unspawned creature 9136 to the spawned remains GO 160445.
INSERT IGNORE INTO `gameobject_involvedrelation` (`id`, `quest`)
SELECT 160445, 3821 FROM DUAL WHERE EXISTS (SELECT 1 FROM `gameobject` WHERE `id` = 160445)
                                AND EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 3821);
INSERT IGNORE INTO `gameobject_questrelation` (`id`, `quest`)
SELECT 160445, 3822 FROM DUAL WHERE EXISTS (SELECT 1 FROM `gameobject` WHERE `id` = 160445)
                                AND EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 3822);
INSERT IGNORE INTO `creature_involvedrelation_bak_80206` (`id`, `quest`)
SELECT `id`, `quest` FROM `creature_involvedrelation`
 WHERE `id` = 9136 AND `quest` = 3821
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 9136 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));
INSERT IGNORE INTO `creature_questrelation_bak_80206` (`id`, `quest`)
SELECT `id`, `quest` FROM `creature_questrelation`
 WHERE `id` = 9136 AND `quest` = 3822
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 9136 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));
DELETE FROM `creature_involvedrelation`
 WHERE `id` = 9136 AND `quest` = 3821
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 9136 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));
DELETE FROM `creature_questrelation`
 WHERE `id` = 9136 AND `quest` = 3822
   AND NOT EXISTS (SELECT 1 FROM `creature` c WHERE 9136 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`));

-- 3. 41799: Marine Hornby offers it (only if the quest still has no giver).
INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`)
SELECT 60936, 41799 FROM DUAL
 WHERE EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 41799)
   AND NOT EXISTS (SELECT 1 FROM `creature_questrelation` WHERE `quest` = 41799);

-- 4. 41921/41922: Akh Z'ador -> Elder Krasheen -> Sanv K'la (each only if that side is still empty).
INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`)
SELECT 91782, 41921 FROM DUAL
 WHERE EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 41921)
   AND NOT EXISTS (SELECT 1 FROM `creature_questrelation` WHERE `quest` = 41921);
INSERT IGNORE INTO `creature_involvedrelation` (`id`, `quest`)
SELECT 62863, 41921 FROM DUAL
 WHERE EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 41921)
   AND NOT EXISTS (SELECT 1 FROM `creature_involvedrelation` WHERE `quest` = 41921);
INSERT IGNORE INTO `creature_questrelation` (`id`, `quest`)
SELECT 62863, 41922 FROM DUAL
 WHERE EXISTS (SELECT 1 FROM `quest_template` WHERE `entry` = 41922)
   AND NOT EXISTS (SELECT 1 FROM `creature_questrelation` WHERE `quest` = 41922);

-- End state. The updater ignores statement results, so a failed or skipped step must fail
-- here: the CHECK turns a wrong end state into an error (temporary table, no cleanup needed).
CREATE TEMPORARY TABLE IF NOT EXISTS `tmp_check_80206` (`ok` TINYINT(1) NOT NULL CHECK (`ok` = 1));
INSERT INTO `tmp_check_80206` (`ok`)
SELECT ((SELECT COUNT(*) FROM `creature` c WHERE 80202 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`)) > 0
        OR (SELECT COUNT(*) FROM `creature_questrelation` WHERE `quest` = 80206) = 0)
   AND (SELECT COUNT(*) FROM `gameobject_involvedrelation` WHERE `id` = 160445 AND `quest` = 3821) = 1
   AND (SELECT COUNT(*) FROM `gameobject_questrelation` WHERE `id` = 160445 AND `quest` = 3822) = 1
   AND ((SELECT COUNT(*) FROM `creature` c WHERE 9136 IN (c.`id`, c.`id2`, c.`id3`, c.`id4`)) > 0
        OR ((SELECT COUNT(*) FROM `creature_involvedrelation` WHERE `id` = 9136 AND `quest` = 3821) = 0
            AND (SELECT COUNT(*) FROM `creature_questrelation` WHERE `id` = 9136 AND `quest` = 3822) = 0))
   AND (SELECT COUNT(*) FROM `creature_questrelation` WHERE `quest` = 41799) >= 1
   AND (SELECT COUNT(*) FROM `creature_questrelation` WHERE `quest` = 41921) >= 1
   AND (SELECT COUNT(*) FROM `creature_involvedrelation` WHERE `quest` = 41921) >= 1
   AND (SELECT COUNT(*) FROM `creature_questrelation` WHERE `quest` = 41922) >= 1;
