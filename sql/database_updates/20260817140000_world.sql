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
