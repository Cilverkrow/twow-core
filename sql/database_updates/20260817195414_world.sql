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
VALUES (6230306, 'Yes, it was the Prestor family! Thank you for reminding me!', 'Yes, it was the Prestor family! Thank you for reminding me!', 0, 0, 0, 0, 0, 0, 0, 0, 0);

INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`) VALUES (6230304, 6230306);

INSERT INTO `gossip_menu` (`entry`, `text_id`) VALUES (30356, 6230304);

UPDATE `gossip_menu_option` SET `action_menu_id` = 30356 WHERE `menu_id` = 62303 AND `id` = 2;
