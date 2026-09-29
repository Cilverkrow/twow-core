-- twow-repo#443 (train 9): Karrsh the Sentinel (62934), first boss of Timbermaw Hold (map 819).
-- Owner design 2026-09-29 (video 1). C++ script boss_karrsh_the_sentinel with its four totems
-- npc_karrsh_totem (65300-65303, own entries for bot target priority, OB-10). Existing client
-- spells are reused with scripted values, so no client patch is needed.
-- Section 3 (rank 3 + boss loot registry) follows the owner decision on Karrsh; merge only
-- after the owner's go (OB-00, twow-repo#443).
-- Replay-safe: INSERT IGNORE and guarded UPDATEs.
-- Rollback:
--   UPDATE `creature_template` SET `script_name` = '', `ai_name` = 'EventAI', `rank` = 0 WHERE `entry` = 62934;
--   DELETE FROM `creature_template` WHERE `entry` BETWEEN 65300 AND 65303;
--   DELETE FROM `creature_loot_bonus_registry` WHERE `creature_entry` = 62934 AND `map_id` = 819;
--   DELETE FROM `broadcast_text` WHERE `entry` BETWEEN 6530001 AND 6530004;

-- 1. Totems (clones of the shaman totems; level 63, 800 hp, Karrsh's faction)
CREATE TEMPORARY TABLE `tmp_ct` LIKE `creature_template`;
INSERT IGNORE INTO `tmp_ct` SELECT * FROM `creature_template` WHERE `entry` IN (2630, 6112, 5929, 3902);
UPDATE `tmp_ct` SET `entry` = 65300, `name` = 'Earthbind Totem'       WHERE `entry` = 2630;
UPDATE `tmp_ct` SET `entry` = 65301, `name` = 'Windfury Totem'        WHERE `entry` = 6112;
UPDATE `tmp_ct` SET `entry` = 65302, `name` = 'Lava Nova Totem'       WHERE `entry` = 5929;
UPDATE `tmp_ct` SET `entry` = 65303, `name` = 'Chain Lightning Totem' WHERE `entry` = 3902;
UPDATE `tmp_ct` SET `subname` = 'Karrsh the Sentinel', `level_min` = 63, `level_max` = 63,
    `health_min` = 800, `health_max` = 800, `faction` = 16, `ai_name` = '', `script_name` = 'npc_karrsh_totem';
INSERT IGNORE INTO `creature_template` SELECT * FROM `tmp_ct`;
DROP TEMPORARY TABLE `tmp_ct`;

-- 2. Karrsh uses the C++ script instead of the empty EventAI
UPDATE `creature_template` SET `ai_name` = '', `script_name` = 'boss_karrsh_the_sentinel'
WHERE `entry` = 62934 AND `script_name` = '';

-- 3. Owner decision pending (OB-00): boss rank and boss loot registry, like Selenaxx (#429 variant A)
UPDATE `creature_template` SET `rank` = 3 WHERE `entry` = 62934 AND `rank` = 0;
INSERT IGNORE INTO creature_loot_bonus_registry (creature_entry, map_id, category, note) VALUES
    (62934,819,'raid','#443 owner: Karrsh the Sentinel, first boss of Timbermaw Hold');

-- 4. Yells (own English lines; broadcast_text 6530001-6530004, yell, no sound)
INSERT IGNORE INTO `broadcast_text`
  (`entry`, `male_text`, `female_text`, `chat_type`, `sound_id`, `language_id`,
   `emote_id1`, `emote_id2`, `emote_id3`, `emote_delay1`, `emote_delay2`, `emote_delay3`)
VALUES
(6530001, 'The Hold is closed to outsiders. The shadow in the roots commands it!', 'The Hold is closed to outsiders. The shadow in the roots commands it!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6530002, 'Earth, wind, bind them! The corruption will not be stopped by the likes of you!', 'Earth, wind, bind them! The corruption will not be stopped by the likes of you!', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6530003, 'Another one for the dark below.', 'Another one for the dark below.', 1, 0, 0, 0, 0, 0, 0, 0, 0),
(6530004, 'The whispers... they are gone... Timbermaw... forgive...', 'The whispers... they are gone... Timbermaw... forgive...', 1, 0, 0, 0, 0, 0, 0, 0, 0);
