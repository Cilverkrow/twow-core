-- ==============================================
-- FILE: 01-empty-houses-quest-credit.sql
-- GENERATED: 20260817211652
-- ==============================================
DELETE FROM `gossip_scripts` WHERE `id` IN (62489, 62153, 62154);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `target_type`, `comments`) VALUES
(62489, 0, 0, 1, 18,    0, 0, 'Judith Flenning - Cry emote on interrogation'),
(62489, 0, 1, 8, 60068, 0, 0, 'Judith Flenning - Quest 41643 objective credit'),
(62153, 0, 0, 8, 60067, 0, 0, 'Bailiff Lancaster - Quest 41643 objective credit'),
(62154, 0, 0, 8, 60066, 0, 0, 'Ignatz - Quest 41643 objective credit');

INSERT INTO `conditions` (`condition_entry`, `type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES
(41643, 9, 41643, 1, 0, 0, 0),
(41768, 9, 41768, 1, 0, 0, 0)
ON DUPLICATE KEY UPDATE `type`=VALUES(`type`), `value1`=VALUES(`value1`), `value2`=VALUES(`value2`), `flags`=VALUES(`flags`);

UPDATE `gossip_menu_option` SET `action_script_id` = 62489, `condition_id` = 41643 WHERE `menu_id` = 62489 AND `id` = 0;
UPDATE `gossip_menu_option` SET `action_script_id` = 62153, `condition_id` = 41643 WHERE `menu_id` = 62153 AND `id` = 0;
UPDATE `gossip_menu_option` SET `condition_id` = 41768                             WHERE `menu_id` = 62153 AND `id` = 1;
UPDATE `gossip_menu_option` SET `action_script_id` = 62154, `condition_id` = 41643 WHERE `menu_id` = 62154 AND `id` = 0;
UPDATE `gossip_menu_option` SET `condition_id` = 41768                             WHERE `menu_id` = 62164 AND `id` = 0;
-- ==============================================
-- FILE: 02-cutpurse-warren-deal.sql
-- ==============================================
-- Cutpurse Warren (62298): the donated-books crate for quest 41636 is
-- obtained either by buying it (confirmed-buy quest 41839, section 13)
-- or by threatening Warren. Both choices are one-time for the player,
-- gated behind an AND condition (quest 41636 active AND crate not owned).
-- The temporary vendor-backed workaround and the earlier silent
-- script-based charge are removed.

-- Clear legacy auto-increment rows with the same definition first:
-- condition_entry is AUTO_INCREMENT and (type,value1,value2,flags,value3,value4)
-- is unique, so an ON DUPLICATE UPDATE would "absorb" pre-existing rows
-- created under different ids instead of creating the explicit ones.
DELETE FROM `conditions` WHERE `type` = 9 AND `value1` = 41636 AND `value2` = 1 AND `flags` = 0;
DELETE FROM `conditions` WHERE `type` = 2 AND `value1` = 41606 AND `value2` = 1 AND `flags` = 1;
DELETE FROM `conditions` WHERE `type` = -1 AND `value1` IN (1678804, 41636) AND `value2` IN (1678815, 4163601);

INSERT INTO `conditions` (`condition_entry`, `type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES
(41636, 9, 41636, 1, 0, 0, 0),
(4163601, 2, 41606, 1, 0, 0, 1),
(4163602, -1, 41636, 4163601, 0, 0, 0)
ON DUPLICATE KEY UPDATE `type`=VALUES(`type`), `value1`=VALUES(`value1`), `value2`=VALUES(`value2`), `value3`=VALUES(`value3`), `value4`=VALUES(`value4`), `flags`=VALUES(`flags`);

DELETE FROM `npc_vendor` WHERE `entry` = 62298 AND `item` = 41606;
UPDATE `creature_template` SET `npc_flags` = `npc_flags` & ~128 WHERE `entry` = 62298;
UPDATE `item_template` SET `buy_price` = 0 WHERE `entry` = 41606 AND `buy_price` = 2000;

DELETE FROM `gossip_scripts` WHERE `id` IN (6229805, 6229806);
DELETE FROM `gossip_menu_option` WHERE `menu_id` IN (62298, 30348);

-- Threat branch: close gossip, become temporarily hostile, then attack.
INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(6229805, 0, 0, 22, 14, 3, 'Cutpurse Warren threat: become hostile and attack'),
(6229805, 0, 1, 26, 0, 0, 'Cutpurse Warren threat: attack the player');

-- Paid branch: create the quest item after validating money and inventory
-- (SCRIPT_COMMAND_CREATE_ITEM datalong3 = copper cost, data_flags 8 = validate).
INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `datalong3`, `data_flags`, `comments`) VALUES
(6229806, 0, 0, 17, 41606, 1, 2000, 8, 'Cutpurse Warren deal: charge 20 silver and create the donated-books crate');

