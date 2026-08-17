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
VALUES (6230106, 'Hehe, you''re funny!', 'Hehe, you''re funny!', 0, 0, 0, 0, 0, 0, 0, 0, 0);

INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`) VALUES (6230104, 6230106);

INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (30358, 6230104);

UPDATE `gossip_menu_option` SET `action_menu_id` = 30358 WHERE `menu_id` = 62301 AND `id` = 2;