-- Handover option leads to the follow-up menu with the pay/threat choices.
INSERT INTO `gossip_menu_option` (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`, `option_id`, `npc_option_npcflag`, `action_menu_id`, `action_poi_id`, `action_script_id`, `box_coded`, `box_money`, `box_text`, `box_broadcast_text`, `condition_id`) VALUES
(62298, 0, 7, 'Hand over the crate with donations from Stormwind!', 6229801, 1, 1, 30348, 0, 0, 0, 0, '', 0, 4163602),
(30348, 0, 6, 'Take it and get out of Northwind <Pay 20 silver>', 0, 1, 1, -1, 0, 6229806, 0, 0, '', 0, 4163602),
(30348, 1, 7, 'As if! Today you''ll draw your last breath!', 0, 1, 1, -1, 0, 6229805, 0, 0, '', 0, 4163602)
ON DUPLICATE KEY UPDATE `option_text`=VALUES(`option_text`), `action_menu_id`=VALUES(`action_menu_id`), `action_script_id`=VALUES(`action_script_id`), `condition_id`=VALUES(`condition_id`);
-- FILE: 05-messenger-of-northwind-reports.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: The Messenger of Northwind (41768) report handover.
-- Sir Amberwood (62164) and Bailiff Lancaster (62153) both display their
-- report dialogue but never create the required quest items. Wire the two
-- gossip options to SCRIPT_COMMAND_CREATE_ITEM scripts (script id =
-- NPC entry * 100 + option id, matching the existing Bolvar/Magni report
-- pattern), and close Lancaster's gossip window after the handover
-- (action_menu_id = -1). Lord Amberwood's Report (41864) is obtained by
-- opening the Amberwood Chest given on quest accept (SrcItemId 36669) and
-- needs no change.

DELETE FROM `gossip_scripts` WHERE `id` IN (6216401, 6215301);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(6216401, 0, 0, 17, 41865, 1, 'Sir Amberwood - Quest 41768: give Sir Amberwood''s Report'),
(6215301, 0, 0, 17, 41866, 1, 'Bailiff Lancaster - Quest 41768: give Bailiff Lancaster''s Report');

UPDATE `gossip_menu_option` SET `action_script_id` = 6216401 WHERE `menu_id` = 62164 AND `id` = 0;
UPDATE `gossip_menu_option` SET `action_script_id` = 6215301, `action_menu_id` = -1 WHERE `menu_id` = 62153 AND `id` = 1;

-- ==============================================
-- FILE: 06-school-assistance-credit.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: School Assistance (41637) quiz credit.
-- Lloyd, Ellie, Randolph and Tio display their quiz dialogue and
-- validation replies but never award the four objectives. Wire each
-- correct-answer gossip option to a SCRIPT_COMMAND_KILL_CREDIT script
-- and gate the options behind the active-quest condition, matching the
-- Empty Houses (41643) pattern.
-- Correct answers: Lloyd -> Daria Balor, Ellie -> Gold veins,
-- Randolph -> Barathen Wrynn, Tio -> Prestor family.

DELETE FROM `gossip_scripts` WHERE `id` IN (62300, 62301, 62302, 62303);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(62300, 0, 0, 8, 60078, 0, 'Lloyd - Quest 41637 objective credit (Daria Balor)'),
(62301, 0, 0, 8, 60063, 0, 'Ellie - Quest 41637 objective credit (Gold veins)'),
(62302, 0, 0, 8, 60064, 0, 'Randolph - Quest 41637 objective credit (Barathen Wrynn)'),
(62303, 0, 0, 8, 60065, 0, 'Tio - Quest 41637 objective credit (Prestor family)');

INSERT INTO `conditions` (`condition_entry`, `type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES
(41637, 9, 41637, 1, 0, 0, 0)
ON DUPLICATE KEY UPDATE `type`=VALUES(`type`), `value1`=VALUES(`value1`), `value2`=VALUES(`value2`), `flags`=VALUES(`flags`);

UPDATE `gossip_menu_option` SET `action_script_id` = 62300, `condition_id` = 41637 WHERE `menu_id` = 62300 AND `id` = 2;
UPDATE `gossip_menu_option` SET `action_script_id` = 62301, `condition_id` = 41637 WHERE `menu_id` = 62301 AND `id` = 0;
UPDATE `gossip_menu_option` SET `action_script_id` = 62302, `condition_id` = 41637 WHERE `menu_id` = 62302 AND `id` = 1;
UPDATE `gossip_menu_option` SET `action_script_id` = 62303, `condition_id` = 41637 WHERE `menu_id` = 62303 AND `id` = 2;

-- ==============================================
-- FILE: 07-randolph-menu-cleanup.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: remove duplicate quiz options from Randolph's menu (62302).
-- The menu contained authoring leftovers: a second "Barathen Wrynn."
-- (id 3, no reply menu or script) and a second "Llane Wrynn." (id 4,
-- pointing at the wrong-answer text menu 30355). Removing them leaves
-- one option per answer; the single "Barathen Wrynn." (id 1) carries the
-- School Assistance (41637) credit script.

DELETE FROM `gossip_menu_option` WHERE `menu_id` = 62302 AND `id` IN (3, 4);

-- ==============================================
-- FILE: 08-quiz-answer-feedback.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: School Assistance (41637) quiz answer feedback.
-- Wrong answers were silent (no reply, window stayed open). Wire every
-- wrong-answer option to the existing "try again" reply menu (30355).
-- Tio's correct answer (Prestor family) pointed at the wrong-answer
-- menu, so give him a proper validation reply (new broadcast_text,
-- npc_text and gossip_menu rows).

UPDATE `gossip_menu_option` SET `action_menu_id` = 30355 WHERE `menu_id` = 62300 AND `id` IN (0, 1);
UPDATE `gossip_menu_option` SET `action_menu_id` = 30355 WHERE `menu_id` = 62301 AND `id` IN (1, 2);
UPDATE `gossip_menu_option` SET `action_menu_id` = 30355 WHERE `menu_id` = 62302 AND `id` IN (0, 2);
UPDATE `gossip_menu_option` SET `action_menu_id` = 30355 WHERE `menu_id` = 62303 AND `id` IN (0, 1);

INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`, `chat_type`, `sound_id`, `language_id`, `emote_id1`, `emote_id2`, `emote_id3`, `emote_delay1`, `emote_delay2`, `emote_delay3`)
VALUES (6230306, 'Yes, it was the Prestor family! Thank you for reminding me!', 'Yes, it was the Prestor family! Thank you for reminding me!', 0, 0, 0, 0, 0, 0, 0, 0, 0)
ON DUPLICATE KEY UPDATE `male_text`=VALUES(`male_text`), `female_text`=VALUES(`female_text`);

INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`) VALUES (6230304, 6230306)
ON DUPLICATE KEY UPDATE `BroadcastTextID0`=VALUES(`BroadcastTextID0`);

INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (30356, 6230304)
ON DUPLICATE KEY UPDATE `text_id`=VALUES(`text_id`);

UPDATE `gossip_menu_option` SET `action_menu_id` = 30356 WHERE `menu_id` = 62303 AND `id` = 2;

-- ==============================================
-- FILE: 09-quiz-gating-ellie-reply.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: School Assistance (41637) quiz polish.
-- 1) Gate every quiz option behind the active-quest condition so the
--    children show no answer choices once the quest is abandoned,
--    completed, or not yet accepted (only the correct-answer options
--    were gated before).
-- 2) Ellie's joke answer "Heaps of candy." gets its own reply line
--    instead of the generic try-again text (new broadcast_text,
--    npc_text and gossip_menu rows).

UPDATE `gossip_menu_option` SET `condition_id` = 41637 WHERE `menu_id` = 62300 AND `id` IN (0, 1);
UPDATE `gossip_menu_option` SET `condition_id` = 41637 WHERE `menu_id` = 62301 AND `id` IN (1, 2);
UPDATE `gossip_menu_option` SET `condition_id` = 41637 WHERE `menu_id` = 62302 AND `id` IN (0, 2);
UPDATE `gossip_menu_option` SET `condition_id` = 41637 WHERE `menu_id` = 62303 AND `id` IN (0, 1);

INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`, `chat_type`, `sound_id`, `language_id`, `emote_id1`, `emote_id2`, `emote_id3`, `emote_delay1`, `emote_delay2`, `emote_delay3`)
VALUES (6230106, 'Hehe, you''re funny!', 'Hehe, you''re funny!', 0, 0, 0, 0, 0, 0, 0, 0, 0)
ON DUPLICATE KEY UPDATE `male_text`=VALUES(`male_text`), `female_text`=VALUES(`female_text`);

INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`) VALUES (6230104, 6230106)
ON DUPLICATE KEY UPDATE `BroadcastTextID0`=VALUES(`BroadcastTextID0`);

INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (30358, 6230104)
ON DUPLICATE KEY UPDATE `text_id`=VALUES(`text_id`);

UPDATE `gossip_menu_option` SET `action_menu_id` = 30358 WHERE `menu_id` = 62301 AND `id` = 2;

-- ==============================================
-- FILE: 10-balor-book-pages.sql
-- GENERATED: 20260817211652
-- ==============================================
-- Northwind: link the Balor lore book pages.
-- The Siege of Balor (GO 2020127) and The Founding of Balor (GO 2020128)
-- only showed their first page because every page_text row had
-- next_page = 0. Chain the existing pages: Siege = 50734-50747
-- (14 pages), Founding = 50748-50757 (10 pages). The last page of each
-- chain keeps next_page = 0.

UPDATE `page_text` SET `next_page` = `entry` + 1 WHERE `entry` BETWEEN 50734 AND 50746;
UPDATE `page_text` SET `next_page` = `entry` + 1 WHERE `entry` BETWEEN 50748 AND 50756;


-- ==============================================
-- FILE: 11-sara-comb.sql
-- ==============================================
-- Sara Flenning's corpse (62490) only displayed a description text with
-- no interaction, so Sara's Comb (41695) could not be obtained for
-- quest 41648 (Deathcap And Widow's Frill). Add a "search the body"
-- gossip option that creates the comb, gated behind the active quest.
-- The item is unique (max_count = 1), so no one-time condition is needed.

INSERT INTO `conditions` (`condition_entry`, `type`, `value1`, `value2`, `value3`, `value4`, `flags`) VALUES
(41648, 9, 41648, 1, 0, 0, 0)
ON DUPLICATE KEY UPDATE `type`=VALUES(`type`), `value1`=VALUES(`value1`), `value2`=VALUES(`value2`), `flags`=VALUES(`flags`);

INSERT INTO `gossip_scripts` (`id`, `delay`, `priority`, `command`, `datalong`, `datalong2`, `comments`) VALUES
(62490, 0, 0, 17, 41695, 1, 'Sara Flenning corpse - Quest 41648: give Sara''s Comb');

INSERT INTO `gossip_menu_option` (`menu_id`, `id`, `option_icon`, `option_text`, `option_broadcast_text`, `option_id`, `npc_option_npcflag`, `action_menu_id`, `action_poi_id`, `action_script_id`, `box_coded`, `box_money`, `box_text`, `box_broadcast_text`, `condition_id`) VALUES
(62490, 0, 0, 'Search the body for anything useful.', 0, 1, 1, -1, 0, 62490, 0, 0, '', 0, 41648)
ON DUPLICATE KEY UPDATE `option_text`=VALUES(`option_text`), `action_menu_id`=VALUES(`action_menu_id`), `action_script_id`=VALUES(`action_script_id`), `condition_id`=VALUES(`condition_id`);

-- ==============================================
-- FILE: 12-shadow-vision-credit.sql
-- ==============================================
-- Quest 41684 (Shadow's Vision) objective "Perpetrators found" pointed at
-- dummy trigger 60071 that nothing ever credited (the Stormreaver Tracker
-- has ai_name EventAI but no events). Point the objective at the tracker
-- itself (62265) so the engine awards native kill credit; the custom
-- objective label comes from quest_template.ObjectiveText1 and is
-- unchanged, and the missive drop (creature_loot_template 41747, -100%)
-- is untouched.

UPDATE `quest_template` SET `ReqCreatureOrGOId1` = 62265 WHERE `entry` = 41684;
-- ==============================================
-- FILE: 13-gossip-buy-cleanup.sql
-- ==============================================
-- The crate purchase is pure gossip (handover -> pay/threat options,
-- section 02); no quest is involved. Remove the quest-based variant if
-- it was applied from an earlier revision.

DELETE FROM `creature_involvedrelation` WHERE `quest` = 41839;
DELETE FROM `creature_questrelation` WHERE `quest` = 41839;
DELETE FROM `quest_template` WHERE `entry` = 41839;
